#!/usr/bin/env python3

""" Parse a man page in mdoc format, and optionally check that all
    options are included in a completion file.
"""

import argparse
import functools


# Check that it's sorted (ignoring hyphens, and mostly alphabetical).
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


def check_sorted(optargs):
    """ Check that the optargs list of pairs is sorted. """
    optlist = [opt for opt, arg in optargs]
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


def get_sections_options(filename_manpage):
    """ Parse the man-page to get options from each section. """
    sections = {}

    with open(filename_manpage) as filep:
        lines = filep.readlines()

    for line in lines:
        # Do we have a new section?
        if line.startswith(".Sh "):
            section = line.split()[1].lower()
            sections[section] = []
            continue

        if line.startswith(".It Fl"):
            opt, arg = parse_opt_arg(line)

            # Special case for: -q (--fast-read)
            if opt == "-q":
                # Insert --fast-read before --force-resources
                index = sections[section].index(("--force-resources", None))
                sections[section].insert(index, ("--fast-read", None))
                # There's no arg for -q
                arg = None

            # Record the value
            sections[section].append((opt, arg))

    return sections


def get_options_with_arg(optargs, containslist):
    """ Get options which have an argument which is is containslist."""
    found = []
    for optarg in optargs:
        if optarg[1] in containslist:
            found.append(optarg[0])
    return found


def get_options(filename_manpage):
    """ Get the options from the man page. """
    sections = get_sections_options(filename_manpage)

    # The OPTIONS section should already be sorted, but the DESCRIPTION
    # section should not.
    check_sorted(sections["options"])

    options = {}

    # Options which require an argument.
    options["filenameargs"] = get_options_with_arg(sections["options"],
                                                   ["filename", "key-file"])
    options["dirargs"] = get_options_with_arg(sections["options"],
                                              ["directory", "cache-dir"])
    options["otherargs"] = [opt for opt, arg in sections["options"]
                            if arg is not None and
                            opt not in options["filenameargs"] and
                            opt not in options["dirargs"]]

    # Some short options (such as -x) are only mentioned in the DESCRIPTION.
    shortargs_description = [opt for opt, arg in sections["description"]
                             if len(opt) == 2]
    shortargs_options = [opt for opt, arg in sections["options"]
                         if len(opt) == 2]
    options["shortargs"] = shortargs_description + [
        opt for opt in shortargs_options
        if opt not in shortargs_description]

    options["longargs"] = [opt for opt, arg in sections["options"]
                           if len(opt) > 2]
    options["longargs"] += [opt for opt, arg in sections["description"]
                            if opt not in options["longargs"] and
                            len(opt) > 2]

    # Sort longargs, but not shortargs.
    options["longargs"] = sorted(options["longargs"],
                                 key=functools.cmp_to_key(sort_tarsnap_opts))

    return options


def check_options_in_file(options, filename):
    """ Check that all the options are in the file. """
    with open(filename) as filep:
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
    parser.add_argument("-c", "--check-file",
                        help="check that all options are in the file")
    args = parser.parse_args()

    # Sanity check.
    if "mdoc" not in args.filename_manpage:
        raise Exception("man-page must be in mdoc format")

    return args


def main(args):
    """ Parse the man page and edit the bash completion file. """
    options = get_options(args.filename_manpage)

    if args.check_file:
        check_options_in_file(options, args.check_file)


if __name__ == "__main__":
    main(parse_cmdline())
