#!/bin/sh

### Constants
c_valgrind_min=1
sampledir=${scriptdir}/sample-backup

check_ie() {
	name=$1
	args=$2

	# Set up variables.
	out_name=${s_basename}-${name}.txt
	good_name=${scriptdir}/07-selecting-files-${name}.good

	# Run command.  We need to `sort` because the OS doesn't necessarily
	# process files in alphabetical order.  Don't quote the ${args}.
	setup_check "check ${name}"
	${c_valgrind_cmd} ./tarsnap --no-default-config		\
		-c --dry-run-metadata -v			\
		-C "${sampledir}"				\
		${args}						\
		. 2>&1 | LC_ALL=C sort > "${out_name}"
	echo $? > "${c_exitfile}"

	# Check against expected output.
	setup_check "check ${name} output"
	cmp "${good_name}" "${out_name}"
	echo $? > "${c_exitfile}"

}

check_nT() {
	name=$1
	cmd=$2

	# Set up variables.
	out_name=${s_basename}-nT-${name}.txt
	good_name=${scriptdir}/07-selecting-files-nT-${name}.good
	filelist=${s_basename}-filelist-${name}.txt

	# Create filelist.  Don't quote ${cmd}.
	(cd "${sampledir}" && eval ${cmd} | sort > "${filelist}")

	# Run command.  We need to `sort` because the OS doesn't necessarily
	# process files in alphabetical order.
	setup_check "check ${name}"
	(
		cd "${sampledir}" &&
			${c_valgrind_cmd}				\
			"${out}"/../tarsnap --no-default-config		\
			-c --dry-run-metadata -v			\
			-C "${sampledir}"				\
			-n -T "${filelist}"				\
			2>&1 | LC_ALL=C sort > "${out_name}"
		echo $? > "${c_exitfile}"
	)

	# Check against expected output.
	setup_check "check ${name} output"
	cmp "${good_name}" "${out_name}"
	echo $? > "${c_exitfile}"

}

scenario_cmd() {
	# Run a baseline, then check include, exclude, and both.
	# For --include, we need "." otherwise it won't match anything.
	check_ie "baseline" ""
	check_ie "include" "--include . --include dir1"
	check_ie "exclude" "--exclude dir1"
	check_ie "include-exclude" "--include . --include dir1 --exclude b"

	# Check passing a list of files via -n -T.
	check_nT "full" "find ."
	check_nT "partial" "find . | grep -v dir1"
}
