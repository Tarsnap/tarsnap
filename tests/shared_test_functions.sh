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
# tests).
USE_VALGRIND=${USE_VALGRIND:-0}

# A non-zero value unlikely to be used as an exit code by the programs being
# tested.
valgrind_exit_code=108

# Set ${bindir} to $1 if given, else use "." for in-tree builds.
bindir=$(CDPATH='' cd -- "$(dirname -- "${1-.}")" && pwd -P)

# Default value (should be set by tests).
NO_EXITFILE=/dev/null


## prepare_directory():
# Delete the previous test output directory, and create a new one.
prepare_directory() {
	if [ -d "${out}" ]; then
		rm -rf ${out}
	fi
	mkdir ${out}
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
	system_binary=$(command -v ${cmd}) || true
	if [ -z "${system_binary-}" ]; then
		system_binary=""
		printf "System ${cmd} not found.\n" 1>&2
	# If the command exists, check it ensures the ${args}.
	elif ${cmd_with_args} 2>&1 >/dev/null |	\
	    grep -qE "(invalid|illegal) option"; then
		system_binary=""
		printf "Cannot use system ${cmd}; does not" 1>&2
		printf " support necessary arguments.\n" 1>&2
	fi
	echo "${system_binary}"
}

## valgrind_prepare_directory ():
# Clean up a previous valgrind directory, and prepare for new valgrind tests
# (if applicable).
valgrind_prepare_directory() {
	# Always delete any previous valgrind directory.
	if [ -d "${out_valgrind}" ]; then
		rm -rf ${out_valgrind}
	fi

	# Bail if we don't want valgrind at all.
	if [ "$USE_VALGRIND" -eq 0 ]; then
		return
	fi

	mkdir ${out_valgrind}
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

## wait_for_file (filename):
# Waits until ${filename} exists.
wait_for_file() {
	filename=$1
	while [ ! -e ${filename} ]; do
		if [ ${VERBOSE} -ne 0 ]; then
			echo "Waiting for ${filename}" 1>&2
		fi
		sleep 1
	done
}

## valgrind_check_optional ():
# Return a $USE_VALGRIND variable defined; if it was previously defined and
# was greater than 0, then check that valgrind is available in the $PATH.
valgrind_check_optional() {
	if [ "$USE_VALGRIND" -gt 0 ]; then
		# Look for valgrind in $PATH.
		if ! command -v valgrind >/dev/null 2>&1; then
			printf "valgrind not found\n" 1>&2
			exit 1
		fi

		# Check the version.
		version=$(valgrind --version | cut -d "-" -f 2)
		major=$(echo "${version}" | cut -d "." -f 1)
		minor=$(echo "${version}" | cut -d "." -f 2)
		if [ "${major}" -lt "3" ]; then
			printf "valgrind must be at least version 3.13\n" 1>&2
			exit 1;
		fi
		if [ "${major}" -eq "3" ] && [ "${minor}" -lt "13" ]; then
			printf "valgrind must be at least version 3.13\n" 1>&2
			exit 1;
		fi
	fi
}

## valgrind_ensure_suppression (potential_memleaks_binary):
# Run the ${potential_memleaks_binary} through valgrind, keeping
# track of any apparent memory leak in order to suppress reporting
# those leaks when testing other binaries.  Record how many file descriptors
# are open at exit in ${valgrind_fds}.
valgrind_ensure_suppression() {
	potential_memleaks_binary=$1

	# Quit if we're not using valgrind.
	if [ ! "$USE_VALGRIND" -gt 0 ]; then
		return
	fi;

	printf "Generating valgrind suppressions... " 1>&2
	valgrind_suppressions="${out_valgrind}/suppressions"
	valgrind_suppressions_log="${out_valgrind}/suppressions.pre"

	# Start off with an empty suppression file
	touch ${valgrind_suppressions}

	# Get list of tests and the number of open descriptors at a normal exit
	valgrind_suppressions_tests="${out_valgrind}/suppressions-names.txt"
	thislog="${out_valgrind}/fds.log"
	valgrind --track-fds=yes --log-file=${thislog}			\
	    ${potential_memleaks_binary} > "${valgrind_suppressions_tests}"
	valgrind_fds=$(grep "FILE DESCRIPTORS" "${thislog}" | awk '{print $4}')

	# Generate suppressions for each test
	while read testname; do
		this_valgrind_supp="${valgrind_suppressions_log}-${testname}"

		# Run valgrind on the binary, sending it a "\n" so that
		# a test which uses STDIN will not wait for user input.
		printf "\n" | (valgrind					\
		    --leak-check=full --show-leak-kinds=all		\
		    --gen-suppressions=all				\
		    --suppressions=${valgrind_suppressions}		\
		    --log-file=${this_valgrind_supp}			\
		    ${potential_memleaks_binary}			\
		    ${testname})					\
		    > /dev/null

		# Append name to suppressions file
		printf "# ${testname}\n" >> ${valgrind_suppressions}

		# Strip out useless parts from the log file, and allow the
		# suppressions to apply to other binaries by removing:
		# - references to the main() function,
		# - "pl_*" ("potential loss") functions,
		# - references to the binary itself.
		# Append to suppressions file.
		(grep -v "^==" ${this_valgrind_supp}			\
			| grep -v "   fun:pl_" -			\
			| grep -v "   fun:main" -			\
			| grep -v -E "   obj:.*/potential-memleaks" -	\
			>> ${valgrind_suppressions} ) || true
	done < "${valgrind_suppressions_tests}"

	# Clean up
	rm -f ${valgrind_suppressions_log}
	printf "done.\n" 1>&2
}

## valgrind_setup_cmd ():
# Set up the valgrind command if $USE_VALGRIND is greater than or equal to
# ${valgrind_min}.
valgrind_setup_cmd() {
	# Bail if we don't want to use valgrind for this check.
	if [ "${USE_VALGRIND}" -lt "${c_valgrind_min}" ]; then
		return
	fi

	val_logfilename="${s_val_basename}-${count_str}-%p.log"
	c_valgrind_cmd="valgrind \
		--log-file=${val_logfilename} \
		--track-fds=yes \
		--leak-check=full --show-leak-kinds=all \
		--errors-for-leak-kinds=all \
		--suppressions=${valgrind_suppressions}"
	echo "${c_valgrind_cmd}"
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
	printf "${description}\n" >				\
		"${s_basename}-${count_str}.desc"

	# Set up the valgrind command (or an empty string).
	c_valgrind_cmd="$(valgrind_setup_cmd)"

	# Advances the number of checks.
	s_count=$((s_count + 1))
}

## valgrind_get_basename (exitfile):
# Return the filename without ".log" of the valgrind logfile corresponding to
# ${exitfile}.
valgrind_get_basename() {
	exitfile=$1
	basename=$(basename "${exitfile}" ".exit")
	echo "${out_valgrind}/${basename}"
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

## valgrind_check_logfile(logfile)
# Check for any (unsuppressed) memory leaks recorded in a valgrind logfile.
# Echo the filename if there's a leak; otherwise, echo nothing.
valgrind_check_logfile() {
	logfile=$1

	# Bytes in use at exit.
	in_use=$(grep "in use at exit:" "${logfile}" | awk '{print $6}')

	# Sanity check.
	if [ $(echo "${in_use}" | wc -w) -ne "1" ]; then
		echo "Programmer error: invalid number valgrind outputs" 1>&2
		exit 1
	fi

	# Check for any leaks.  Use string comparison, because valgrind formats
	# the number with commas, and sh can't convert strings like "1,000"
	# into an integer.
	if [ "${in_use}" != "0" ] ; then
		# Check if all of the leaked bytes are suppressed.  The extra
		# whitespace in " suppressed" is necessary to distinguish
		# between two instances of "suppressed" in the log file.  Use
		# string comparison due to the format of the number.
		suppressed=$(grep " suppressed:" "${logfile}" |	\
		    awk '{print $3}')
		if [ "${in_use}" != "${suppressed}" ]; then
			# There is an unsuppressed leak.
			echo "${logfile}"
		fi
	fi

	# Check for the wrong number of open fds.  On a normal desktop
	# computer, we expect 4: std{in,out,err}, plus the valgrind logfile.
	# If this is running inside a virtualized OS or container or shared
	# CI setup (such as Travis-CI), there might be other open
	# descriptors.  The important thing is that the number of fds should
	# match the simple test case (executing potential_memleaks without
	# running any actual tests).
	fds_in_use=$(grep "FILE DESCRIPTORS" "${logfile}" | awk '{print $4}')
	if [ "${fds_in_use}" != "${valgrind_fds}" ] ; then
		# There is an unsuppressed leak.
		echo "${logfile}"
	fi
}

## valgrind_check_basenames (exitfile):
# Check for any memory leaks recorded in valgrind logfiles associated with a
# test exitfile.  Return the filename if there's a leak; otherwise return an
# empty string.
valgrind_check_basenames() {
	exitfile="$1"
	val_basename=$( valgrind_get_basename ${exitfile} )

	# Get list of files to check.  (Yes, the star goes outside the quotes.)
	logfiles=$(ls "${val_basename}"* 2>/dev/null)
	num_logfiles=$(echo "${logfiles}" | wc -w)

	# Bail if we don't have any valgrind logfiles to check.
	# Use numeric comparison, because wc leaves a tab in the output.
	if [ "${num_logfiles}" -eq "0" ] ; then
		return
	fi

	# Check a single file.
	if [ "${num_logfiles}" -eq "1" ]; then
		valgrind_check_logfile "${logfiles}"
		return
	fi

	# If there's two files, there's a fork() -- likely within
	# daemonize() -- so only pay attention to the child.
	if [ "${num_logfiles}" -eq "2" ]; then
		# Find both pids.
		val_pids=""
		for logfile in ${logfiles} ; do
			val_pid=$(head -n 1 "${logfile}" | cut -d "=" -f 3)
			val_pids="${val_pids} ${val_pid}"
		done

		# Find the logfile which has a parent in the list of pids.
		for logfile in ${logfiles} ; do
			val_parent_pid=$(grep "Parent PID:" "${logfile}" | \
			    awk '{ print $4 }')
			if [ "${val_pids#*$val_parent_pid}" !=		\
			    "${val_pids}" ]; then
				valgrind_check_logfile "${logfile}"
				return "$?"
			fi
		done
	fi

	# Programmer error; hard bail.
	echo "Programmer error: wrong number of valgrind logfiles!" 1>&2
	exit 1
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
	exitfiles=$(ls ${log_basename}-*.exit) || true
	if [ -z "$exitfiles" ]; then
		echo "FAILED" 1>&2
		s_retval=1
		return
	fi

	# Count results
	total_exitfiles=0
	skip_exitfiles=0

	# Check each exitfile.
	for exitfile in $(echo $exitfiles | sort); do
		ret=$(cat ${exitfile})
		total_exitfiles=$(( total_exitfiles + 1 ))
		if [ "${ret}" -lt 0 ]; then
			skip_exitfiles=$(( skip_exitfiles + 1 ))
		fi
		# Check for test failure.
		if [ "${ret}" -gt 0 ]; then
			echo "FAILED!" 1>&2
			if [ ${VERBOSE} -ne 0 ]; then
				printf "File ${exitfile} contains exit" 1>&2
				printf " code ${ret}.\n" 1>&2
				descfile=$(echo ${exitfile} |		\
				    sed 's/\.exit/\.desc/g')
				printf "Test description: " 1>&2
				cat ${descfile} 1>&2
			fi
			s_retval=${ret}
			return
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

## valgrind_init():
# Clear previous valgrind output, and prepare for running valgrind tests
# (if applicable).
valgrind_init() {
	# If we want valgrind, check that the version is high enough.
	valgrind_check_optional

	# Remove any previous directory, and create a new one.
	valgrind_prepare_directory

	# Generate valgrind suppression file if it is required.  Must be
	# done after preparing the directory.
	valgrind_ensure_suppression ${bindir}/tests/valgrind/potential-memleaks
}

## scenario_runner (scenario_filename):
# Run a test scenario from ${scenario_filename}.
scenario_runner() {
	scenario_filename=$1
	basename=$(basename ${scenario_filename} .sh)
	printf "  ${basename}... " 1>&2

	# Initialize "scenario" and "check" variables.
	s_basename=${out}/${basename}
	s_val_basename=${out_valgrind}/${basename}
	s_count=0
	c_exitfile="${NO_EXITFILE}"
	c_valgrind_min=9
	c_valgrind_cmd=""

	# Load scenario_cmd() from the scenario file.
	unset scenario_cmd
	. ${scenario_filename}
	if ! command -v scenario_cmd 1>/dev/null ; then
		printf "ERROR: scenario_cmd() is not defined in\n" 1>&2
		printf "  ${scenario_filename}\n" 1>&2
		exit 1
	fi

	# Run the scenario command.
	scenario_cmd

	# Print PASS or FAIL, and return result.
	s_retval=0
	notify_success_or_fail ${s_basename} ${s_val_basename}

	return "${s_retval}"
}

## run_scenarios (scenario_filenames):
# Run all scenarios matching ${scenario_filenames}.
run_scenarios() {
	# Clean up any previous directory, and create a new one.
	prepare_directory

	# Clean up any previous valgrind directory, and prepare for new
	# valgrind tests (if applicable).
	valgrind_init

	printf -- "Running tests\n" 1>&2
	printf -- "-------------\n" 1>&2
	for scenario in "$@"; do
		# We can't call this function with $( ... ) because we
		# want to allow it to echo values to stdout.
		scenario_runner ${scenario}
		retval=$?
		if [ ${retval} -gt 0 ]; then
			exit ${retval}
		fi
	done
}
