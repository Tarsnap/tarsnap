#!/usr/bin/env python3
""" Checks that there is a newline in function definitions. """

import sys

import style_utils


def handle_file(filename):
    """ Parses a .c file and checks that functions are defined with a newline.

        Args:
            filename (string): the filename to examine."""
    func_text = ""
    with open(filename) as filep:
        for i, line in enumerate(filep):
            if line[0] == "{":
                func_text += line
                func_text_search = func_text.replace("static ", "")
                if " " in func_text_search.split("(")[0]:
                    print("--------- WARNING: possible style violation")
                    print("\t%s" % filename)
                    print("\tline %i" % i)
                    print(func_text)
                func_text = ""
            if line[0] == "\t" or line[0] == " " or line[0] == "_":
                continue
            if line[0] == "#" or line[0:2] == "/*" or line[0:3] == " */":
                func_text = ""
                continue
            if ";" in line:
                func_text = ""
                continue
            if "(" not in line:
                func_text = ""
                continue
            func_text += line


def main(filenames):
    """ Checks files for a newline in function definitions.

        Args:
            filenames (list): the filenames (as strings) to check."""
    for filename in filenames:
        # don't apply to libarchive right now
        if style_utils.is_libarchive(filename):
            continue
        handle_file(filename)
    print("---------")
    print("This script is an aid, not a definitive guide.  It can create")
    print("false positives and false negatives.  Use your own judgement.")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Manual usage: specify the filename of a .c file")
        print("Automatic usage:")
        print("  find . -name \"*.c\" | "
              "xargs misc/checks/check_c_function_newline.py")
        exit(1)
    main(sys.argv[1:])
