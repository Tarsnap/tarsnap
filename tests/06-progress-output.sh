#!/bin/sh

### Constants
c_valgrind_min=1
out_v="${s_basename}-v.stderr"
out_pb="${s_basename}-pb.stderr"
out_vpb_1k="${s_basename}-vpb-1k.stderr"
out_vpb_20k="${s_basename}-vpb-20k.stderr"
tmp="${s_basename}-tmp"

scenario_cmd() {
	# Check tarsnap --dry-run -c -v
	setup_check "check -v"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		--dry-run -c -C "${scriptdir}"			\
		-v						\
		shared_test_functions.sh			\
		shared_valgrind_functions.sh			\
		2> "${out_v}"
	echo $? > "${c_exitfile}"

	# Check output of tarsnap --dry-run -c -v
	setup_check "check -v output"
	cmp "${out_v}" "${scriptdir}/06-progress-output-v.good"
	echo $? > "${c_exitfile}"

	# Check tarsnap --dry-run -c --progress-bytes 1k
	setup_check "check --progress-bytes 1k"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		--dry-run -c -C "${scriptdir}"			\
		--progress-bytes 1k				\
		shared_test_functions.sh			\
		shared_valgrind_functions.sh			\
		2> "${out_pb}"
	echo $? > "${c_exitfile}"

	# Trim bytes from ${out_pb}
	cut -f 1-3 -d " " "${out_pb}" > "${tmp}"
	mv "${tmp}" "${out_pb}"

	# Check output of tarsnap --dry-run -c --progress-bytes 1k
	setup_check "check --progress-bytes 1k output"
	cmp "${out_pb}" "${scriptdir}/06-progress-output-pb.good"
	echo $? > "${c_exitfile}"

	# Check tarsnap --dry-run -c -v --progress-bytes 1k
	setup_check "check -v --progress-bytes 1k"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		--dry-run -c -C "${scriptdir}"			\
		-v --progress-bytes 1k				\
		shared_test_functions.sh			\
		shared_valgrind_functions.sh			\
		2> "${out_vpb_1k}"
	echo $? > "${c_exitfile}"

	# Trim bytes from ${out_vpb_1k}
	cut -f 1-3 -d " " "${out_vpb_1k}" > "${tmp}"
	mv "${tmp}" "${out_vpb_1k}"

	# Check output of tarsnap --dry-run -c -v --progress-bytes 1k
	setup_check "check -v --progress-bytes 1k output"
	cmp "${out_vpb_1k}" "${scriptdir}/06-progress-output-vpb-1k.good"
	echo $? > "${c_exitfile}"

	# Check tarsnap --dry-run -c -v --progress-bytes 20k
	setup_check "check -v --progress-bytes 20k"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		--dry-run -c -C "${scriptdir}"			\
		-v --progress-bytes 20k				\
		shared_test_functions.sh			\
		shared_valgrind_functions.sh			\
		2> "${out_vpb_20k}"
	echo $? > "${c_exitfile}"

	# Trim bytes from ${out_vpb_20k}
	cut -f 1-3 -d " " "${out_vpb_20k}" > "${tmp}"
	mv "${tmp}" "${out_vpb_20k}"

	# Check output of tarsnap --dry-run -c -v --progress-bytes 20k
	setup_check "check -v --progress-bytes 20k output"
	cmp "${out_vpb_20k}" "${scriptdir}/06-progress-output-vpb-20k.good"
	echo $? > "${c_exitfile}"
}
