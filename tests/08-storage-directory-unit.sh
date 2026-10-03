#!/bin/sh

scenario_cmd() {
	setup_check "storage_directory_read output ownership"

	sh "${scriptdir}/unit/storage-directory.sh" "${bindir}" \
	    > "${s_basename}-${c_count_str}.stdout" \
	    2> "${s_basename}-${c_count_str}.stderr"
	echo $? > "${c_exitfile}"
}
