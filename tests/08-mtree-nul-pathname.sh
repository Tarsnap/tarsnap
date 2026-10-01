#!/bin/sh

### Constants
c_valgrind_min=1
leading_nul_mtree=${s_basename}-leading-nul.mtree
embedded_nul_mtree=${s_basename}-embedded-nul.mtree
valid_escape_mtree=${s_basename}-valid-escape.mtree
leading_nul_output=${s_basename}-leading-nul.output
embedded_nul_output=${s_basename}-embedded-nul.output
valid_escape_output=${s_basename}-valid-escape.output

scenario_cmd() {
	# Case 1: Leading NUL
	# Pathname '\0' must be rejected with a nonzero exit and diagnostic 'NUL in mtree pathname'.
	# It must not create an archive entry.
	printf '%s\n' '#mtree' '\0 type=file' > "${leading_nul_mtree}"
	setup_check "reject mtree pathname with leading NUL"
	${c_valgrind_cmd} ./tarsnap --no-default-config \
		-c --dry-run "@${leading_nul_mtree}" \
		> "${leading_nul_output}" 2>&1
	expected_exitcode 1 $? > "${c_exitfile}"

	setup_check "check leading NUL mtree diagnostic"
	grep -q "NUL in mtree pathname" "${leading_nul_output}"
	echo $? > "${c_exitfile}"

	setup_check "check leading NUL mtree creates no entry"
	! grep -q '^a ' "${leading_nul_output}"
	echo $? > "${c_exitfile}"

	# Case 2: Embedded NUL
	# 'alpha\0one' and 'alpha\0two' must both be rejected with the same diagnostic
	# and must not be silently truncated to 'alpha'.
	printf '%s\n' '#mtree' 'alpha\0one type=file' 'alpha\0two type=file' > \
		"${embedded_nul_mtree}"
	setup_check "reject mtree pathname with embedded NUL"
	${c_valgrind_cmd} ./tarsnap --no-default-config \
		-c -v --dry-run "@${embedded_nul_mtree}" \
		> "${embedded_nul_output}" 2>&1
	expected_exitcode 1 $? > "${c_exitfile}"

	setup_check "check embedded NUL mtree diagnostic"
	grep -q "NUL in mtree pathname" "${embedded_nul_output}"
	echo $? > "${c_exitfile}"

	setup_check "check embedded NUL mtree creates no truncated entry"
	! grep -q '^a alpha$' "${embedded_nul_output}"
	echo $? > "${c_exitfile}"

	# Case 3: Valid non-NUL escape
	# 'valid\040name' must still be accepted and produce the pathname 'valid name'.
	printf '%s\n' '#mtree' 'valid\040name type=file' > "${valid_escape_mtree}"
	setup_check "accept mtree pathname with non-NUL escape"
	${c_valgrind_cmd} ./tarsnap --no-default-config \
		-c -v --dry-run "@${valid_escape_mtree}" \
		> "${valid_escape_output}" 2>&1
	echo $? > "${c_exitfile}"

	setup_check "check non-NUL mtree pathname escape output"
	grep -q '^a valid name$' "${valid_escape_output}"
	echo $? > "${c_exitfile}"
}
