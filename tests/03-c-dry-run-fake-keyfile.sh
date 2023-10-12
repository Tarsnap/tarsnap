#!/bin/sh

### Constants
c_valgrind_min=1
samplefile=${scriptdir}/test_tarsnap.sh
out_stats_stderr=${s_basename}-output-stats.stderr
keyfile=${scriptdir}/fake.keys
cachedir=${s_basename}-cachedir
init_cache_stderr=${s_basename}-cachedir.stderr

scenario_cmd() {
	# Create a cache directory.
	setup_check "check --initialize-cachedir"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		--keyfile "${keyfile}" --cachedir "${cachedir}"	\
		--initialize-cachedir				\
		2> "${init_cache_stderr}"
	echo $? > "${c_exitfile}"

	setup_check "check --initialize-cachedir output"
	grep -q "created for" "${init_cache_stderr}"
	echo $? > "${c_exitfile}"

	# Check -c --dry-run --print-stats.  The precise stats
	# will vary based on the system, so we can't check those.
	# (This uses more code than a --dry-run without any keyfile.)
	setup_check "check -c --dry-run with fake key"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		--keyfile "${keyfile}" --cachedir "${cachedir}"	\
		-c --dry-run --print-stats			\
		"${samplefile}"					\
		2> "${out_stats_stderr}"
	echo $? > "${c_exitfile}"

	setup_check "check -c --dry-run with fake key output"
	grep -q "tarsnap: Removing leading" "${out_stats_stderr}"
	echo $? > "${c_exitfile}"
}
