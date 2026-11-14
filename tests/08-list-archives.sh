#!/bin/sh

### Constants
c_valgrind_min=9

scenario_cmd() {
	# Exercise archive-list cleanup through the ordinary test harness.
	setup_check "check list-archives cleanup regression"
	sh "${scriptdir}/unit/list-archives.sh" "${bindir}"
	echo $? > "${c_exitfile}"
}
