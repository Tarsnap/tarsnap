""" Shared utilities for Tarsnap style-checking scripts. """


def is_libarchive(filename):
    """ Does this file come from libarchive?

        Args:
            filename (string): the filename to check."""
    if "libarchive/" in filename:
        return True
    if "tar/tree.h" in filename:
        return True
    if "tar/bsdtar_" in filename:
        return True
    return False
