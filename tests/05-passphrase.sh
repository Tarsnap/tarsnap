#!/bin/sh

### Constants
c_valgrind_min=1
keyfile=${scriptdir}/fake-passphrased.keys
passphrase="hunter2"

scenario_cmd() {
	# Try to create a cache directory with a wrong passphrase.
	setup_check_variables "check --initialize-cachedir, bad passphrase"
	echo "wrong passphrase" |				\
		${c_valgrind_cmd} ./tarsnap --no-default-config \
		--keyfile ${keyfile}				\
		--cachedir ${s_basename}-cachedir-${s_count}	\
		--initialize-cachedir				\
		--passphrase-stdin				\
		2> ${s_basename}-${s_count}.stderr
	expected_exitcode 1 $? > ${c_exitfile}

	# Create a cache directory with the correct passphrase.
	setup_check_variables "check --initialize-cachedir, passphrase stdin"
	echo "${passphrase}" |					\
		${c_valgrind_cmd} ./tarsnap --no-default-config \
		--keyfile ${keyfile}				\
		--cachedir ${s_basename}-cachedir-${s_count}	\
		--initialize-cachedir				\
		--passphrase-stdin				\
		2> ${s_basename}-${s_count}.stderr
	echo $? > ${c_exitfile}
}
