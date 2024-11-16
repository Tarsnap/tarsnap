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
# - valgrind_setup(str):
#   Set up the valgrind command if ${USE_VALGRIND} is greater than or equal to
#   ${valgrind_min}.  If ${str} is not blank, include it in the log filename.
# - valgrind_check(exitfile):
#   Check for any memory leaks recorded in valgrind logfiles associated with a
#   test exitfile.  Return the filename if there's a leak; otherwise return an
#   empty string.
# - valgrind_incomplete():
#   Check if any valgrind log files are incomplete.
#
# We adopt the convention of "private" function names beginning with an _.
#
### Variables
#
# Wherever possible, this suite uses local variables and
# explicitly-passed arguments, with the following exceptions:
# - valgrind_suppressions: filename of valgrind suppressions.
# - valgrind_fds_log: filename of the log of open file descriptors.


## _val_prepdir ():
# Clean up a previous valgrind directory, and prepare for new valgrind tests
# (if applicable).
_val_prepdir() {
	# If we don't want to generate new suppressions files, move them.
	if [ "${USE_VALGRIND_NO_REGEN}" -gt 0 ]; then
		# Bail if the file doesn't exist.
		if [ ! -e "${valgrind_suppressions}" ]; then
			echo "No valgrind suppressions file" 1>&2
			exit 1
		fi

		# Move the files away.
		_val_prepdir_supp_tmp="$(mktemp /tmp/valgrind-suppressions.XXXXXX)"
		_val_prepdir_fds_tmp="$(mktemp /tmp/valgrind-fds.XXXXXX)"
		mv "${valgrind_suppressions}" "${_val_prepdir_supp_tmp}"
		mv "${valgrind_fds_log}" "${_val_prepdir_fds_tmp}"
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
		mv "${_val_prepdir_supp_tmp}" "${valgrind_suppressions}"
		mv "${_val_prepdir_fds_tmp}" "${valgrind_fds_log}"
	fi

	# We don't want to back up this directory.
	[ "$(uname)" = "FreeBSD" ] && chflags nodump "${out_valgrind}"
}

## _val_checkver ():
# If ${USE_VALGRIND} is greater than 0, check that valgrind is available in
# the ${PATH} and is at least version 3.13.
_val_checkver() {
	# Quit if we're not using valgrind.
	if [ ! "${USE_VALGRIND}" -gt 0 ]; then
		return
	fi;

	# Look for valgrind in $PATH.
	if ! command -v valgrind >/dev/null 2>&1; then
		printf "valgrind not found\n" 1>&2
		exit 1
	fi

	# Check the version.
	_val_checkver_version=$(valgrind --version | cut -d "-" -f 2)
	_val_checkver_major=$(echo "${_val_checkver_version}" | cut -d "." -f 1)
	_val_checkver_minor=$(echo "${_val_checkver_version}" | cut -d "." -f 2)
	if [ "${_val_checkver_major}" -lt "3" ]; then
		printf "valgrind must be at least version 3.13\n" 1>&2
		exit 1;
	fi
	if [ "${_val_checkver_major}" -eq "3" ] &&			\
	    [ "${_val_checkver_minor}" -lt "13" ]; then
		printf "valgrind must be at least version 3.13\n" 1>&2
		exit 1;
	fi
}

## _val_seg(filename):
# Generalize an already-segmented portion of a valgrind suppressions file;
# write the result to ${valgrind_suppressions}.
_val_seg() {
	_val_seg_filename=$1

	# Find last relevant line.
	_val_seg_lastline="$(grep -n "}" "${_val_seg_filename}" | cut -f1 -d:)"

	# Cut off anything below the 1st "fun:pl_" (inclusive).
	_val_seg_funcline="$(grep -n "fun:pl_" "${_val_seg_filename}" |	\
		cut -f1 -d: |						\
		head -n1)"
	if [ -n "${_val_seg_funcline}" ]; then
		if [ "${_val_seg_lastline}" -gt "${_val_seg_funcline}" ]; then
			_val_seg_lastline="${_val_seg_funcline}"
		fi
	fi

	# Cut off anything below "fun:main" (including that line).  (Due to
	# linking and/or optimizations, some memory leaks occur without
	# "fun:pl_" appearing in the valgrind suppression.)
	_val_seg_funcline="$(grep -n "fun:main" "${_val_seg_filename}" | \
	    cut -f1 -d:)"
	if [ -n "${_val_seg_funcline}" ]; then
		if [ "${_val_seg_lastline}" -gt "${_val_seg_funcline}" ]; then
			_val_seg_lastline="${_val_seg_funcline}"
		fi
	fi

	# Only keep the beginning of each suppression.
	_val_seg_lastline="$((_val_seg_lastline - 1))"
	head -n "${_val_seg_lastline}" "${_val_seg_filename}" >>	\
	    "${valgrind_suppressions}"
	printf "}\n" >> "${valgrind_suppressions}"
}

## _val_generalize(filename):
# Generalize suppressions from a valgrind suppression file by omitting the
# "fun:pl_*" and "fun:main" lines and anything below them.
_val_generalize() {
	_val_generalize_filename=$1

	# How many segments do we have?
	_val_generalize_num_segments="$(grep -c "^{" "${_val_generalize_filename}")"

	# Bail if there's nothing to do.
	if [ "${_val_generalize_num_segments}" -eq "0" ]; then
		return
	fi

	# Sanity check.
	if [ "${_val_generalize_num_segments}" -gt 100 ]; then
		printf "More than 100 valgrind suppressions?!\n" 1>&2
		exit 1
	fi

	# Split into segments.
	csplit -f "${_val_generalize_filename}" "${_val_generalize_filename}" \
	    "/{/" "{$((_val_generalize_num_segments - 1))}" > /dev/null

	# Skip "${filename}00" because that doesn't contain a suppression.
	_val_generalize_i=1
	while [ "${_val_generalize_i}" -le "${_val_generalize_num_segments}" ]; do
		# Process segment
		_val_seg "$(printf "%s%02d"				\
		    "${_val_generalize_filename}" "${_val_generalize_i}")"

		# Advance to the next suppression.
		_val_generalize_i=$((_val_generalize_i + 1))
	done
}

## _val_ensure (potential_memleaks_binary):
# Run the ${potential_memleaks_binary} through valgrind, keeping
# track of any apparent memory leak in order to suppress reporting
# those leaks when testing other binaries.  Record a log file which shows the
# open file descriptors in ${valgrind_fds_log}.
_val_ensure() {
	_val_ensure_potential_memleaks_binary=$1

	# Quit if we're not using valgrind.
	if [ ! "${USE_VALGRIND}" -gt 0 ]; then
		return
	fi;

	if [ "${USE_VALGRIND_NO_REGEN}" -gt 0 ]; then
		printf "Using old valgrind suppressions\n" 1>&2
		return
	fi

	printf "Generating valgrind suppressions... " 1>&2
	_val_ensure_log="${out_valgrind}/suppressions.pre"

	# Start off with an empty suppression file
	touch "${valgrind_suppressions}"

	# Get list of tests and the number of open descriptors at a normal exit
	_val_ensure_names="${out_valgrind}/suppressions-names.txt"
	valgrind --track-fds=yes --log-file="${valgrind_fds_log}"	\
	    "${_val_ensure_potential_memleaks_binary}"			\
	    > "${_val_ensure_names}"

	# Generate suppressions for each test
	while read -r _val_ensure_testname; do
		_val_ensure_thisl="${_val_ensure_log}-${_val_ensure_testname}"

		# Run valgrind on the binary, sending it a "\n" so that
		# a test which uses STDIN will not wait for user input.
		printf "\n" | (valgrind					\
		    --leak-check=full --show-leak-kinds=all		\
		    --gen-suppressions=all				\
		    --trace-children=yes				\
		    --suppressions="${valgrind_suppressions}"		\
		    --log-file="${_val_ensure_thisl}"			\
		    "${_val_ensure_potential_memleaks_binary}"		\
		    "${_val_ensure_testname}")				\
		    > /dev/null

		# Append name to suppressions file
		printf "# %s\n" "${_val_ensure_testname}"		\
		    >> "${valgrind_suppressions}"

		# Strip out useless parts from the log file, and allow the
		# suppressions to apply to other binaries.
		_val_generalize "${_val_ensure_thisl}"
	done < "${_val_ensure_names}"

	# Clean up
	rm -f "${_val_ensure_log}"
	printf "done.\n" 1>&2
}

## valgrind_setup (str):
# Set up the valgrind command if ${USE_VALGRIND} is greater than or equal to
# ${valgrind_min}.  If ${str} is not blank, include it in the log filename.
valgrind_setup() {
	_valgrind_setup_str=${1:-}

	# Bail if we don't want to use valgrind for this check.
	if [ "${USE_VALGRIND}" -lt "${c_valgrind_min}" ]; then
		return
	fi

	# Set up the log filename.
	if [ -n "${_valgrind_setup_str}" ]; then
		_valgrind_setup_logfilename="${s_val_basename}-${c_count_str}-${_valgrind_setup_str}-%p.log"
	else
		_valgrind_setup_logfilename="${s_val_basename}-${c_count_str}-%p.log"
	fi

	# Set up valgrind command.
	_valgrind_setup_cmd="valgrind				\
		--log-file=${_valgrind_setup_logfilename}	\
		--track-fds=yes					\
		--trace-children=yes				\
		--leak-check=full				\
		--show-leak-kinds=all				\
		--errors-for-leak-kinds=all			\
		--suppressions=${valgrind_suppressions}"
	echo "${_valgrind_setup_cmd}"
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

## _val_getbase (exitfile):
# Return the filename without ".log" of the valgrind logfile corresponding to
# ${exitfile}.
_val_getbase() {
	_val_getbase_exitfile=$1
	_val_getbase_basename=$(basename "${_val_getbase_exitfile}" ".exit")
	echo "${out_valgrind}/${_val_getbase_basename}"
}

## _val_checkl(logfile)
# Check for any (unsuppressed) memory leaks recorded in a valgrind logfile.
# Echo the filename if there's a leak; otherwise, echo nothing.
_val_checkl() {
	_val_checkl_logfile=$1

	# Bytes in use at exit.
	_val_checkl_in_use=$(grep "in use at exit:" "${_val_checkl_logfile}" | awk '{print $6}')

	# Sanity check.
	if [ "$(echo "${_val_checkl_in_use}" | wc -w)" -ne "1" ]; then
		echo "Programmer error: invalid number valgrind outputs" 1>&2
		exit 1
	fi

	# Check for any leaks.  Use string comparison, because valgrind formats
	# the number with commas, and sh can't convert strings like "1,000"
	# into an integer.
	if [ "${_val_checkl_in_use}" != "0" ] ; then
		# Check if all of the leaked bytes are suppressed.  The extra
		# whitespace in " suppressed" is necessary to distinguish
		# between two instances of "suppressed" in the log file.  Use
		# string comparison due to the format of the number.
		_val_checkl_suppressed=$(grep " suppressed:" "${_val_checkl_logfile}" |	\
		    awk '{print $3}')
		if [ "${_val_checkl_in_use}" != "${_val_checkl_suppressed}" ]; then
			# There is an unsuppressed leak.
			echo "${_val_checkl_logfile}"
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
	_val_checkl_fds_in_use=$(grep "FILE DESCRIPTORS" "${_val_checkl_logfile}" | awk '{print $4}')
	_val_checkl_valgrind_fds=$(grep "FILE DESCRIPTORS" "${valgrind_fds_log}" | \
	    awk '{print $4}')
	if [ "${_val_checkl_fds_in_use}" != "${_val_checkl_valgrind_fds}" ] ; then
		# There is an unsuppressed leak.
		echo "${_val_checkl_logfile}"
		return
	fi

	# Check the error summary.  Get the number of expected errors from the
	# ${valgrind_fds_log} file.  (Ideally this would be 0, but due to
	# porting issues, some versions of valgrind on some platforms always
	# report a non-zero number of errors.)
	_val_checkl_num_errors=$(grep "ERROR SUMMARY: " "${_val_checkl_logfile}" | awk '{print $4}')
	_val_checkl_num_errors_basic=$(grep "ERROR SUMMARY: " "${valgrind_fds_log}" | awk '{ print $4}')
	if [ "${_val_checkl_num_errors}" != "${_val_checkl_num_errors_basic}" ]; then
		# There was some other error(s) -- invalid read or write,
		# conditional jump based on uninitialized value(s), invalid
		# free, etc.
		echo "${_val_checkl_logfile}"
		return
	fi
}

## _get_pids (logfiles):
# Extract a list of pids in the format %08d from ${logfiles}.
_get_pids() {
	_get_pids_logfiles=$1

	_get_pids_pids=""
	for _get_pids_logfile in ${_valgrind_check_logfiles} ; do
		# Get the pid.
		_get_pids_pid=$(printf "%s" "${_get_pids_logfile%%.log}" | \
		    rev | cut -d "-" -f 1 | rev)
		# Zero-pad it and add it to the new list.
		_get_pids_pids=$(printf "%s %08d"			\
		    "${_get_pids_pids}" "${_get_pids_pid}")
	done

	echo "${_get_pids_pids}"
}

## _is_parent (logfile, pids):
# If the parent pid of ${logfile} is in ${pids}, return 0; otherwise, return 1.
_is_parent () {
	_is_parent_logfile=$1
	_is_parent_pids=$2

	# Get the parent pid from the valgrind logfile
	ppid=$(grep "Parent PID:" "${_is_parent_logfile}" | \
	    awk '{ print $4 }')
	ppid=$(printf "%08d" "${ppid}")

	# If the parent is in the list of pids, this isn't the parent process.
	if [ "${_is_parent_pids#*"${ppid}"}" != "${_is_parent_pids}" ] ; then
		return 1
	fi

	# Yes, this is the parent process.
	return 0
}

## valgrind_check (exitfile):
# Check for any memory leaks recorded in valgrind logfiles associated with a
# test exitfile.  Return the filename if there's a leak; otherwise return an
# empty string.
valgrind_check() {
	_valgrind_check_exitfile="$1"
	_valgrind_check_basename=$(_val_getbase "$1")

	# Get list of files to check.  (Yes, the star goes outside the quotes.)
	_valgrind_check_logfiles=$(ls "${_valgrind_check_basename}"* 2>/dev/null)
	_valgrind_check_num=$(echo "${_valgrind_check_logfiles}" | wc -w)

	# Bail if we don't have any valgrind logfiles to check.
	# Use numeric comparison, because wc leaves a tab in the output.
	if [ "${_valgrind_check_num}" -eq "0" ] ; then
		return
	fi

	# Check a single file.
	if [ "${_valgrind_check_num}" -eq "1" ]; then
		_val_checkl "${_valgrind_check_logfiles}"
		return
	fi

	# Get a normalized list of pids.
	_valgrind_check_pids=$(_get_pids "${_valgrind_check_logfiles}")

	# If the valgrind logfiles contain "-valgrind-parent-", then we only
	# want to check the parent.  The parent is the logfile whose "parent
	# pid" is not in the list of pids.  (If one logfile contains
	# "-valgrind-parent-" then all of them should have it, so we can
	# simply check if that string occurs in the list of logfiles.)
	if [ "${_valgrind_check_logfiles#*-valgrind-parent-}" !=	\
	    "${_valgrind_check_logfiles}" ]; then
		_valgrind_check_parent=1
	else
		_valgrind_check_parent=0
	fi

	# Check the logfiles depending on whether it's the parent or not,
	# and whether we want to check the parent or children.
	for _valgrind_check_logfile in ${_valgrind_check_logfiles} ; do
		if _is_parent "${_valgrind_check_logfile}"	\
		    "${_valgrind_check_pids}" ; then
			# This is the parent.
			if [ "${_valgrind_check_parent}" -eq 1 ] ; then
				_val_checkl "${_valgrind_check_logfile}"
				# Bail if there's a problem.
				if [ "$?" -ne 0 ]; then
					return
				fi
			fi
		else
			# This is a child.
			if [ "${_valgrind_check_parent}" -eq 0 ] ; then
				_val_checkl "${_valgrind_check_logfile}"
				# Bail if there's a problem.
				if [ "$?" -ne 0 ]; then
					return
				fi
			fi
		fi
	done
}

## valgrind_init():
# Clear previous valgrind output, and prepare for running valgrind tests
# (if applicable).
valgrind_init() {
	# Set up global variables.
	valgrind_suppressions="${out_valgrind}/suppressions"
	valgrind_fds_log="${out_valgrind}/fds.log"

	# If we want valgrind, check that the version is high enough.
	_val_checkver

	# Remove any previous directory, and create a new one.
	_val_prepdir

	# Generate valgrind suppression file if it is required.  Must be
	# done after preparing the directory.
	_val_ensure "${bindir}/tests/valgrind/potential-memleaks"
}
