#!/bin/sh

### Constants
c_valgrind_min=1

# Issue #885: a raw NUL in an mtree option list used to underflow
# remove_option() length and SIGSEGV.  After the libarchive backport,
# tarsnap must reject the input without crashing.
scenario_cmd() {
	mtree=${s_basename}-raw-nul.mtree
	stderr=${s_basename}-raw-nul.stderr

	# 15-byte reproducer from Tarsnap/tarsnap#885
	printf '#mtree\nf# w w\000\n' > "${mtree}"

	setup_check "check mtree raw-NUL option list does not crash"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
	    -c --dry-run "@${mtree}" 2> "${stderr}"
	rc=$?
	if [ "${rc}" -ge 128 ]; then
		echo 1 > "${c_exitfile}"
	else
		echo 0 > "${c_exitfile}"
	fi

	# Same underflow also existed on /set with a bare option.
	gset=${s_basename}-global-set.mtree
	gerr=${s_basename}-global-set.stderr
	printf '#mtree\n/set w w\n.\n' > "${gset}"

	setup_check "check mtree /set bare option does not crash"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
	    -c --dry-run "@${gset}" 2> "${gerr}"
	rc=$?
	if [ "${rc}" -ge 128 ]; then
		echo 1 > "${c_exitfile}"
	else
		echo 0 > "${c_exitfile}"
	fi
}
