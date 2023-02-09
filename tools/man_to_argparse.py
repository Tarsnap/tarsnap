#!/usr/bin/env python3

""" Write a python argparse object and text file for a given OptList. """

import argparse

_PY_BEGIN = """#!/usr/bin/env python3

\"\"\" Arguments for the tarsnap CLI.  Does nothing else.

    WARNING: options starting with "mode-" are a workaround for
    argparse not being able to have subparsers which start with a hyphen.
    https://bugs.python.org/issue34046

    For the real tarsnap options, remove any "mode-" string.
\"\"\"

import argparse


def get_parser():
    \"\"\" Create a parser for tarsnap command-line arguments. \"\"\"
    parser = argparse.ArgumentParser(prog="tarsnap")
    subparsers = parser.add_subparsers()

    # Commands available for all modes.
"""

_PY_NOARG = """    {parser_name}.add_argument("{opt}", action="store_true",
    {spaces}help="{desc}")
"""
# Don't add "quotes" around the 'type' argument.
_PY_ARG_TYPE = """    {parser_name}.add_argument("{opt}", metavar="{metavar}",
    {spaces}type={argtype},
    {spaces}help="{desc}")
"""

_PY_SUBPARSER = """
    # Commands specific for mode {mode}.
    subparser = subparsers.add_parser("{mode}",
    {spaces}help="{desc}",
    {spaces}description="{desc}")
"""

_PY_END = """    return parser


if __name__ == "__main__":
    tarsnap_parser = get_parser()
    tarsnap_parser.parse_args()
"""


def get_argtypestr(arg):
    """ Return the type of variable to use for the tarsnap arg as a string. """
    if arg in ["bytespercheckpoint", "bytespersecond", "X", "numbytes",
               "count"]:
        argtypestr = "int"
    elif arg in ["cache-dir", "directory"]:
        argtypestr = "directory"
    elif arg in ["filename", "key-file"]:
        argtypestr = "filename"
    elif arg in ["archive-name"]:
        argtypestr = "archive_list_filename"
    elif arg in ["date"]:
        argtypestr = "date"
    elif arg in ["method:arg", "pattern"]:
        argtypestr = "str"
    else:
        print("Unsupported arg:", arg)
        exit(1)
    return argtypestr


def get_argtype(arg):
    """ Return the type of variable to use for the tarsnap arg. """
    argtypestr = get_argtypestr(arg)
    if argtypestr == "str":
        argtype = str
    elif argtypestr == "int":
        argtype = int
    elif ["directory", "filename", "date",
          "archive_list_filename"].index(argtypestr) >= 0:
        # No special handling (yet?)
        argtype = str
    else:
        print("Unsupported argtypestr:\t%s\t%s" % (arg, argtypestr))
        exit(1)
    return argtype


def add_arg(parser_obj, parser_name, optarg):
    """ Add an optarg to the parser_obj called parser_name. """
    # Skip -h because argparse has its own -h --help.
    if optarg.opt == "-h":
        return ""

    spaces = " " * len("%s.add_argument(" % parser_name)
    text = ""
    if optarg.arg:
        argtype = get_argtype(optarg.arg)
        text = _PY_ARG_TYPE.format(parser_name=parser_name, opt=optarg.opt,
                                   spaces=spaces, desc=optarg.desc,
                                   metavar=optarg.arg,
                                   argtype=argtype.__name__)
        parser_obj.add_argument(optarg.opt, metavar=optarg.arg,
                                help=optarg.desc,
                                type=argtype)
    else:
        text = _PY_NOARG.format(parser_name=parser_name, opt=optarg.opt,
                                spaces=spaces, desc=optarg.desc)
        parser_obj.add_argument(optarg.opt, action="store_true",
                                help=optarg.desc)
    return text


def is_mode_global(mode):
    """ Is this mode global? """
    if mode == "":
        return True
    modestr = mode[0]
    if modestr == "(all modes)" or modestr.startswith("(use with"):
        return True
    return False


def add_global_options(parser_obj, parser_name, optlist):
    """ Add all global options to parser_obj. """
    relevant = optlist.get_optargs_with_func_modes(is_mode_global)
    text = ""
    for optarg in relevant:
        text += add_arg(parser_obj, parser_name, optarg)
    return text


def generate(options, optlist, descs):
    """ Generate an argparse object and text for a python file. """

    parser = argparse.ArgumentParser(prog="tarsnap")
    subparsers = parser.add_subparsers()

    text = ""

    # Global options
    text += add_global_options(parser, "parser", optlist)

    for mode in sorted(options["modes"]):
        if len(mode) == 1:
            modestr = "mode-%s" % mode
        else:
            modestr = "mode--%s" % mode

        desc = descs.get(modestr)
        spaces = " " * len("subparser = subparsers.add_parser(")
        text += _PY_SUBPARSER.format(mode=modestr, spaces=spaces, desc=desc)

        # argparse uses "description" for the main text shown in:
        #     ./tarsnap.py mode-c --help
        # and "help" for the mode-c text displayed in:
        #     ./tarsnap.py --help
        subparser = subparsers.add_parser(modestr, help=desc, description=desc)

        # Add options specific to this mode.
        relevant = optlist.get_optargs_with_func_modes(
            lambda x, m=mode: m in x)
        for optarg in relevant:
            text += add_arg(subparser, "subparser", optarg)

        # Add global options (which will still be valid).
        text += add_global_options(subparser, "subparser", optlist)

    return parser, text


def write_argparse(filename_py, options, optlist, descs):
    """ Write a python file with argparse for tarsnap.

        WARNING: options starting with "mode-" are a workaround for
        argparse not being able to have subparsers which start with a hyphen.
        https://bugs.python.org/issue34046
    """
    _, parser_text = generate(options, optlist, descs)

    # Write argparse file.
    with open(filename_py, "wt", encoding="utf-8") as fileobj:
        fileobj.write(_PY_BEGIN)
        fileobj.write(parser_text)
        fileobj.write(_PY_END)
