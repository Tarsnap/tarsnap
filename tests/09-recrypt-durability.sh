#!/bin/sh

### Constants
c_valgrind_min=9

scenario_cmd() {
	# Exercise recrypt durability ordering before old-machine deletion.
	setup_check "check recrypt cache durability ordering"
	sh "${scriptdir}/unit/recrypt-durability.sh" "${bindir}"
	echo $? > "${c_exitfile}"
}
