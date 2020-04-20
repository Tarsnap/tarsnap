#!/bin/sh

### Constants
c_valgrind_min=1
samplefile=${scriptdir}/test_tarsnap.sh
out_stats_stderr=${s_basename}-output-stats.stderr

scenario_cmd() {
	# Check -c --dry-run --print-stats.  The precise stats
	# will vary based on the system, so we can't check those.
	setup_check_variables
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		-c --dry-run --print-stats			\
		${samplefile}					\
		2> ${out_stats_stderr}
	echo $? > ${c_exitfile}

	# Check expected warning message.
	setup_check_variables
	grep -q "tarsnap: Removing leading" ${out_stats_stderr}
	echo $? > ${c_exitfile}

	# Check another expected warning message.
	setup_check_variables
	grep -q "(sizes may be slightly inaccurate)" ${out_stats_stderr}
	echo $? > ${c_exitfile}
}
