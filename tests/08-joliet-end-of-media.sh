#!/bin/sh

### Constants
c_valgrind_min=1

# Issue #882: malformed Joliet ISO used to SIGSEGV in the ISO9660
# end-of-media error path because file->name (archive_string) was
# passed to %s.  After the backport, tarsnap must not crash.
scenario_cmd() {
	iso=${s_basename}-joliet-end-of-media.iso
	stderr=${s_basename}-joliet-end-of-media.stderr

	gunzip -c "${scriptdir}/08-joliet-end-of-media.iso.gz" > "${iso}"

	setup_check "check joliet end-of-media ISO does not crash"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
	    -c --dry-run "@${iso}" 2> "${stderr}"
	rc=$?
	if [ "${rc}" -ge 128 ]; then
		echo 1 > "${c_exitfile}"
	else
		echo 0 > "${c_exitfile}"
	fi
}
