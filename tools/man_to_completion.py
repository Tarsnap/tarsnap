#!/usr/bin/env python3

""" Automatically rewrite options in a bash completion file. """

import textwrap


def _wrap(text, width, initial_indent, subsequent_indent, separator):
    """ Wrap the given text.

        This is a workaround for textwrap.fill(), which appears to
        count a tab as a single space.
    """
    # Sanity check if this function will work.
    assert "tttttttt" not in text
    assert "~" not in text

    # Replace tabs with 8 "t", so that they'll count as 8 chars.
    initial_indent_t = initial_indent.replace("\t", "tttttttt")
    subsequent_indent_t = subsequent_indent.replace("\t", "tttttttt")

    # Replace spaces in indent with ~.
    subsequent_indent_tz = subsequent_indent_t.replace(" ", "~")

    # Wrap text.
    text_wrapped = textwrap.fill(text, width=width,
                                 initial_indent=initial_indent_t,
                                 subsequent_indent=subsequent_indent_tz,
                                 expand_tabs=False, break_long_words=False,
                                 break_on_hyphens=False)

    # Apply separator.
    text_wrapped_sep = text_wrapped.replace(" ", separator)

    # Undo special workarounds.
    text_wrapped_sep_tabs = text_wrapped_sep.replace("tttttttt", "\t")
    text_wrapped_sep_tabs_spaces = text_wrapped_sep_tabs.replace("~", " ")

    return text_wrapped_sep_tabs_spaces


def _bash_opts_multiline(bash_name, sep, options):
    """ Format the list of options as a multi-line bash variable. """

    # Initial indent text.
    init_ind = "\t%s=\"" % (bash_name)

    # Calculate how many initial spaces we need on subsequent lines.
    init_spaces = len(bash_name) + 8 + 2 - 16 - 1
    if sep != " ":
        init_spaces += 1
    subs_ind = "\t\t%s%s" % (' ' * init_spaces, sep)

    # Wrap text.
    width = 72
    options_string_unwrapped = "%s" % (" ".join(options))
    options_string = _wrap(options_string_unwrapped, width,
                           init_ind, subs_ind, sep)

    # Replace end-of-line characters.
    lines = options_string.split("\n")
    if sep == " ":
        options_string = "\n".join(["%s \\" % (line) for line in lines])
        # Remove the final 'sep',
        options_string = options_string[:-2]
    else:
        options_string = "\n".join(["%s%s" % (line, sep) for line in lines])
        # Remove the final 'sep'.
        options_string = options_string[:-1]

    # Append the final double-quote.
    options_string += "\""
    return options_string + "\n"


def bash_completion_update(filename_bash, options):
    """ Write the options into the bash completion file.  """

    # Read file.
    with open(filename_bash, encoding="utf-8") as bash:
        lines = bash.readlines()

    newlines = []
    skipping = False
    for line in lines:
        if skipping:
            if line == "\n":
                skipping = False
            else:
                continue

        # Handle options which are used in a "case" statement,
        # which therefore require a "|" separator.
        if "\twfilearg=" in line:
            skipping = True
            replacement = _bash_opts_multiline("wfilearg", "|",
                                               options["filenameargs"])
            newlines.append(replacement)
        elif "\twpatharg=" in line:
            skipping = True
            replacement = _bash_opts_multiline("wpatharg", "|",
                                               options["dirargs"])
            newlines.append(replacement)
        elif "\twotherarg=" in line:
            skipping = True
            replacement = _bash_opts_multiline("wotherarg", "|",
                                               options["otherargs"])
            newlines.append(replacement)

        # Handle options used in "compgen", which therefore
        # require a " " separator.
        elif "\tlongopts=" in line:
            skipping = True
            replacement = _bash_opts_multiline("longopts", " ",
                                               options["longargs"])
            newlines.append(replacement)
        elif "\tshortopts=" in line:
            skipping = True
            replacement = _bash_opts_multiline("shortopts", " ",
                                               options["shortargs"])
            newlines.append(replacement)

        # Handle other lines.
        else:
            newlines.append(line)

    # Write file.
    with open(filename_bash, "wt", encoding="utf-8") as bash:
        newbash = "".join(newlines)
        bash.write(newbash)
