#!/bin/sh

BUILDDIR=/tmp/tarsnap-debian-source/

# Handle arguments
RELEASE_TARBALL=$1
DEBIAN_DIR=$(realpath $2)
REVISION=$3
if [ ! -f $RELEASE_TARBALL ] || [ -z $DEBIAN_DIR ] || [ -z $REVISION ]; then
	echo "Usage: mkdebsource.sh RELEASE_TARBALL DEBIAN_DIR REVISION"
	exit 1
fi

# This can only be run on a system with debuild
if ! command -v debuild >/dev/null 2>&1; then
	echo "This script requires debuild"
	exit 1
fi

# Extract version from tarball name
IFS='-.' read -r STR1 STR2 X Y Z STR3 <<EOF
$(basename $RELEASE_TARBALL)
EOF
VERSION="$X.$Y.$Z"

# Handle pre-release tarballs so that they are "earlier" in Debian version
# numbering.  '~' is commonly used for upstream pre-releases; in fact, it is
# the only thing that is sorted earlier than an empty string:
# https://www.debian.org/doc/debian-policy/footnotes.html#f37
# In addition, Debian requires that the tarball contain the name of the Debian
# version number.
PRERELEASE=`echo $Z | tr -d [:digit:]`
W=`echo $Z | tr -d [:alpha:]`
if [ -z "$PRERELEASE" ]; then
	# Not a prerelease
	TAR_ORIG_VERSION="$X.$Y.$W"
else
	TAR_ORIG_VERSION="$X.$Y.$W~a"
fi

# Define variables to be used later (requires $VERSION)
RELEASE_DIRNAME=tarsnap-autoconf-$VERSION
RENAMED_TARBALL=tarsnap_$TAR_ORIG_VERSION.orig.tar.gz

# Copy (with renaming) release tarball to match debian's *required* format.
mkdir -p $BUILDDIR
cp $RELEASE_TARBALL $BUILDDIR/$RENAMED_TARBALL

# Extract release tarball
cd $BUILDDIR
tar -xzf $RENAMED_TARBALL

# Prepare source directory for debianizing
cd $RELEASE_DIRNAME
cp -r $DEBIAN_DIR .

# Debianize: create tarsnap_*.debian.tar.gz and tarsnap_*.dsc
debuild -S -us -uc

# Add missing newline to bottom of tarsnap_*.dsc
printf "\n" >> ${BUILDDIR}/tarsnap_$VERSION-$REVISION.dsc

# Inform the user of the package location
echo "\nDebian source package created in:"
echo "    ${BUILDDIR}"
