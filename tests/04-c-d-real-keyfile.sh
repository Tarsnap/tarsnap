#!/bin/sh

### Constants
c_valgrind_min=1
samplefile=${scriptdir}/test_tarsnap.sh
cachedir=${s_basename}-cachedir
list_archives_stdout=${s_basename}-list-archives.stdout
create_stderr=${s_basename}-create.stderr
init_cache_stderr=${s_basename}-initialize-cachedir.stderr
fsck_stdout=${s_basename}-fsck.stdout
list_contents_stdout=${s_basename}-create.stdout
extract_dir=${s_basename}-extract
delete_stderr=${s_basename}-delete.stderr
archivename="c-d-real"

scenario_cmd() {
	# Check for a keyfile.
	if [ -z "${TARSNAP_TEST_KEYFILE-}" ]; then
		# SKIP if we don't have a TARSNAP_TEST_KEYFILE.
		setup_check_variables "real keyfile skip"
		echo "-1" > ${c_exitfile}
		return
	fi
	keyfile=${TARSNAP_TEST_KEYFILE}

	# Check that this key has no archives.
	setup_check_variables "real keyfile --list-archives"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		--keyfile ${keyfile}				\
		--list-archives					\
		> ${list_archives_stdout}
	if [ $? -gt 0 ]; then
		echo "Could not list the archives!  Bail."
		echo "1" > ${c_exitfile}
		return
	else
		echo "0" > ${c_exitfile}
	fi

	# Check that this key has no archives; error if it does.
	setup_check_variables "real keyfile --list-archives output"
	if [ -s ${list_archives_stdout} ]; then
		echo "Keyfile has archives!  Bail."
		echo "1" > ${c_exitfile}
		return
	else
		echo "0" > ${c_exitfile}
	fi

	# Create a cache directory.
	setup_check_variables "real keyfile --initialize-cachedir"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		--keyfile ${keyfile} --cachedir ${cachedir}	\
		--initialize-cachedir				\
		2> ${init_cache_stderr}
	echo $? > ${c_exitfile}

	# Check --initialize-cachedir output.
	setup_check_variables "real keyfile --initialize-cachedir output"
	grep -q "created for" ${init_cache_stderr}
	echo $? > ${c_exitfile}

	# Fsck.
	setup_check_variables "real key --fsck"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		--keyfile ${keyfile} --cachedir ${cachedir}	\
		--fsck						\
		> ${fsck_stdout}
	echo $? > ${c_exitfile}

	# Check fsck output.
	setup_check_variables "real key --fsck output"
	grep -q "Phase 5: Identifying unreferenced chunks" "${fsck_stdout}"
	echo $? > ${c_exitfile}

	# Create an archive.  The precise stats will vary based on
	# the system, so we can't check those.
	setup_check_variables "real key -c"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		--keyfile ${keyfile} --cachedir ${cachedir}	\
		-c --print-stats -f ${archivename}		\
		${samplefile}					\
		2> ${create_stderr}
	echo $? > ${c_exitfile}

	# Check -c output.
	setup_check_variables "real key -c output"
	grep -q "tarsnap: Removing leading" ${create_stderr}
	echo $? > ${c_exitfile}

	setup_check_variables "real key -c output > 0"
	total=$( grep "All archives" ${create_stderr} | awk '{ print $3 }' )
	test "${total}" -gt 0
	echo $? > ${c_exitfile}

	# Test the contents of the archive.
	setup_check_variables "real key -t"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		--keyfile ${keyfile} --cachedir ${cachedir}	\
		-t -f ${archivename}				\
		> ${list_contents_stdout}
	echo $? > ${c_exitfile}

	# Check -t output.  The filename in the archive does not have a
	# leading / (it was stripped by tarsnap), so we add it to the string
	# we're checking so that it matches the ${samplefile}.
	setup_check_variables "real key -t output"
	list_contents="$(cat "${list_contents_stdout}")"
	[ "/${list_contents}" = "${samplefile}" ]
	echo $? > ${c_exitfile}

	# Extract the contents of the archive in the test output directory.
	setup_check_variables "real key -x"
	mkdir "${extract_dir}"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		--keyfile ${keyfile} --cachedir ${cachedir}	\
		-x -f ${archivename} -C "${extract_dir}"
	echo $? > ${c_exitfile}

	# Check -x output.
	setup_check_variables "real key -x output"
	cmp "${samplefile}" "${extract_dir}${samplefile}"
	echo $? > ${c_exitfile}

	# Delete the archive.
	setup_check_variables "real key -d"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		--keyfile ${keyfile} --cachedir ${cachedir}	\
		-d --print-stats -f ${archivename}		\
		2> ${delete_stderr}
	echo $? > ${c_exitfile}

	# Check -d output; we should have no data left on the server.
	setup_check_variables "real key -d output"
	total=$( grep "All archives" ${delete_stderr} | awk '{ print $3 }' )
	test "${total}" -eq 0
	echo $? > ${c_exitfile}
}
