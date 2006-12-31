#!./parrot

# $Id$

=head1 NAME

plumhead.pir - implementations of PHP

=head1 DESCRIPTION

Driver for three variants of PHP on Parrot.

Take XML from phc and transform it with XSLT to PIR setting up PAST.
Run the PAST with the help of TGE.

Parse PHP with Java Parser and TreeParser, generated from ANTLR3 grammars.

Parser PHP with the Parrot compiler tools.

=cut

.const string VERSION="0.0.1"

.include "library/dumper.pir"

.sub plumhead :main
    .param pmc argv
    # _dumper( argv )

    .local string rest
    .local pmc    opt
    ( opt, rest ) = parse_options(argv)

    .local string php_source_fn 
    php_source_fn = opt['f']
    if php_source_fn goto GOT_PHP_SOURCE_FN
        php_source_fn = rest
GOT_PHP_SOURCE_FN:

    .local string cmd, err_msg
    .local int ret
    
    .local string variant
    variant = opt['variant']

    if variant == 'antlr3' goto VARIANT_ANTLR3

VARIANT_PHC:
    err_msg = 'Creating XML-AST with phc failed'
    cmd = 'phc --dump-ast-xml '
    concat cmd, php_source_fn
    concat cmd, '> plumhead_phc_ast.xml'
    ret = spawnw cmd
    if ret goto ERROR

    err_msg = 'Creating XML-PAST with xsltproc failed'
    cmd = 'xsltproc languages/plumhead/src/phc/phc_xml_to_past_xml.xsl plumhead_phc_ast.xml > plumhead_past.xml'
    ret = spawnw cmd
    if ret goto ERROR

    err_msg = 'Creating PIR with xsltproc failed'
    cmd = 'xsltproc languages/plumhead/src/phc/past_xml_to_past_pir.xsl  plumhead_past.xml  > plumhead_past.pir'
    ret = spawnw cmd
    if ret goto ERROR
    goto EXECUTE_PAST_PIR

VARIANT_PARTRIDGE:
    # TODO: really use partridge
    err_msg = 'Creating XML-AST with phc failed'
    cmd = 'phc --dump-ast-xml '
    concat cmd, php_source_fn
    concat cmd, '> plumhead_phc_ast.xml'
    ret = spawnw cmd
    if ret goto ERROR

    err_msg = 'Creating XML-PAST with xsltproc failed'
    cmd = 'xsltproc languages/plumhead/src/phc/phc_xml_to_past_xml.xsl plumhead_phc_ast.xml > plumhead_past.xml'
    ret = spawnw cmd
    if ret goto ERROR

    err_msg = 'Creating PIR with xsltproc failed'
    cmd = 'xsltproc languages/plumhead/src/common/past_xml_to_past_pir.xsl  plumhead_past.xml  > plumhead_past.pir'
    ret = spawnw cmd
    if ret goto ERROR
    goto EXECUTE_PAST_PIR

VARIANT_ANTLR3:
    err_msg = 'Generating PAST from PHP source failed'
    cmd = 'java PlumheadAntlr3 '
    concat cmd, php_source_fn
    concat cmd, ' plumhead_past.pir'
    ret = spawnw cmd
    if ret goto ERROR

EXECUTE_PAST_PIR:
    err_msg = 'Executing plumhead_past.pir with parrot failed'
    cmd = './parrot plumhead_past.pir'
    ret = spawnw cmd
    if ret goto ERROR

    # Clean up temporary files
    #.local pmc os
    #os = new .OS
    # os."rm"('plumhead_phc_ast.xml')
    # os."rm"('plumhead_past.xml')
    # os."rm"('plumhead_past.pir')

    exit 0

ERROR:
    printerr err_msg
    printerr "\n"
    # Clean up temporary files
    #.local pmc os
    #os = new .OS
    #os."rm"('plumhead_phc_ast.xml')
    #os."rm"('plumhead_past.xml')
    #os."rm"('plumhead_past.pir')

   exit ret

.end


# get commandline options
.sub parse_options
    .param pmc argv

    load_bytecode "Getopt/Obj.pbc"

    .local string prog
    prog = shift argv

    # Specification of command line arguments.
    # --version, --debug, --inv=nnn, --builtin=name, --nc, --help
    .local pmc getopts
    getopts = new 'Getopt::Obj'
    push getopts, 'version'
    push getopts, 'debug'
    push getopts, 'help'
    push getopts, 'd:%'
    push getopts, 'r=s'
    push getopts, 'f=s'
    push getopts, 'C'
    push getopts, 'variant=s'

    .local pmc opt
    opt = getopts."get_options"(argv)

    $I0 = defined opt['version']
    unless $I0 goto n_ver
	print prog
	print " "
	print VERSION
	print "\n"
	end
n_ver:
    $I0 = defined opt['help']
    unless $I0 goto n_help
help:
    print "usage: "
    print prog
    print " [options...] [file]\n"
    print "see\n\tperldoc -F "
    print prog
    print "\nfor more\n"
    end

n_help:
    $I0 = defined opt['debug']
    unless $I0 goto n_deb
	print "debugging on\n"
n_deb:

    .local int argc
    .local string rest
    argc = elements argv
    if argc < 1 goto help
    dec argc
    rest = argv[argc]

    .return (opt, rest )
.end

=head1 SEE ALSO

=head1 AUTHOR

Bernhard Schmalhofer - L<Bernhard.Schmalhofer@gmx.de>

=cut

# vim: ft=imc sw=4:
