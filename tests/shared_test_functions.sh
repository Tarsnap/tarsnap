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
# The main function is scenario_runner(scenario_filename), which
# takes a scenario file as the argument, and runs a
#     scenario_cmd()
# function which was defined in that file.
#
### Variables
#
# Wherever possible, this suite uses local variables and
# explicitly-passed arguments, with the following exceptions:
# - s_basename: this is the basename for the scenario's temporary
#       and log files.
# - s_val_basename: this is the basename for the scenario's
#       valgrind log files.
# - s_count: this is the count of the scenario's checks (so that
#       each check can have distinct files).
# - s_retval: this is the overall exit code of the scenario.
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


## prepare_directory():
# Delete the previous test output directory, and create a new one.
prepare_directory() {
	if [ -d "${out}" ]; then
		rm -rf "${out}"
	fi
	mkdir "${out}"

	# We don't want to back up this directory.
	[ "$(uname)" = "FreeBSD" ] && chflags nodump "${out}"
}

## find_system (cmd, args):
# Look for ${cmd} in the $PATH, and ensure that it supports ${args}.
find_system() {
	cmd=$1
	cmd_with_args="$1 ${2:-}"

	# Sanity check.
	if [ "$#" -gt "2" ]; then
		printf "Programmer error: find_system: too many args\n" 1>&2
		exit 1
	fi

	# Look for ${cmd}; the "|| true" and -} make this work with set -e.
	system_binary=$(command -v "${cmd}") || true
	if [ -z "${system_binary-}" ]; then
		system_binary=""
		printf "System %s not found.\n" "${cmd}" 1>&2
	# If the command exists, check it ensures the ${args}.
	elif ${cmd_with_args} 2>&1 >/dev/null |	\
	    grep -qE "(invalid|illegal) option"; then
		system_binary=""
		printf "Cannot use system %s; does not" "${cmd}" 1>&2
		printf " support necessary arguments.\n" 1>&2
	fi
	echo "${system_binary}"
}

## has_pid (cmd):
# Look for ${cmd} in ps; return 0 if ${cmd} exists.
has_pid() {
	cmd=$1
	pid=$(ps -Aopid,args | grep -F "${cmd}" | grep -v "grep") || true
	if [ -n "${pid}" ]; then
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

## setup_check_variables (description, check_prev=1):
# Set up the "check" variables ${c_exitfile} and ${c_valgrind_cmd}, the
# latter depending on the previously-defined ${c_valgrind_min}.
# Advances the number of checks ${s_count} so that the next call to this
# function will set up new filenames.  Write ${description} into a
# file.  If ${check_prev} is non-zero, check that the previous
# ${c_exitfile} exists.
setup_check_variables() {
	description=$1
	check_prev=${2:-1}

	# Should we check for the previous exitfile?
	if [ "${c_exitfile}" != "${NO_EXITFILE}" ] &&			\
	    [ "${check_prev}" -gt 0 ] ; then
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
	count_str=$(printf "%02d" "${s_count}")
	c_exitfile="${s_basename}-${count_str}.exit"

	# Write the "description" file.
	printf "%s\n" "${description}" >				\
		"${s_basename}-${count_str}.desc"

	# Set up the valgrind command (or an empty string).
	c_valgrind_cmd="$(valgrind_setup_cmd)"

	# Advances the number of checks.
	s_count=$((s_count + 1))
}

## expected_exitcode (expected, exitcode):
# If ${exitcode} matches the ${expected} value, return 0.  If the exitcode is
# ${valgrind_exit_code}, return that.  Otherwise, return 1 to indicate
# failure.
expected_exitcode() {
	expected=$1
	exitcode=$2

	if [ "${exitcode}" -eq "${expected}" ]; then
		echo "0"
	elif [ "${exitcode}" -eq "${valgrind_exit_code}" ]; then
		echo "${valgrind_exit_code}"
	else
		echo "1"
	fi
}

## notify_success_or_fail (log_basename, val_log_basename):
# Examine all "exit code" files beginning with ${log_basename} and
# print "SUCCESS!", "FAILED!", "SKIP!", or "PARTIAL SUCCESS / SKIP!"
# as appropriate.  Check any valgrind log files associated with the
# test and print "FAILED!" if appropriate, along with the valgrind
# logfile.  If the test failed and ${VERBOSE} is non-zero, print
# the description to stderr.
notify_success_or_fail() {
	log_basename=$1
	val_log_basename=$2

	# Bail if there's no exitfiles.
	exitfiles=$(ls "${log_basename}"-*.exit) || true
	if [ -z "$exitfiles" ]; then
		echo "FAILED" 1>&2
		s_retval=1
		return
	fi

	# Count results
	total_exitfiles=0
	skip_exitfiles=0

	# Check each exitfile.
	for exitfile in $(echo "$exitfiles" | sort); do
		ret=$(cat "${exitfile}")
		total_exitfiles=$(( total_exitfiles + 1 ))
		if [ "${ret}" -lt 0 ]; then
			skip_exitfiles=$(( skip_exitfiles + 1 ))
		fi

		# Check for test failure.
		descfile=$(echo "${exitfile}" | sed 's/\.exit/\.desc/g')
		if [ "${ret}" -gt 0 ]; then
			echo "FAILED!" 1>&2
			if [ "${VERBOSE}" -ne 0 ]; then
				printf "File %s contains" "${exitfile}" 1>&2
				printf " exit code %s.\n" "${ret}" 1>&2
				printf "Test description: " 1>&2
				cat "${descfile}" 1>&2
			fi
			s_retval=${ret}
			return
		else
			# If there's no failure, delete the files.
			rm "${exitfile}"
			rm "${descfile}"
		fi

		# Check valgrind logfile(s).
		val_failed="$(valgrind_check_basenames "${exitfile}")"
		if [ -n "${val_failed}" ]; then
			echo "FAILED!" 1>&2
			s_retval="${valgrind_exit_code}"
			cat "${val_failed}" 1>&2
			return
		fi
	done

	# Notify about skip or success.
	if [ ${skip_exitfiles} -gt 0 ]; then
		if [ ${skip_exitfiles} -eq ${total_exitfiles} ]; then
			echo "SKIP!" 1>&2
		else
			echo "PARTIAL SUCCESS / SKIP!" 1>&2
		fi
	else
		echo "SUCCESS!" 1>&2
	fi
}

## scenario_runner (scenario_filename):
# Run a test scenario from ${scenario_filename}.
scenario_runner() {
	scenario_filename=$1
	basename=$(basename "${scenario_filename}" .sh)
	printf "  %s... " "${basename}" 1>&2

	# Initialize "scenario" and "check" variables.
	s_basename=${out}/${basename}
	s_val_basename=${out_valgrind}/${basename}
	s_count=0
	c_exitfile="${NO_EXITFILE}"
	c_valgrind_min=9
	c_valgrind_cmd=""

	# Load scenario_cmd() from the scenario file.
	unset scenario_cmd
	. "${scenario_filename}"
	if ! command -v scenario_cmd 1>/dev/null ; then
		printf "ERROR: scenario_cmd() is not defined in\n" 1>&2
		printf "  %s\n" "${scenario_filename}" 1>&2
		exit 1
	fi

	# Run the scenario command.
	scenario_cmd

	# Print PASS or FAIL, and return result.
	s_retval=0
	notify_success_or_fail "${s_basename}" "${s_val_basename}"

	return "${s_retval}"
}

## run_scenarios (scenario_filenames):
# Run all scenarios matching ${scenario_filenames}.
run_scenarios() {
	# Get the test number(s) to run.
	if [ "${N:-0}" -gt "0" ]; then
		test_scenarios="$(printf "${scriptdir}/%02d-*.sh" "${N}")"
	else
		test_scenarios="${scriptdir}/??-*.sh"
	fi

	# Clean up any previous directory, and create a new one.
	prepare_directory

	# Clean up any previous valgrind directory, and prepare for new
	# valgrind tests (if applicable).
	valgrind_init

	printf -- "Running tests\n" 1>&2
	printf -- "-------------\n" 1>&2
	for scenario in ${test_scenarios}; do
		# We can't call this function with $( ... ) because we
		# want to allow it to echo values to stdout.
		scenario_runner "${scenario}"
		retval=$?
		if [ ${retval} -gt 0 ]; then
			exit ${retval}
		fi
	done
}
