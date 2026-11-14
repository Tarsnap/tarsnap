#!/bin/sh

### Constants
c_valgrind_min=9

scenario_cmd() {
	# Exercise the focused empty/populated chunk-directory regression.
	setup_check "check chunk-directory reader regression"
	sh "${scriptdir}/unit/chunks-directory.sh" "${bindir}"
	echo $? > "${c_exitfile}"
}
