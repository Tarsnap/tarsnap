#!/usr/bin/env python3

""" Parse a man page in mdoc format, and optionally check that all
    options are included in a completion file or update a bash completion file.
"""

import argparse
import dataclasses
import functools

import man_to_completion


@dataclasses.dataclass
class OptArg:
    """ An option, its argument (if applicable), and the modes for which it is
        valid.
    """
    opt: str
    arg: str
    modes: str = ""


class OptList(list):
    """ List of options, plus a few convenience classes. """
    def append_optarg(self, opt, arg):
        """ Append object to the end of the list. """
        super().append(OptArg(opt, arg))

    def insert_optarg(self, index, opt, arg):
        """ Insert object before index. """
        super().insert(index, OptArg(opt, arg))

    def append(self, value):
        raise Exception("Not supported; use append_optarg")

    def insert(self, index, value):
        raise Exception("Not supported; use insert_optarg")

    def get_opts(self):
        """ Return a list of every --opt. """
        return [optarg.opt for optarg in self]

    def get_opts_with_func_opt(self, func):
        """ Return a list of every --opt which satisfies func(opt). """
        return [optarg.opt for optarg in self if func(optarg.opt)]

    def get_opts_with_func_arg(self, func):
        """ Return a list of every --opt which satisfies func(arg). """
        return [optarg.opt for optarg in self if func(optarg.arg)]

    def index_of_opt(self, opt):
        """ Return the index of opt, or None. """
        for i, optarg in enumerate(self):
            if optarg.opt == opt:
                return i
        return None

    def set_only_modes(self, modes):
        """ Set the previous option to only refer to the given modes. """
        self[-1].modes = modes

    def get_opts_no_leading(self):
        """ Return a list of every --opt, without any leading hyphens. """
        opts = []
        for opt in self.get_opts():
            if opt.startswith("--"):
                opt = opt[2:]
            elif opt.startswith("-"):
                opt = opt[1:]
            opts.append(opt)
        return opts


def sort_tarsnap_opts(two, one):
    """ Sort options in the same way as the tarsnap man page.

        Sort order is:
        - Alphabetical, ignoring case other than as a tie-breaker
          (i.e. this order is correct: --force-resources -H -h).
        - Special exception: --newer-than comes before
          --newer-mtime-than.

        Note about the argument names: when python sorts a list with
        functools.cmp_to_key, it passes pairs as (x_{n+1}, x_{n}).
    """

    # The man-page makes more sense if --newer-than comes first.
    if one == "--newer-than" and two == "--newer-mtime-than":
        return 0

    # Sort args in lowercase, without hyphens.
    a = one.strip("-").lower()
    b = two.strip("-").lower()

    # Sort (ignoring capitalization).
    if a < b:
        return 1
    if a < b:
        return -1

    # It's a tie (such as -H, h), so now use capilization.
    if one < two:
        return 1
    if one > two:
        return -1

    raise Exception("Something weird happened: %s" % one)


def check_sorted(optlist):
    """ Check that the optlist is sorted. """
    optlist = optlist.get_opts()
    optlist_sorted = sorted(optlist,
                            key=functools.cmp_to_key(sort_tarsnap_opts))

    # If not sorted, print debug info.
    if optlist != optlist_sorted:
        print("Some options not sorted correctly!")
        for opt in optlist:
            print("\t%s " % opt)
        for a, b in zip(optlist, optlist_sorted):
            if a != b:
                print("\nProblem with: %s %s" % (a, b))
                exit(1)


def parse_opt_arg(line):
    """ Parse a line to produce the option and argument. """
    sl = line.split()[2:]
    # Get the option.
    opt = "-%s" % sl[0]
    # Get the argument (if applicable).
    if len(sl) > 1:
        # If we have an arg, assume it has enough parts.
        arg = sl[2]
    else:
        arg = None
    return opt, arg


def parse_modes_only(line):
    """ Parse a "([x, y, and z] mode(s) only)" line. """
    # Bail if the option don't specify a limited set of modes.
    if not line.startswith("("):
        return None
    if line.startswith("(use with"):
        return None
    if line == "(all modes)\n":
        return None

    # Check that the "mode(s) only" fits onto one line.
    if not line.endswith("only)\n"):
        print("man error: %s" % (line))
        exit(1)

    # Strip the initial '(' and the ending.
    for ending in (" mode only)\n", " modes only)\n"):
        if ending in line:
            end_chars = len(ending)
    actual = line[1:-end_chars].replace("and ", "").replace(",", "")

    # Return the modes.
    return actual.split(" ")


def get_sections_options(filename_manpage):
    """ Parse the man-page to get options from each section. """
    sections = {}
    getmodes = False

    with open(filename_manpage, encoding="utf-8") as filep:
        lines = filep.readlines()

    for line in lines:
        # Do we have a new section?
        if line.startswith(".Sh "):
            section = line.split()[1].lower()
            sections[section] = OptList()
            continue

        if line.startswith(".It Fl"):
            opt, arg = parse_opt_arg(line)

            # Special case for: -q (--fast-read)
            if opt == "-q":
                # Insert --fast-read before --force-resources
                index = sections[section].index_of_opt("--force-resources")
                # If we failed to find it, something went wrong
                assert index is not None
                sections[section].insert_optarg(index, "--fast-read", None)

                # There's no arg for -q
                arg = None

            # Record the value
            sections[section].append_optarg(opt, arg)
            getmodes = True
            continue

        if getmodes:
            getmodes = False
            modes = parse_modes_only(line)
            if modes:
                sections[section].set_only_modes(modes)

    return sections


def get_options(filename_manpage):
    """ Get the options from the man page. """
    sections = get_sections_options(filename_manpage)

    # The OPTIONS section should already be sorted, but the DESCRIPTION
    # section should not.
    check_sorted(sections["options"])

    # Check that there's no unknown modes.
    modes = sections["description"].get_opts_no_leading()
    for optarg in sections["options"]:
        for mode in optarg.modes:
            if mode not in modes:
                print("Unrecognized mode: %s" % (optarg))
                exit(1)

    options = {}

    # Options which require an argument.
    options["filenameargs"] = sections["options"].get_opts_with_func_arg(
        lambda x: x in ["filename", "key-file"])
    options["dirargs"] = sections["options"].get_opts_with_func_arg(
        lambda x: x in ["directory", "cache-dir"])
    opts_which_have_arg = sections["options"].get_opts_with_func_arg(
        lambda x: x)
    options["otherargs"] = [opt for opt in opts_which_have_arg if
                            opt not in options["filenameargs"] and
                            opt not in options["dirargs"]]

    # Some short options (such as -x) are only mentioned in the DESCRIPTION.
    shortargs_description = sections["description"].get_opts_with_func_opt(
        lambda x: len(x) == 2)
    shortargs_options = sections["options"].get_opts_with_func_opt(
        lambda x: len(x) == 2)
    options["shortargs"] = shortargs_description + [
        opt for opt in shortargs_options
        if opt not in shortargs_description]

    options["longargs"] = sections["options"].get_opts_with_func_opt(
        lambda x: len(x) > 2)
    opts_desc = sections["description"].get_opts_with_func_opt(
        lambda x: len(x) > 2)
    options["longargs"] += [opt for opt in opts_desc
                            if opt not in options["longargs"]]

    # Sort longargs, but not shortargs.
    options["longargs"] = sorted(options["longargs"],
                                 key=functools.cmp_to_key(sort_tarsnap_opts))

    return options


def check_options_in_file(options, filename):
    """ Check that all the options are in the file. """
    with open(filename, encoding="utf-8") as filep:
        data = filep.read()

    # Check that every option is in the file.
    err = 0
    for opt in options["longargs"] + options["shortargs"]:
        if opt not in data:
            print("Missing option: %s" % opt)
            err += 1

    # Bail if anything is missing.
    if err > 0:
        exit(1)


def parse_cmdline():
    """ Parse the command line. """
    parser = argparse.ArgumentParser(description="Check a man page.")
    parser.add_argument("filename_manpage",
                        help="man-page in mdoc format")
    parser.add_argument("-c", "--check-file", metavar="filename",
                        help="check that all options are in the file")
    parser.add_argument("--update-bash", metavar="filename",
                        help="update the bash completion file")
    args = parser.parse_args()

    # Sanity check.
    if "mdoc" not in args.filename_manpage:
        raise Exception("man-page must be in mdoc format")
    if args.check_file and args.update_bash:
        assert args.check_file != args.update_bash

    return args


def main(args):
    """ Parse the man page and edit the bash completion file. """
    options = get_options(args.filename_manpage)

    if args.check_file:
        check_options_in_file(options, args.check_file)
    if args.update_bash:
        man_to_completion.bash_completion_update(args.update_bash, options)


if __name__ == "__main__":
    main(parse_cmdline())
