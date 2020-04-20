#!/bin/sh

### Constants
c_valgrind_min=1
version_stdout=${s_basename}-version.stdout
help_stdout=${s_basename}-help.stdout
no_args_stderr=${s_basename}-no-args.stderr

scenario_cmd() {
	# Check --help.
	setup_check_variables
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
	    --help > ${help_stdout}
	echo $? > ${c_exitfile}

	# Check --version.
	setup_check_variables
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
	    --version > ${version_stdout}
	echo $? > ${c_exitfile}

	setup_check_variables
	grep -q "tarsnap" ${version_stdout}
	echo $? > ${c_exitfile}

	# Check no arguments (expect exit code 1, error message).
	setup_check_variables
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
	    2> ${no_args_stderr}
	expected_exitcode 1 $? > ${c_exitfile}

	setup_check_variables
	grep -q "tarsnap: Must specify one of" ${no_args_stderr}
	echo $? > ${c_exitfile}
}
