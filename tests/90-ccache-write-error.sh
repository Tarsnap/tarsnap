#!/bin/sh

### Constants
c_valgrind_min=9

scenario_cmd() {
    setup_check "check ccache write/remove failure unwinding"
    sh "${scriptdir}/unit/ccache-write-error.sh" "${bindir}"
    echo $? > "${c_exitfile}"
}
