#!/bin/sh

### Constants
c_valgrind_min=1
missing=${s_basename}-missing.tar
open_stderr=${s_basename}-open.stderr

scenario_cmd() {
	# Exercise the ordinary local @archive failure path.  In normal test
	# mode this proves the error remains reachable and returns failure; when
	# USE_VALGRIND >= 1 the suite's existing leak checker makes the old
	# archive_read_new() leak fail this same check.
	rm -f "${missing}"
	setup_check "check failed local @archive cleanup"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
	    -c --dry-run "@${missing}"			\
	    2> "${open_stderr}"
	expected_exitcode 1 $? > "${c_exitfile}"

	setup_check "check failed local @archive diagnostic"
	grep -F "${missing}" "${open_stderr}" >/dev/null
	echo $? > "${c_exitfile}"

	# Keep the two symmetric failed-open paths from drifting apart.  The
	# diagnostic must consume archive_error_string(ina) before the reader is
	# finished, and both early returns must release the reader.
	setup_check "check both failed-open paths finish input archive"
	awk '
	/^append_archive_filename\(/ {
		fn = 1
		warned = cleaned = 0
		next
	}
	/^append_archive_tarsnap\(/ {
		fn = 2
		warned = cleaned = 0
		next
	}
	fn && /bsdtar_warnc\(bsdtar/ {
		warned = 1
	}
	fn && /archive_read_finish\(ina\);/ {
		if (!warned)
			exit 1
		cleaned = 1
	}
	fn && /return \(0\);/ {
		if (!cleaned)
			exit 1
		ok[fn] = 1
		fn = 0
	}
	END {
		if (!ok[1] || !ok[2])
			exit 1
	}' "${scriptdir}/../tar/write.c"
	echo $? > "${c_exitfile}"
}
