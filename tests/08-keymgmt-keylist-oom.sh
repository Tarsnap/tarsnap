#!/bin/sh

### Constants
c_valgrind_min=9
oom_stderr=${s_basename}-oom.stderr
compile_stderr=${s_basename}-compile.stderr
preload_source=${s_basename}-fail-strdup.c

scenario_cmd() {
	case "$(uname -s)" in
	Linux)
		preload_library=${s_basename}-fail-strdup.so
		preload_kind=linux
		;;
	Darwin)
		preload_library=${s_basename}-fail-strdup.dylib
		preload_kind=darwin
		;;
	*)
		setup_check "skip --keylist allocation failure regression"
		echo -1 > "${c_exitfile}"
		return
		;;
	esac

	cat > "${preload_source}" <<'PRELOAD_EOF'
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

char *
strdup(const char * s)
{
	static const char sentinel[] = "31,30";
	size_t len;
	char * copy;

	if (strcmp(s, sentinel) == 0)
		return (NULL);

	len = strlen(s) + 1;
	if ((copy = malloc(len)) == NULL)
		return (NULL);
	memcpy(copy, s, len);
	return (copy);
}
PRELOAD_EOF

	setup_check "build --keylist strdup failure interposer"
	if [ "${preload_kind}" = "linux" ]; then
		cc -shared -fPIC -o "${preload_library}" "${preload_source}" \
		    2> "${compile_stderr}"
	else
		cc -dynamiclib -o "${preload_library}" "${preload_source}" \
		    2> "${compile_stderr}"
	fi
	compile_status=$?
	echo "${compile_status}" > "${c_exitfile}"
	if [ "${compile_status}" -ne 0 ]; then
		return
	fi

	setup_check "check --keylist allocation failure exits 1"
	if [ "${preload_kind}" = "linux" ]; then
		LD_PRELOAD="${preload_library}" ./tarsnap-keymgmt \
		    --keylist=31,30 2> "${oom_stderr}"
	else
		DYLD_INSERT_LIBRARIES="${preload_library}" \
		DYLD_FORCE_FLAT_NAMESPACE=1 ./tarsnap-keymgmt \
		    --keylist=31,30 2> "${oom_stderr}"
	fi
	keymgmt_status=$?
	expected_exitcode 1 "${keymgmt_status}" > "${c_exitfile}"

	setup_check "check --keylist allocation failure diagnostic"
	grep -q "Out of memory" "${oom_stderr}"
	echo $? > "${c_exitfile}"
}
