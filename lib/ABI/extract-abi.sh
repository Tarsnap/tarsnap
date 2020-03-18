#!/bin/sh

# Defines to check.
# (Yes, SIZEOF_WCHAR_T does not have a leading underscore.)
EXTRACT="SIZEOF_WCHAR_T			\
	_DARWIN_USE_64_BIT_INODE	\
	_FILE_OFFSET_BITS		\
	_LARGE_FILES			\
	"

# Config file to search.
config=$1

# Extract the defines.
defs=""
for def in $EXTRACT
do
	# Find the line contining the define (if it exists), else bail.
	line=$( grep "define ${def}" "${config}" )
	if [ -z "${line}" ]; then
		continue
	fi

	# Find the value.
	value=$( echo "${line}" | sed -e "s/^.*define ${def} //g" )

	# Record the define and value.
	defs="${defs} -D${def}=${value}"
done

# Output the values, after stripping the initial whitespace.
echo "${defs}" | sed 's/^ //g'
