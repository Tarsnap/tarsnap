#!/bin/sh

### Constants
c_valgrind_min=1
samplefile=${s_basename}-test_abc_abc_abc
substitution_stderr=${s_basename}-substitution.stderr

scenario_cmd() {
	# Check that the global flag replaces all matches in a pathname.
	setup_check "check global substitution"
	touch "${samplefile}"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		-c --dry-run -v -s /abc/ABC/gp			\
		"${samplefile}"					\
		2> "${substitution_stderr}"
	echo $? > "${c_exitfile}"

	setup_check "check global substitution output"
	grep -q "test_abc_abc_abc >> .*test_ABC_ABC_ABC"	\
		"${substitution_stderr}"
	echo $? > "${c_exitfile}"
}
