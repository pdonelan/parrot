/* intlists.c
 *  Copyright: (When this is determined...it will go here)
 *  CVS Info
 *     $Id$
 *  Overview:
 *     Regex stack handling routines for Parrot
 *  Data Structure and Algorithms:
 *
 *     The basic data structure is a variant of a doubly-linked list
 *     of 'chunks', where a chunk is a Buffer header subclass
 *     containing the link pointers and other metadata for the chunk.
 *     As expected from it being a Buffer header, the .bufstart field
 *     points to the actual array of INTVALs. The handle used by
 *     external code for one of these IntLists is just a pointer to a
 *     chunk, always called 'list' in this code.
 *
 *     For now, all of the chunks are fixed-length in size. (That
 *     could easily be changed, at the cost of another integer in each
 *     header.)
 *
 *     Notice that I said 'variant' of a doubly-linked list. That is
 *     because if you start at 'list' and follow prev pointers, you
 *     will loop through all the used nodes of the list, as usual. But
 *     if you follow next pointers instead, you might find a spare
 *     node hanging off the last node in the list (the last node is
 *     always list->prev, so if there is a spare node, it will be at
 *     list->prev->next. If no spare exists, then
 *     list->prev->next==list.)
 *
 *     The first node in the list may be partly full; the intermediate
 *     nodes are always completely full; and the last node may be
 *     partly full. Each node has a .start field, giving the offset of
 *     the first valid element (always zero except for possibly the
 *     first node), and a .end field, giving one past the offset of
 *     the last valid element (always equal to INTLIST_CHUNK_SIZE
 *     except for possibly the last node).
 *
 *     To make it concrete, let's walk through some sample operations.
 *     To push onto the end of the list, first find the last chunk:
 *     list->prev. Then if chunk->end < INTLIST_CHUNK_SIZE, there is
 *     space to fit another element and so just stick it in. If not,
 *     we must add a chunk to the end of the list. If there is a
 *     spare, just link it fully into the list (forming a conventional
 *     doubly-linked list). Otherwise, create a new chunk and link it
 *     fully into the list. Easy enough.
 *
 *     To pop something off the end, first go to the end chunk
 *     (list->prev). Pop off an element and decrement .end if the
 *     chunk is nonempty. If it is empty, make that last chunk into
 *     the spare (discarding the previous spare). Then go to the
 *     previous chunk, which is guaranteed to have .end set to
 *     INTLIST_CHUNK_SIZE, and return data[.end--].
 *
 *     The length of the list is always cached in the overall header
 *     chunk. If an operation changes which chunk is the header (i.e.,
 *     shift or unshift), then the length is copied to the new header.
 *
 *     Invariants:
 *
 *     There is always space in list->prev to insert an element.
 *     
 *     The 'list' chunk is never empty unless the entire list is
 *     empty.
 *
 *     In combination, the above invariants imply that the various
 *     operations are implemented as:
 *
 *     push: write element, push a new chunk if necessary
 *     pop: check to see if we have to back up a chunk, read element
 *     shift: read element, discard chunk and advance if necessary
 *     unshift: unshift a chunk if necessary, write element
 *
 *     Direct aka indexed access of intlist data:
 *
 *     The classic method would be to walk the intlist->next pointers
 *     (or optimized, the ->prev pointers if an index near the end is
 *     requested) and locate the chunk, that holds the wanted list
 *     item.
 *     To speed things up, especially for bigger lists, there are
 *     additional fields in the 'list' (the head chunk):
 *     chunk_list  ... holds pointers to individual chunks
 *     collect_runs ... collect_runs counter, when chunk_list was
 *                     rebuilt last.
 *     n_chunks ... used length in chunk_list
 *
 *     If on any indexed access interpreter's collect_runs is
 *     different, the chunks might have been moved, so the chunk_list
 *     has to be rebuilt.
 *
 *
 *  History:
 *  Notes:
 * References: */

#include "parrot/parrot.h"
#include "parrot/intlist.h"

static size_t rebuild_chunk_list(Interp *interpreter, IntList *list);
static IntList* allocate_chunk(Interp *interpreter, int initial);
static size_t rebuild_chunk_list(Interp *interpreter, IntList *list);

IntList*
intlist_new(Interp *interpreter) {
    return allocate_chunk(interpreter, 1);
}

/* growth increment of chunk_list, must be a power of 2 */
#define CL_SIZE 16

/* Buffers store the used len in bytes in buflen
 * so, the len in entries is: */

#define chunk_list_size(list) \
                (list->chunk_list.buflen / sizeof(IntList_Chunk *))

/* hide the ugly cast somehow: */
#define chunk_list_ptr(list, idx) \
        ((IntList_Chunk**)list->chunk_list.bufstart)[idx]

static IntList*
allocate_chunk(Interp *interpreter, int initial)
{
    IntList* list;

    interpreter->DOD_block_level++;
    list = (IntList *) new_bufferlike_header(interpreter, sizeof(*list));
    list->start = 0;
    list->end = 0;
    list->length = 0;
    list->next = list;
    list->prev = list;
    list->buffer.bufstart = NULL;
    interpreter->GC_block_level++;
    Parrot_allocate(interpreter, (Buffer*) list,
                    INTLIST_CHUNK_SIZE * sizeof(INTVAL));
    if (initial) {
        /* we have a cleared list, so no need to zero chunk_list */
        rebuild_chunk_list(interpreter, list);
    }
    interpreter->DOD_block_level--;
    interpreter->GC_block_level--;
    return list;
}

PMC*
intlist_mark(Interp* interpreter, IntList* list, PMC* last)
{
    IntList_Chunk* chunk = (IntList_Chunk*) list;
    do {
        buffer_lives((Buffer *) chunk);
        chunk = chunk->next;
    } while (chunk != (IntList_Chunk*) list);
    buffer_lives(&list->chunk_list);
    return last;
}

#ifdef INTLIST_DEBUG
void
intlist_dump(FILE* fp, IntList* list, int verbose)
{
    IntList_Chunk* chunk = (IntList_Chunk*) list;
    IntList_Chunk* lastChunk = list->prev;
    if (fp == NULL) fp = stderr; /* Useful for calling from gdb */

    if (verbose) fprintf(fp, "LIST[%d]: ", (int) chunk->length);

    while (1) {
        int i;

        if (verbose)
            fprintf(fp, "[%d..%d] ", (int) chunk->start, (int) chunk->end-1);

        for (i = chunk->start; i < chunk->end; i++) {
            INTVAL* entries = (INTVAL*) chunk->buffer.bufstart;
            fprintf(fp, INTVAL_FMT " ", entries[i]);
        }
        if (chunk == lastChunk) break;
        chunk = chunk->next;
    }

    fprintf(fp, "\n");
}
#endif

static size_t
rebuild_chunk_list(Interp *interpreter, IntList *list)
{
    IntList_Chunk* chunk = (IntList_Chunk*) list;
    IntList_Chunk* lastChunk = list->prev;
    size_t len = 0;
    /* allocate a new chunk_list buffer, old one my have moved
     * firsr, count chunks */
    while (1) {
        len++;
        if (chunk == lastChunk) break;
        chunk = chunk->next;
    }
    Parrot_allocate(interpreter, &list->chunk_list,
                    (len+1) * sizeof(IntList_Chunk *));
    len = 0;
    chunk = list;
    /* then fill list */
    while (1) {
        chunk_list_ptr(list, len) = (IntList_Chunk*) chunk;
        len++;
        if (chunk == lastChunk) break;
        chunk = chunk->next;
    }
    list->collect_runs = interpreter->collect_runs;
    list->n_chunks = len;
    return len;
}

static void
add_chunk(Interp* interpreter, IntList* list)
{
    IntList_Chunk* chunk = list->prev;

    if (chunk->next == list) {
        /* Need to add a new chunk */
        IntList_Chunk* new_chunk = allocate_chunk(interpreter, 0);
        new_chunk->next = list;
        new_chunk->prev = chunk;
        chunk->next = new_chunk;
        list->prev = new_chunk;
    }
    else {
        /* Reuse the spare chunk we kept */
        list->prev = chunk->next;
    }
}

static void
push_chunk(Interp* interpreter, IntList* list)
{
    add_chunk(interpreter, list);
    list->prev->start = 0;
    list->prev->end = 0;
    /* optimization, add new chunk directly, if still space
     * but only, if no collections was done in between
     */
    if (list->n_chunks >= chunk_list_size(list) ||
            list->collect_runs != interpreter->collect_runs)
    rebuild_chunk_list(interpreter, list);
    else {
        /* add the appended = last chunk = list->prev to chunk_list */
        chunk_list_ptr(list, list->n_chunks) = list->prev;
        list->n_chunks++;
    }
}

static void
unshift_chunk(Interp* interpreter, IntList* list)
{
    add_chunk(interpreter, list);
    list->prev->start = INTLIST_CHUNK_SIZE;
    list->prev->end = INTLIST_CHUNK_SIZE;
}

void
intlist_push(Interp *interpreter, IntList* list, INTVAL data)
{
    IntList_Chunk* chunk = (IntList *) list->prev;
    INTVAL length = list->length + 1;

    ((INTVAL*)chunk->buffer.bufstart)[chunk->end++] = data;

    /* Add on a new chunk if necessary */
    if (chunk->end == INTLIST_CHUNK_SIZE)
        push_chunk(interpreter, list);

    list->length = length;
}

void
intlist_unshift(Interp *interpreter, IntList** list, INTVAL data)
{
    IntList_Chunk* chunk = (IntList_Chunk *) *list;
    INTVAL length = chunk->length + 1;
    INTVAL offset;
    IntList_Chunk * o = chunk;

    /* Add on a new chunk if necessary */
    if (chunk->start == 0) {
        unshift_chunk(interpreter, *list);
        chunk = chunk->prev;
        *list = chunk;
        /* move chunk_list to new list head */
        (*list)->chunk_list = o->chunk_list;
        /* and rebuild it from scratch */
        rebuild_chunk_list(interpreter, *list);
    }

    ((INTVAL*)chunk->buffer.bufstart)[--chunk->start] = data;

    (*list)->length = length;
}

INTVAL
intlist_shift(Interp *interpreter, IntList** list)
{
    IntList_Chunk* chunk = (IntList_Chunk *) *list;
    INTVAL length = chunk->length - 1;
    INTVAL value;
    IntList_Chunk * o = chunk;

    if (chunk->start >= chunk->end) {
        internal_exception(OUT_OF_BOUNDS, "No entries on list!\n");
        return 0;
    }

    value = ((INTVAL*)chunk->buffer.bufstart)[chunk->start++];

    if (chunk->start >= chunk->end) {
        /* Just walked off the end of the initial chunk. Make initial
         * chunk into the spare. */
        chunk->next->prev = chunk->prev;
        chunk->prev->next = chunk;
        *list = chunk->next;
        /* like above */
        (*list)->chunk_list = o->chunk_list;
        rebuild_chunk_list(interpreter, *list);
    }

    (*list)->length = length;

    return value;
}

INTVAL
intlist_pop(Interp *interpreter, IntList* list)
{
    IntList_Chunk* chunk = (IntList *) list->prev;
    INTVAL length = list->length - 1;

    /* We may have an empty chunk at the end of the list */
    if (chunk->start >= chunk->end) {
        /* Discard this chunk by making it the spare. */
        chunk->prev->next = chunk;
        chunk->next = list;
        list->prev = chunk->prev;
        chunk = chunk->prev;
        /* chunk_list hold one less valid entry now */
        list->n_chunks--;
    }

    /* Quick sanity check */
    if (chunk->end == chunk->start) {
        internal_exception(OUT_OF_BOUNDS, "No entries on list!\n");
        return 0;
    }

    list->length = length;

    /* Decrement end and return the value */
    return ((INTVAL*)chunk->buffer.bufstart)[--chunk->end];
}

static IntList_Chunk*
find_chunk(Interp* interpreter, IntList* list, INTVAL idx)
{
    IntList_Chunk* chunk = list;
    UNUSED(interpreter);

    if (list->collect_runs != interpreter->collect_runs)
        rebuild_chunk_list(interpreter, list);
    return chunk_list_ptr(list, idx / INTLIST_CHUNK_SIZE);
}

INTVAL
intlist_get(Interp* interpreter, IntList* list, INTVAL idx)
{
    IntList_Chunk* chunk;
    INTVAL length = list->length;
    
    if (idx >= length || -idx > length) {
        internal_exception(OUT_OF_BOUNDS,
                          "Invalid index, must be " INTVAL_FMT ".." INTVAL_FMT,
                           -length, length-1);
        return 0;
    }

    if (idx < 0) idx += length;
    idx +=  list->start;

    chunk = find_chunk(interpreter, list, idx);

    idx = idx % INTLIST_CHUNK_SIZE;

    return ((INTVAL*)chunk->buffer.bufstart)[idx];
}

static void
intlist_extend(Interp* interpreter, IntList* list, INTVAL length)
{
    IntList_Chunk* chunk = list->prev;
    INTVAL to_add = length - list->length;

    while (to_add > 0) {
        INTVAL available = INTLIST_CHUNK_SIZE - chunk->end;
        INTVAL end;

        /* Zero out all newly added elements */
        end = (to_add <= available) ? chunk->end + to_add : INTLIST_CHUNK_SIZE;
        memset(&((INTVAL*)chunk->buffer.bufstart)[chunk->end],
               0,
               sizeof(INTVAL) * (end - chunk->end));
        to_add -= end - chunk->end;
        chunk->end = end;

        if (to_add > 0) push_chunk(interpreter, list);

        chunk = chunk->next;
    }

    assert(length >= list->length);
    list->length = length;
}

void
intlist_assign(Interp* interpreter, IntList* list, INTVAL idx, INTVAL val)
{
    IntList_Chunk* chunk;
    INTVAL length = list->length;
    
    if (idx < -length) {
        internal_exception(OUT_OF_BOUNDS,
                          "Invalid index, must be " INTVAL_FMT ".." INTVAL_FMT,
                           -length, length-1);
        return;
    }

    if (idx < 0)
        idx += length;
    else if (idx >= length) {
        intlist_extend(interpreter, list, idx + 1);
    }

    idx +=  list->start;
    chunk = find_chunk(interpreter, list, idx);

    idx = idx % INTLIST_CHUNK_SIZE;
    ((INTVAL*)chunk->buffer.bufstart)[idx] = val;
}

/*
 * Local variables:
 * c-indentation-style: bsd
 * c-basic-offset: 4
 * indent-tabs-mode: nil 
 * End:
 *
 * vim: expandtab shiftwidth=4:
*/
