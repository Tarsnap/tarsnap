#!/bin/sh

VERSION=$1
DESTDIR=tarsnap-autoconf-${VERSION}

# Copy bits in
mkdir ${DESTDIR}
cp COPYING Makefile.am actarsnap.m4 configure.ac tsserver .autom4te.cfg ${DESTDIR}/
cp -R keygen keymgmt keyregen lib libarchive libcperciva misc pkg recrypt tar ${DESTDIR}/

# Generate autotools files
( cd ${DESTDIR}
echo -n ${VERSION} > tar-version
autoreconf -i
rm .autom4te.cfg Makefile.am aclocal.m4 actarsnap.m4 configure.ac tar-version tsserver )

# Create tarball
tar -czf ${DESTDIR}.tgz ${DESTDIR}
rm -r ${DESTDIR}
