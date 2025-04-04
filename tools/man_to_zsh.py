#!/usr/bin/env python3

""" Automatically write a zsh completion file. """

import os.path
import re

import shtab

import man_to_argparse

# The zsh generation in shtab can change from version to version, so this
# script is not guaranteed to work with a later (or earlier) shtab.
_HANDLES_SHTAB_VERSION = "1.5.8"

_ZSH_PREAMBLE = r"""# Custom header for the zsh completion file for tarsnap
#
# Place this file in one of the completion folders listed in $fpath (e.g.
# /usr/share/zsh/functions/Completion/Linux or $HOME/.zsh/functions).
# Then either restart zsh, or: autoload -Uz _tarsnap; compdef _tarsnap tarsnap

# Set the following variable to the path of a file containing the output from
# "tarsnap --list-archives", i.e. one archive name per line.  If left blank
# then archive names will not be completed.
local archive_list_file=

if [ -n "${archive_list_file}" ]; then
    archive_list=( ${(uf)"$(< "${archive_list_file}")"} )
else
    archive_list=
fi

"""

# -f has a different meaning when it's in --list-archives
SPECIAL_CASE_LIST_ARCHIVES_F = '  "-f[specify hash of archive name' \
    ' to operate on (requires --hashes)]:tapehash"'

_AUTO_GENERATED = "# AUTOMATICALLY GENERATED by `shtab`"
_AUTO_GENERATED_US = "%s, then modified by %s" % (_AUTO_GENERATED,
                                                  os.path.basename(__file__))


def add_argtypes(zsh_output):
    """ Add special zsh completion rules for some arguments. """
    outlines = []
    for line in zsh_output.split("\n"):
        if ':"' in line:
            # Special handling for shtab infrastructure in output
            if "_shtab_" in line:
                outlines.append(line)
                continue
            # Special handling for "method:arg"
            if r"method\:arg" in line:
                outlines.append(line)
                continue

            arg = line.split(":")[-2]
            argtype = man_to_argparse.get_argtypestr(arg)

            if argtype == "directory":
                line = line[:-1] + "{_files -/}\""
            elif argtype == "filename":
                line = line[:-1] + "{_files}\""
            elif argtype == "archive-name":
                line = line[:-1] + "(${archive_list}):\""

        outlines.append(line)

    zsh_output = "\n".join(outlines)
    return zsh_output


def restore_metavars(zsh_output, optlist):
    """ Replace the argument strings in zsh_output.

        shtab uses the option name as the argument string, instead of
        metavar (which is what we'd prefer).
    """
    get_option = re.compile(r"\"(.*)\[")
    get_arg = re.compile(r":(.*):\"")

    outlines = []
    for line in zsh_output.split("\n"):
        # Does the line contain an argument?
        if ':"' in line:
            # Special handling for shtab infrastructure in output
            if "_shtab_" in line:
                outlines.append(line)
                continue
            # Find the correct arg to use
            opt = get_option.findall(line)[0]
            optarg = optlist.get_optarg(opt)
            # We need to escape "method:arg", but there's no harm in calling
            # this function on every arg in case something else comes up in
            # the future.
            arg = shtab.escape_zsh(optarg.arg)

            # Replace it in the line
            line = re.sub(get_arg, ":%s:\"" % arg, line)
        outlines.append(line)

    zsh_output = "\n".join(outlines)

    return zsh_output


def special_case_list_archives_f(zsh_output):
    outlines = []

    section = ""
    for line in zsh_output.split("\n"):
        if line.startswith("_shtab_tarsnap__"):
            # Get the remainder of that line, other than the last 2 chars
            section = line[len("_shtab_tarsnap__"):-2]
        elif section != "list_archives_options":
            pass
        elif not line.startswith("  \"-f[specify name"):
            pass
        else:
            # Modify output of -f for this special case.
            line = SPECIAL_CASE_LIST_ARCHIVES_F

        outlines.append(line)

    zsh_output = "\n".join(outlines)

    return zsh_output

def write_zsh(filename_zsh, options, optlist, descs):
    """ Write the options into the zsh completion file.  """
    # Sanity-check shtab version
    if shtab.__version__ != _HANDLES_SHTAB_VERSION:
        print("ERROR: script designed for shtab %s; found %s instead" % (
              _HANDLES_SHTAB_VERSION, shtab.__version__))
        exit(1)
    # Get a zsh completion file.
    parser_obj, _ = man_to_argparse.generate(options, optlist, descs)
    zsh_output = shtab.complete(parser_obj, shell="zsh")

    # Remind readers that we've modified the completion file.
    zsh_output = zsh_output.replace(_AUTO_GENERATED, _AUTO_GENERATED_US)

    # Remove the false "mode" strings.
    zsh_output = zsh_output.replace("_mode", "").replace("mode", "")

    # Use argparse's 'metavar' for argument strings, instead of the 'option'.
    zsh_output = restore_metavars(zsh_output, optlist)

    # Add special argument completion rules (e.g,. "is a file", "is a dir").
    zsh_output = add_argtypes(zsh_output)

    # Special-case --list-archives -f TAPEHASH
    zsh_output = special_case_list_archives_f(zsh_output)

    # Add our custom preamble.
    index = zsh_output.find("# AUTO")
    zsh_output = "%s%s%s" % (zsh_output[:index], _ZSH_PREAMBLE,
                             zsh_output[index:])

    # Write final completion file
    with open(filename_zsh, "wt", encoding="utf-8") as fileobj:
        fileobj.write(zsh_output)
        fileobj.write("\n")
