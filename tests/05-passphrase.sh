#!/bin/sh

### Constants
c_valgrind_min=1
keyfile=${scriptdir}/fake-passphrased.keys
passphrase="hunter2"

scenario_cmd() {
	# Try to create a cache directory with a wrong passphrase.
	setup_check "check --initialize-cachedir, bad passphrase"
	echo "wrong passphrase" |				\
		${c_valgrind_cmd} ./tarsnap --no-default-config \
		--keyfile "${keyfile}"				\
		--cachedir "${s_basename}-cachedir-${c_count_str}"	\
		--initialize-cachedir				\
		--passphrase dev:stdin-once			\
		2> "${s_basename}-${c_count_str}.stderr"
	expected_exitcode 1 $? > "${c_exitfile}"

	# Create a cache directory with the correct passphrase.
	setup_check "check --initialize-cachedir, passphrase stdin"
	echo "${passphrase}" |					\
		${c_valgrind_cmd} ./tarsnap --no-default-config \
		--keyfile "${keyfile}"				\
		--cachedir "${s_basename}-cachedir-${c_count_str}"	\
		--initialize-cachedir				\
		--passphrase dev:stdin-once			\
		2> "${s_basename}-${c_count_str}.stderr"
	echo $? > "${c_exitfile}"

	# Create a cache directory with the correct passphrase in env.
	setup_check "check --initialize-cachedir, passphrase env"
	PASSENV="hunter2"					\
		${c_valgrind_cmd} ./tarsnap --no-default-config	\
		--keyfile "${keyfile}"				\
		--cachedir "${s_basename}-cachedir-${c_count_str}"	\
		--initialize-cachedir				\
		--passphrase env:PASSENV			\
		2> "${s_basename}-${c_count_str}.stderr"
	echo $? > "${c_exitfile}"

	# Create a cache directory with the correct passphrase in a file.
	setup_check "check --initialize-cachedir, passphrase file"
	printf "hunter2\n" > "${s_basename}-passphrase.txt"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		--keyfile "${keyfile}"				\
		--cachedir "${s_basename}-cachedir-${c_count_str}"	\
		--initialize-cachedir				\
		--passphrase "file:${s_basename}-passphrase.txt" \
		2> "${s_basename}-${c_count_str}.stderr"
	echo $? > "${c_exitfile}"
}
