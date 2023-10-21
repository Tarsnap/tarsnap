#!/bin/sh

### Definitions
#
# This test suite uses the following terminology:
# - scenario: a series of commands to test.  Each must be in a
#       separate file, and must be completely self-contained
#       (other than the variables listed below).
# - check: a series of commands that produces an exit code which
#       the test suite should check.  A scenario may contain any
#       number of checks.
#
### Design
#
# The main function is _scenario_runner(scenario_filename), which
# takes a scenario file as the argument, and runs a
#     scenario_cmd()
# function which was defined in that file.
#
# Functions which are available to other scripts as a "public API" are:
# - find_system(cmd, args):
#   Look for ${cmd} in the ${PATH}, and ensure that it supports ${args}.
# - has_pid(cmd):
#   Look for a ${cmd} in $(ps).
# - wait_while(func):
#   Wait until ${func} returns non-zero.
# - setup_check(description, check_prev):
#   Set up the below variables.
# - expected_exitcode(expected, actual):
#   Check if ${expected} matches ${actual}.
# - run_scenarios():
#   Run scenarios in the test directory.
#
# We adopt the convention of "private" function names beginning with an _.
#
### Variables
#
# Wherever possible, this suite uses local variables and
# explicitly-passed arguments, with the following exceptions:
# - s_basename: this is the basename for the scenario's temporary
#       and log files.
# - s_val_basename: this is the basename for the scenario's
#       valgrind log files.
# - s_retval: this is the overall exit code of the scenario.
# - c_count_next: this is the count of the scenario's checks (so that
#       each check can have distinct files).
# - c_count_str: the previous value of ${c_count_next}, expressed as a
#       two-digit string.  In other words, when we're working on the Nth
#       check, ${c_count_next} is N, while ${c_count_str} is N-1.
# - c_exitfile: this contains the exit code of each check.
# - c_valgrind_min: this is the minimum value of USE_VALGRIND
#       which will enable valgrind checking for this check.
# - c_valgrind_cmd: this is the valgrind command (including
#       appropriate log file) if necessary, or is "" otherwise.

set -o noclobber -o nounset

# Keep the user-specified "print info about test failures", or initialize to 0
# (don't print extra info).
VERBOSE=${VERBOSE:-0}

# Keep the user-specified ${USE_VALGRIND}, or initialize to 0 (don't do memory
# tests).  If ${USE_VALGRIND_NO_REGEN} is non-zero, re-use the previous
# suppressions files instead of generating new ones.
USE_VALGRIND=${USE_VALGRIND:-0}
USE_VALGRIND_NO_REGEN=${USE_VALGRIND_NO_REGEN:-0}

# Load valgrind-related functions.  These functions will bail on a per-check
# basis if the ${USE_VALGRIND} value does not indicate that we should run a
# valgrind for that check.
. "${scriptdir}/shared_valgrind_functions.sh"

# Set ${bindir} to $1 if given, else use "." for in-tree builds.
bindir=$(CDPATH='' cd -- "$(dirname -- "${1-.}")" && pwd -P)

# Default value (should be set by tests).
NO_EXITFILE=/dev/null

# A non-zero value unlikely to be used as an exit code by the programs being
# tested.
valgrind_exit_code=108


## _prepdir():
# Delete the previous test output directory, and create a new one.
_prepdir() {
	if [ -d "${out}" ]; then
		rm -rf "${out}"
	fi
	mkdir "${out}"

	# We don't want to back up this directory.
	[ "$(uname)" = "FreeBSD" ] && chflags nodump "${out}"
}

## find_system (cmd, args):
# Look for ${cmd} in the ${PATH}, and ensure that it supports ${args}.
find_system() {
	_find_system_cmd=$1
	_find_system_cmd_with_args="$1 ${2:-}"

	# Sanity check.
	if [ "$#" -gt "2" ]; then
		printf "Programmer error: find_system: too many args\n" 1>&2
		exit 1
	fi

	# Look for ${cmd}; the "|| true" and -} make this work with set -e.
	_find_system_binary=$(command -v "${_find_system_cmd}") || true
	if [ -z "${_find_system_binary-}" ]; then
		_find_system_binary=""
		printf "System %s not found.\n" "${_find_system_cmd}" 1>&2
	# If the command exists, check it ensures the ${args}.
	elif ${_find_system_cmd_with_args} 2>&1 >/dev/null |	\
	    grep -qE "(invalid|illegal) option"; then
		_find_system_binary=""
		printf "Cannot use system %s; does not"		\
		    "${_find_system_cmd}" 1>&2
		printf " support necessary arguments.\n" 1>&2
	fi
	echo "${_find_system_binary}"
}

## has_pid (cmd):
# Look for ${cmd} in ps; return 0 if ${cmd} exists.
has_pid() {
	_has_pid_cmd=$1
	_has_pid_pid=$(ps -Aopid,args | grep -F "${_has_pid_cmd}" |	\
	    grep -v "grep") || true
	if [ -n "${_has_pid_pid}" ]; then
		return 0
	fi
	return 1
}

## wait_while(func):
# Wait while ${func} returns 0.  If ${msleep} is defined, use that to wait
# 100ms; otherwise, wait in 1 second increments.
wait_while() {
	_wait_while_ms=0

	# Check for the ending condition
	while "$@"; do
		# Notify user (if desired)
		if [ "${VERBOSE}" -ne 0 ]; then
			printf "waited\t%ims\t%s\n"		\
			    "${_wait_while_ms}" "$*" 1>&2
		fi

		# Wait using the appropriate binary
		if [ -n "${msleep:-}" ];  then
			"${msleep}" 100
			_wait_while_ms=$((_wait_while_ms + 100))
		else
			sleep 1
			_wait_while_ms=$((_wait_while_ms + 1000))
		fi
	done

	# Success
	return 0
}

## setup_check (description, check_prev=1):
# Set up the "check" variables ${c_exitfile} and ${c_valgrind_cmd}, the
# latter depending on the previously-defined ${c_valgrind_min}.
# Advance the number of checks ${c_count_next} so that the next call to this
# function will set up new filenames.  Write ${description} into a
# file.  If ${check_prev} is non-zero, check that the previous
# ${c_exitfile} exists.
setup_check() {
	_setup_check_description=$1
	_setup_check_prev=${2:-1}

	# Should we check for the previous exitfile?
	if [ "${c_exitfile}" != "${NO_EXITFILE}" ] &&			\
	    [ "${_setup_check_prev}" -gt 0 ] ; then
		# Check for the file.
		if [ ! -f "${c_exitfile}" ] ; then
			# We should have written the result of the
			# previous test to this file.
			echo "PROGRAMMING FAILURE" 1>&2
			echo "We should already have ${c_exitfile}" 1>&2
			exit 1
		fi
	fi

	# Set up the "exit" file.
	c_count_str=$(printf "%02d" "${c_count_next}")
	c_exitfile="${s_basename}-${c_count_str}.exit"

	# Write the "description" file.
	printf "%s\n" "${_setup_check_description}" >			\
		"${s_basename}-${c_count_str}.desc"

	# Set up the valgrind command (or an empty string).
	c_valgrind_cmd="$(valgrind_setup)"

	# Advances the number of checks.
	c_count_next=$((c_count_next + 1))
}

## expected_exitcode (expected, exitcode):
# If ${exitcode} matches the ${expected} value, return 0.  If the exitcode is
# ${valgrind_exit_code}, return that.  Otherwise, return 1 to indicate
# failure.
expected_exitcode() {
	_expected_exitcode_expected=$1
	_expected_exitcode_exitcode=$2

	if [ "${_expected_exitcode_exitcode}" -eq		\
	    "${_expected_exitcode_expected}" ]; then
		echo "0"
	elif [ "${_expected_exitcode_exitcode}" -eq		\
	    "${valgrind_exit_code}" ]; then
		echo "${valgrind_exit_code}"
	else
		echo "1"
	fi
}

## _check (log_basename, val_log_basename):
# Examine all "exit code" files beginning with ${log_basename} and
# print "SUCCESS!", "FAILED!", "SKIP!", or "PARTIAL SUCCESS / SKIP!"
# as appropriate.  Check any valgrind log files associated with the
# test and print "FAILED!" if appropriate, along with the valgrind
# logfile.  If the test failed and ${VERBOSE} is non-zero, print
# the description to stderr.
_check() {
	_check_log_basename=$1
	_check_val_log_basename=$2

	# Bail if there's no exitfiles.
	_check_exitfiles=$(ls "${_check_log_basename}"-*.exit) || true
	if [ -z "${_check_exitfiles}" ]; then
		echo "FAILED" 1>&2
		s_retval=1
		return
	fi

	# Count results
	_check_total=0
	_check_skip=0

	# Check each exitfile.
	for _check_exitfile in $(echo "${_check_exitfiles}" | sort); do
		_check_ret=$(cat "${_check_exitfile}")
		_check_total=$(( _check_total + 1 ))
		if [ "${_check_ret}" -lt 0 ]; then
			_check_skip=$(( _check_skip + 1 ))
		fi

		# Check for test failure.
		_check_descfile=$(echo "${_check_exitfile}"		\
		    | sed 's/\.exit/\.desc/g')
		if [ "${_check_ret}" -gt 0 ]; then
			echo "FAILED!" 1>&2
			if [ "${VERBOSE}" -ne 0 ]; then
				printf "File %s contains exit code %s.\n" \
				    "${_check_exitfile}" "${_check_ret}" \
				    1>&2
				printf "Test description: " 1>&2
				cat "${_check_descfile}" 1>&2
			fi
			s_retval=${_check_ret}
			return
		else
			# If there's no failure, delete the files.
			rm "${_check_exitfile}"
			rm "${_check_descfile}"
		fi

		# Check valgrind logfile(s).
		_check_val_failed="$(valgrind_check "${_check_exitfile}")"
		if [ -n "${_check_val_failed}" ]; then
			echo "FAILED!" 1>&2
			s_retval="${valgrind_exit_code}"
			cat "${_check_val_failed}" 1>&2
			return
		fi
	done

	# Notify about skip or success.
	if [ "${_check_skip}" -gt 0 ]; then
		if [ "${_check_skip}" -eq "${_check_total}" ]; then
			echo "SKIP!" 1>&2
		else
			echo "PARTIAL SUCCESS / SKIP!" 1>&2
		fi
	else
		echo "SUCCESS!" 1>&2
	fi
}

## _scenario_runner (scenario_filename):
# Run a test scenario from ${scenario_filename}.
_scenario_runner() {
	_scenario_runner_filename=$1
	_scenario_runner_basename=$(basename "${_scenario_runner_filename}" .sh)
	printf "  %s... " "${_scenario_runner_basename}" 1>&2

	# Initialize "scenario" and "check" variables.
	s_basename=${out}/${_scenario_runner_basename}
	s_val_basename=${out_valgrind}/${_scenario_runner_basename}
	c_count_next=0
	c_exitfile="${NO_EXITFILE}"
	c_valgrind_min=9
	c_valgrind_cmd=""

	# Load scenario_cmd() from the scenario file.
	unset scenario_cmd
	. "${_scenario_runner_filename}"
	if ! command -v scenario_cmd 1>/dev/null ; then
		printf "ERROR: scenario_cmd() is not defined in\n" 1>&2
		printf "  %s\n" "${_scenario_runner_filename}" 1>&2
		exit 1
	fi

	# Run the scenario command.
	scenario_cmd

	# Print PASS or FAIL, and return result.
	s_retval=0
	_check "${s_basename}" "${s_val_basename}"

	return "${s_retval}"
}

## run_scenarios ():
# Run all scenarios in the test directory.  If the environment variable ${N}
# is specified, only run the scenario corresponding to that number.
run_scenarios() {
	# Get the test number(s) to run.
	if [ "${N:-0}" -gt "0" ]; then
		_run_scenarios_filenames="$(printf			\
		    "${scriptdir}/%02d-*.sh" "${N}")"
	else
		_run_scenarios_filenames="${scriptdir}/??-*.sh"
	fi

	# Clean up any previous directory, and create a new one.
	_prepdir

	# Clean up any previous valgrind directory, and prepare for new
	# valgrind tests (if applicable).
	valgrind_init

	printf -- "Running tests\n" 1>&2
	printf -- "-------------\n" 1>&2
	for _run_scenarios_filename in ${_run_scenarios_filenames}; do
		# We can't call this function with $( ... ) because we
		# want to allow it to echo values to stdout.
		_scenario_runner "${_run_scenarios_filename}"
		_run_scenarios_retval=$?
		if [ "${_run_scenarios_retval}" -gt 0 ]; then
			exit "${_run_scenarios_retval}"
		fi
	done
}
