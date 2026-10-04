#!/bin/sh

### Constants
c_valgrind_min=1

check_invalid() {
	name=$1
	arg=$2
	stderr_file=${s_basename}-${name}.stderr

	setup_check "reject --strip-components=${arg}"
	${c_valgrind_cmd} ./tarsnap --no-default-config			\
	    "--strip-components=${arg}" --version > /dev/null		\
	    2> "${stderr_file}"
	expected_exitcode 1 $? > "${c_exitfile}"

	setup_check "check ${name} diagnostic"
	grep -Fq "Invalid --strip-components argument: ${arg}"		\
	    "${stderr_file}"
	echo $? > "${c_exitfile}"
}

check_valid_version() {
	name=$1
	arg=$2
	stdout_file=${s_basename}-${name}.stdout
	stderr_file=${s_basename}-${name}.stderr

	setup_check "accept --strip-components=${arg}"
	${c_valgrind_cmd} ./tarsnap --no-default-config			\
	    "--strip-components=${arg}" --version > "${stdout_file}"	\
	    2> "${stderr_file}"
	echo $? > "${c_exitfile}"

	setup_check "check ${name} version output"
	grep -q "^tarsnap " "${stdout_file}" && test ! -s "${stderr_file}"
	echo $? > "${c_exitfile}"
}

check_verify_nonzero() {
	name=$1
	arg=$2
	stderr_file=${s_basename}-${name}.stderr

	setup_check "parse --strip-components=${arg} as nonzero"
	${c_valgrind_cmd} ./tarsnap --no-default-config			\
	    --verify-config "--strip-components=${arg}"			\
	    2> "${stderr_file}"
	expected_exitcode 1 $? > "${c_exitfile}"

	setup_check "check ${name} mode diagnostic"
	grep -Fq "Option --strip-components is not permitted in mode --verify-config" \
	    "${stderr_file}"
	echo $? > "${c_exitfile}"
}

scenario_cmd() {
	# Accepted decimal spellings continue to parse successfully.
	check_valid_version "decimal" "10"
	check_valid_version "leading-zero-decimal" "010"
	check_valid_version "explicit-plus" "+10"

	# Inputs which were silently truncated or interpreted under base 0 must fail.
	check_invalid "empty" ""
	check_invalid "nonnumeric" "abc"
	check_invalid "trailing" "2x"
	check_invalid "hex-prefix" "0x10"
	check_invalid "negative" "-1"
	check_invalid "int-overflow" "2147483648"
	check_invalid "long-overflow" "999999999999999999999999999999999999"

	# The old base-0 parser treated 08 as zero; base-10 parsing makes it 8,
	# which the existing verify-config mode gate must reject as nonzero.
	check_verify_nonzero "decimal-08" "08"

	# A literal zero remains valid for verify-config and should be silent.
	zero_stdout=${s_basename}-zero.stdout
	zero_stderr=${s_basename}-zero.stderr
	setup_check "accept zero in verify-config mode"
	${c_valgrind_cmd} ./tarsnap --no-default-config			\
	    --verify-config --strip-components=0 > "${zero_stdout}"	\
	    2> "${zero_stderr}"
	echo $? > "${c_exitfile}"

	setup_check "check zero verify-config output"
	test ! -s "${zero_stdout}" && test ! -s "${zero_stderr}"
	echo $? > "${c_exitfile}"
}
