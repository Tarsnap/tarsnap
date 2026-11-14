#!/bin/sh

### Constants
c_valgrind_min=9

scenario_cmd() {
	if [ "$(uname -s)" != "Linux" ]; then
		setup_check "skip storage-delete pending regression on non-Linux"
		echo -1 > "${c_exitfile}"
		return
	fi

	binary=${s_basename}-storage-delete
	compile_stderr=${s_basename}-compile.stderr

	setup_check "build storage-delete pending-count regression"
	"${CC:-cc}" -std=c99 -DHAVE_CONFIG_H -DUSERAGENT='"storage-delete-test"' -O1 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections -I"${bindir}" -I"${scriptdir}/.." -I"${scriptdir}/../lib-platform" -I"${scriptdir}/../lib/crypto" -I"${scriptdir}/../lib/netpacket" -I"${scriptdir}/../lib/netproto" -I"${scriptdir}/../lib/network" -I"${scriptdir}/../libcperciva/crypto" -I"${scriptdir}/../libcperciva/util" -I"${scriptdir}/../tar/storage" "${scriptdir}/unit/storage-delete-pending.c" -Wl,--gc-sections -o "${binary}" 2> "${compile_stderr}"
	compile_status=$?
	echo "${compile_status}" > "${c_exitfile}"
	if [ "${compile_status}" -ne 0 ]; then
		return
	fi

	for spec in "success 0 0" "issue-reject 0 0" "issue-reject 1024 0" "readonly 0 0" "allocation-failure 0 0" "throttle-error 1024 0" "throttle-error 1024 17" "repeat-reject 0 17" "success-after-reject 0 0" "success-after-reject 1024 0"; do
		setup_check "storage-delete pending ownership: ${spec}"
		set -- ${spec}
		"${binary}" "$1" "$2" "$3" > "${s_basename}-${c_count_str}.json"
		echo $? > "${c_exitfile}"
	done
}
