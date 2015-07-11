#!/bin/sh

for M in */*-mdoc */*-mdoc.in; do
	awk -f release-tools/mdoc2man.awk < ${M}	\
	    > `echo ${M} | sed -e 's/mdoc/man/'`
done

