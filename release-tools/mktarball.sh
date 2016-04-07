#!/bin/sh

VERSION=$1
if [ -z $VERSION ]; then
	echo "Please specify the version number"
	exit 1
fi
DESTDIR=tarsnap-autoconf-${VERSION}
RELEASEDATE=`date "+%B %d, %Y"`

# Copy bits in
mkdir ${DESTDIR}
cp COPYING Makefile.am actarsnap.m4 configure.ac tsserver .autom4te.cfg ${DESTDIR}/
cp -R keygen keymgmt keyregen lib libarchive libcperciva misc pkg recrypt tar ${DESTDIR}/

# Copy with substitution
for MANPAGE in */*-man */*-mdoc */*-man.in */*-mdoc.in; do
	sed -e "s/@DATE@/$RELEASEDATE/" < $MANPAGE > ${DESTDIR}/$MANPAGE
done

# Generate autotools files
( cd ${DESTDIR}
printf ${VERSION} > tar-version
autoreconf -i
rm .autom4te.cfg Makefile.am aclocal.m4 actarsnap.m4 configure.ac tar-version tsserver )

# Create tarball
tar -czf ${DESTDIR}.tgz ${DESTDIR}
rm -r ${DESTDIR}
