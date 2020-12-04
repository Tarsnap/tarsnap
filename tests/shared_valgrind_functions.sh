#!/bin/sh

set -o noclobber -o nounset

### Design
#
# This file contains functions related to checking with valgrind.  The POSIX sh
# language doesn't allow us to specify a "public API", but if we could, it
# would be:
# - valgrind_init():
#   Clear previous valgrind output, and prepare for running valgrind tests
#   (if applicable).
# - valgrind_setup_cmd():
#   Set up the valgrind command if $USE_VALGRIND is greater than or equal to
#   ${valgrind_min}.
# - valgrind_check_basenames(exitfile):
#   Check for any memory leaks recorded in valgrind logfiles associated with a
#   test exitfile.  Return the filename if there's a leak; otherwise return an
#   empty string.

# A non-zero value unlikely to be used as an exit code by the programs being
# tested.
valgrind_exit_code=108
