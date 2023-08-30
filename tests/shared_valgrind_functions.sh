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
#   Set up the valgrind command if ${USE_VALGRIND} is greater than or equal to
#   ${valgrind_min}.
# - valgrind_check_basenames(exitfile):
#   Check for any memory leaks recorded in valgrind logfiles associated with a
#   test exitfile.  Return the filename if there's a leak; otherwise return an
#   empty string.
# - valgrind_incomplete():
#   Check if any valgrind log files are incomplete.

# A non-zero value unlikely to be used as an exit code by the programs being
# tested.
valgrind_exit_code=108

## valgrind_prepare_directory ():
# Clean up a previous valgrind directory, and prepare for new valgrind tests
# (if applicable).
valgrind_prepare_directory() {
	# If we don't want to generate new suppressions files, move them.
	if [ "${USE_VALGRIND_NO_REGEN}" -gt 0 ]; then
		valgrind_suppressions="${out_valgrind}/suppressions"
		fds="${out_valgrind}/fds.log"
		# Bail if the file doesn't exist.
		if [ ! -e "${valgrind_suppressions}" ]; then
			echo "No valgrind suppressions file" 1>&2
			exit 1
		fi

		# Move the files away.
		supp_tmp="$(mktemp /tmp/valgrind-suppressions.XXXXXX)"
		fds_tmp="$(mktemp /tmp/valgrind-fds.XXXXXX)"
		mv "${valgrind_suppressions}" "${supp_tmp}"
		mv "${fds}" "${fds_tmp}"
	fi

	# Always delete any previous valgrind directory.
	if [ -d "${out_valgrind}" ]; then
		rm -rf "${out_valgrind}"
	fi

	# Bail if we don't want valgrind at all.
	if [ "${USE_VALGRIND}" -eq 0 ]; then
		return
	fi

	mkdir "${out_valgrind}"

	# If we don't want to generate a new suppressions file, restore it.
	if [ "${USE_VALGRIND_NO_REGEN}" -gt 0 ]; then
		# Move the files back.
		mv "${supp_tmp}" "${valgrind_suppressions}"
		mv "${fds_tmp}" "${fds}"
	fi

	# We don't want to back up this directory.
	[ "$(uname)" = "FreeBSD" ] && chflags nodump "${out_valgrind}"
}

## valgrind_check_optional ():
# If ${USE_VALGRIND} is greater than 0, check that valgrind is available in
# the ${PATH} and is at least version 3.13.
valgrind_check_optional() {
	if [ "${USE_VALGRIND}" -gt 0 ]; then
		# Look for valgrind in ${PATH}.
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

## valgrind_process_suppression_file(filename):
# Generalize suppressions from a valgrind suppression file by omitting the
# "fun:pl_*" and "fun:main" lines and anything below them.
valgrind_process_suppression_file() {
	filename=$1

	# How many segments do we have?
	num_segments="$(grep -c "^{" "${filename}")"

	# Bail if there's nothing to do.
	if [ "${num_segments}" -eq "0" ]; then
		return
	fi

	# Sanity check.
	if [ "${num_segments}" -gt 100 ]; then
		printf "More than 100 valgrind suppressions?!\n" 1>&2
		exit 1
	fi

	# Split into segments.
	csplit -f "${filename}" "${filename}" "/{/"		\
	    "{$((num_segments - 1))}" > /dev/null

	# Skip "${filename}00" because that doesn't contain a suppression.
	i=1
	while [ "${i}" -le "${num_segments}" ]; do
		segfilename="$(printf "%s%02i" "${filename}" "${i}")"

		# Find last relevant line.
		lastline="$(grep -n "}" "${segfilename}" | cut -f1 -d:)"

		# Cut off anything below the 1st "fun:pl_" (inclusive).
		funcline="$(grep -n "fun:pl_" "${segfilename}" |	\
			cut -f1 -d: |					\
			head -n1)"
		if [ -n "${funcline}" ]; then
			if [ "${lastline}" -gt "${funcline}" ]; then
				lastline="${funcline}"
			fi
		fi

		# Cut off anything below "fun:main" (including that line).
		# (Due to linking and/or optimizations, some memory leaks
		# occur without "fun:pl_" appearing in the valgrind
		# suppression.)
		funcline="$(grep -n "fun:main" "${segfilename}" | cut -f1 -d:)"
		if [ -n "${funcline}" ]; then
			if [ "${lastline}" -gt "${funcline}" ]; then
				lastline="${funcline}"
			fi
		fi

		# Only keep the beginning of each suppression.
		lastline="$((lastline - 1))"
		head -n "${lastline}" "${segfilename}" >>	\
		    "${valgrind_suppressions}"
		printf "}\n" >> "${valgrind_suppressions}"

		# Advance to the next suppression.
		i=$((i + 1))
	done
}

## valgrind_ensure_suppression (potential_memleaks_binary):
# Run the ${potential_memleaks_binary} through valgrind, keeping
# track of any apparent memory leak in order to suppress reporting
# those leaks when testing other binaries.  Record how many file descriptors
# are open at exit in ${valgrind_fds}.
valgrind_ensure_suppression() {
	potential_memleaks_binary=$1

	# Quit if we're not using valgrind.
	if [ ! "${USE_VALGRIND}" -gt 0 ]; then
		return
	fi;

	fds_log="${out_valgrind}/fds.log"

	if [ "${USE_VALGRIND_NO_REGEN}" -gt 0 ]; then
		printf "Using old valgrind suppressions\n" 1>&2
		valgrind_fds=$(grep "FILE DESCRIPTORS" "${fds_log}" |	\
		   awk '{print $4}')
		return
	fi

	printf "Generating valgrind suppressions... " 1>&2
	valgrind_suppressions="${out_valgrind}/suppressions"
	valgrind_suppressions_log="${out_valgrind}/suppressions.pre"

	# Start off with an empty suppression file
	touch "${valgrind_suppressions}"

	# Get list of tests and the number of open descriptors at a normal exit
	valgrind_suppressions_tests="${out_valgrind}/suppressions-names.txt"
	valgrind --track-fds=yes --log-file="${fds_log}"		\
	    "${potential_memleaks_binary}" > "${valgrind_suppressions_tests}"
	valgrind_fds=$(grep "FILE DESCRIPTORS" "${fds_log}" | awk '{print $4}')

	# Generate suppressions for each test
	while read -r testname; do
		this_valgrind_supp="${valgrind_suppressions_log}-${testname}"

		# Run valgrind on the binary, sending it a "\n" so that
		# a test which uses STDIN will not wait for user input.
		printf "\n" | (valgrind					\
		    --leak-check=full --show-leak-kinds=all		\
		    --gen-suppressions=all				\
		    --suppressions="${valgrind_suppressions}"		\
		    --log-file="${this_valgrind_supp}"			\
		    "${potential_memleaks_binary}"			\
		    "${testname}")					\
		    > /dev/null

		# Append name to suppressions file
		printf "# %s\n" "${testname}" >> "${valgrind_suppressions}"

		# Strip out useless parts from the log file, and allow the
		# suppressions to apply to other binaries.
		valgrind_process_suppression_file "${this_valgrind_supp}"
	done < "${valgrind_suppressions_tests}"

	# Clean up
	rm -f "${valgrind_suppressions_log}"
	printf "done.\n" 1>&2
}

## valgrind_setup_cmd ():
# Set up the valgrind command if ${USE_VALGRIND} is greater than or equal to
# ${valgrind_min}.
valgrind_setup_cmd() {
	# Bail if we don't want to use valgrind for this check.
	if [ "${USE_VALGRIND}" -lt "${c_valgrind_min}" ]; then
		return
	fi

	val_logfilename="${s_val_basename}-${c_count_str}-%p.log"
	c_valgrind_cmd="valgrind \
		--log-file=${val_logfilename} \
		--track-fds=yes \
		--leak-check=full --show-leak-kinds=all \
		--errors-for-leak-kinds=all \
		--suppressions=${valgrind_suppressions}"
	echo "${c_valgrind_cmd}"
}

## valgrind_incomplete:
# Return 0 if at least one valgrind log file is not complete.
valgrind_incomplete() {
	# The exit code of `grep -L` is undesirable: if at least one file
	# contains the pattern, it returns 0.  To detect if at least one file
	# does *not* contain the pattern, we need to check grep's output,
	# rather than the exit code.
	_valgrind_incomplete_logfiles=$(grep -L "ERROR SUMMARY"		\
	    "${out_valgrind}"/*.log)
	test -n "${_valgrind_incomplete_logfiles}"
}

## valgrind_get_basename (exitfile):
# Return the filename without ".log" of the valgrind logfile corresponding to
# ${exitfile}.
valgrind_get_basename() {
	exitfile=$1
	basename=$(basename "${exitfile}" ".exit")
	echo "${out_valgrind}/${basename}"
}

## valgrind_check_logfile(logfile)
# Check for any (unsuppressed) memory leaks recorded in a valgrind logfile.
# Echo the filename if there's a leak; otherwise, echo nothing.
valgrind_check_logfile() {
	logfile=$1

	# Bytes in use at exit.
	in_use=$(grep "in use at exit:" "${logfile}" | awk '{print $6}')

	# Sanity check.
	if [ "$(echo "${in_use}" | wc -w)" -ne "1" ]; then
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
			return
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
		return
	fi

	# Check the error summary.
	num_errors=$(grep "ERROR SUMMARY: " "${logfile}" | awk '{print $4}')
	if [ "${num_errors}" -gt 0 ]; then
		# There was some other error(s) -- invalid read or write,
		# conditional jump based on uninitialized value(s), invalid
		# free, etc.
		echo "${logfile}"
		return
	fi
}

## valgrind_check_basenames (exitfile):
# Check for any memory leaks recorded in valgrind logfiles associated with a
# test exitfile.  Return the filename if there's a leak; otherwise return an
# empty string.
valgrind_check_basenames() {
	exitfile="$1"
	val_basename=$(valgrind_get_basename "${exitfile}")

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
			if [ "${val_pids#*"${val_parent_pid}"}" !=	\
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
	valgrind_ensure_suppression				\
	    "${bindir}/tests/valgrind/potential-memleaks"
}
