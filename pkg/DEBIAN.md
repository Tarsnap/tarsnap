This method is suitable for those wishing to use or distribute Tarsnap
packages with a view towards reproducibility and cryptographically signed
packages.  If you simply want to create a binary deb package from the source
code, please see [README.md](README.md).

* [Debian - Users](#debian---users)
* [Debian - Packaging](#debian---packaging)


Debian - Users
==============

Initial setup
-------------

To use the official Tarsnap Debian source packages, see:
https://www.tarsnap.com/pkg-deb.html


Debian - Packaging
==================

In these instructions, I use `X.Y.Z` to refer to a regular Tarsnap VERSION
string (i.e. `1.0.37`), and `R` to refer to the Debian package revision
number (i.e. `-1`).

1. obtain the normal release tarball, `tarsnap-autoconf-X.Y.Z.tgz`

   If `R==1`, then go directly to step 4 and use `tarsnap-autoconf-X.Y.Z.tgz`
   by itself.  There is no need to run `mkdebsource.sh`.

2. build the source package:

        sh release-tools/mkdebsource.sh RELEASE_TARBALL R

   :warning: the revision number `R` is required by the Debian packaging
   tools.

   This will give you the Debian source files in
   `/tmp/tarsnap-debian-source/`.

3. Copy the 3 relevant files, and check them:

     - `tarsnap_X.Y.Z.orig.tar.gz`: this should be **identical** to the normal
       release tarball `tarsnap-autoconf-X.Y.Z.tgz` (unfortunately, Debian
       *requires* a different format for this filename).  You can verify that
       the tarballs are identical with `sha256sum`.

     - `tarsnap_X.Y.Z-R.debian.tar.gz`: this should have exactly the same
       contents as the `pkg/debian/` directory in the normal release tarball,
       albeit with uid and gid set to 0.  Unfortunately, `tar` does not sort
       files (by default), so we cannot use `sha256sum` or even the filesize
       to compare them (since the file order can change the compression).  The
       most convenient methods are tools like `tardiff` or GNU tar's `--diff`
       or `--compare` options.  However, we can use POSIX-compatible tar and
       cmp:

           T=tarsnap_X.Y.Z-R.debian.tar.gz
           for F in `tar -tzf $T`; do tar -xzOf $T $F | cmp - pkg/$F; done

       You should see:

           cmp: pkg/debian/: Is a directory
           cmp: pkg/debian/source/: Is a directory

     - `tarsnap_X.Y.Z-R.dsc`: this is a text file which contains package
       metadata and sha1 & sha256 sums of the above tarballs, and can be
       inspected manually.

4. To distribute these packages, use the air-gapped package builder.
