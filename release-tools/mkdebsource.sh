#!/bin/sh

BUILDDIR=/tmp/tarsnap-debian-source/

# Handle arguments
RELEASE_TARBALL=$1
DEBIAN_DIR=$(realpath "$2")
REVISION=$3
if [ ! -f "$RELEASE_TARBALL" ] || [ -z "$DEBIAN_DIR" ] || [ -z "$REVISION" ]; then
	echo "Usage: mkdebsource.sh RELEASE_TARBALL DEBIAN_DIR REVISION"
	exit 1
fi

# This can only be run on a system with debuild
if ! command -v debuild >/dev/null 2>&1; then
	echo "This script requires debuild"
	exit 1
fi

# Extract version from tarball name
IFS='-.' read -r STR1 STR2 X Y Z A STR3 <<EOF
$(basename "$RELEASE_TARBALL")
EOF
VERSION="$X.$Y.$Z"

# Handle a "X.Y.Z.A" pre-release version number.
if [ "$A" != "tgz" ]
then
	VERSION="$X.$Y.$Z.$A"
fi

# Define variables to be used later (requires $VERSION)
RELEASE_DIRNAME=tarsnap-autoconf-$VERSION
RENAMED_TARBALL=tarsnap_$VERSION.orig.tar.gz

# Copy (with renaming) release tarball to match debian's *required* format.
mkdir -p "$BUILDDIR"
cp "$RELEASE_TARBALL" "$BUILDDIR/$RENAMED_TARBALL"

# Extract release tarball
cd "$BUILDDIR"
tar -xzf "$RENAMED_TARBALL"

# Prepare source directory for debianizing
cd "$RELEASE_DIRNAME"
cp -r "$DEBIAN_DIR" .

# Debianize: create tarsnap_*.debian.tar.gz and tarsnap_*.dsc
debuild -S -us -uc

# Add missing newline to bottom of tarsnap_*.dsc
printf "\n" >> "${BUILDDIR}/tarsnap_$VERSION-$REVISION.dsc"

# Inform the user of the package location
echo "\nDebian source package created in:"
echo "    ${BUILDDIR}"
