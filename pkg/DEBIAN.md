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

To use the official Tarsnap Debian source packages, set up:

1. If you haven't already done so, install the above public key to your apt
   keychain, `gpg` keychain, and `gpgv` keychain:

        wget https://www.tarsnap.com/tarsnap-signing-key-2016.asc
        sudo apt-key add tarsnap-signing-key-2016.asc
        gpg --no-default-keyring --keyring ~/.gnupg/trustedkeys.gpg \
          --import tarsnap-signing-key-2016.asc

   (adjust the filename of the Tarsnap signing key in 2017 or later years)

   > :warning: Setting up
   > [gpgv](https://www.gnupg.org/documentation/manuals/gnupg/gpgv.html) is
   > rather awkward, but I am not aware of an easier way.  This is highly
   > unfortunate, and I'm not convinced that
   > [Hanlon's Razor](https://en.wikipedia.org/wiki/Hanlon%27s_razor) is
   > adequate to cover the situation wherein `gpg` recognizes a key but `gpgv`
   > (which is called implicitly by `dpkg-source`) does not.

   > (optional) If you would like to verify the signatures manually, you
   > probably want to install the signature to your regular `gpg` keyring (in
   > addition to the `gpgv` keyring).
   >
   >     gpg --import tarsnap-signing-key-2016.asc

2. Add the server to your `/etc/apt/sources.list`:

        deb-src https://pkg.tarsnap.com/deb-src/ ./

3. Install required software for dealing with packages, and Tarsnap in
   particular:

        sudo apt-get update
        sudo apt-get install build-essential
        sudo apt-get build-dep tarsnap


Installing / Updating
---------------------

1. As usual, run:

        sudo apt-get update

2. Build a binary package:

        apt-get source --compile tarsnap

3. Install:

        sudo dpkg -i tarsnap_<version>_<arch>.deb


Debian - Packaging
==================

In these instructions, I use `X.Y.Z` to refer to a regular Tarsnap VERSION
string (i.e. `1.0.37`), and `R` to refer to the Debian package revision
number (i.e. `-1`).

1. obtain the normal release tarball, `tarsnap-autoconf-X.Y.Z.tgz`

2. build the source package:

        sh release-tools/mkdebsource.sh RELEASE_TARBALL R

   :warning: the revision number `R` is required by the Debian packaging
   tools.

   This will give you the Debian source files in
   `/tmp/tarsnap-debian-source/`.

3. Sign the `tarsnap_X.Y.Z-R.dsc` file with your `gpg` key:

        gpg -u KEYNAME --clearsign tarsnap_X.Y.Z-R.dsc
        mv tarsnap_X.Y.Z-R.dsc.asc tarsnap_X.Y.Z-R.dsc

   :warning: By signing this file, you are endorsing three files:

     - `tarsnap_X.Y.Z.orig.tar.gz`: this should be **identical** to the normal
       release tarball `tarsnap-autoconf-X.Y.Z.tgz` (unfortunately, Debian
       *requires* a different format for this filename).  You can verify that
       the tarballs are identical with `sha256sum`.

     - `tarsnap_X.Y.Z-R.debian.tar.gz`: this should have exactly the same
       contents as the `pkg/debian/` directory in the normal release tarball.
       Unfortunately, `tar` does not sort files (by default), so we cannot use
       `sha256sum` or even the filesize to compare them (since the file order
       can change the compression).  At the moment, the best method I have
       found for verifying the tarball are tools like `tardiff`, GNU tar's
       `--diff` or `--compare` options, or manually extracting the tarball and
       using `diff -r`.

     - `tarsnap_X.Y.Z-R.dsc`: this contains package metadata and sha1 & sha256
       sums of the above tarballs.

4. To distribute these source packages, create a repository and copy the files
   there:

        mkdir -p $REPOSITORY_DIRECTORY
        cp /tmp/tarsnap-debian-source/*.dsc $REPOSITORY_DIRECTORY
        cp /tmp/tarsnap-debian-source/*.gz $REPOSITORY_DIRECTORY

   Then rebuild your repository files:

        cd $REPOSITORY_DIRECTORY
        rm -f Release
        apt-ftparchive sources . > Sources
        apt-ftparchive release . > /tmp/Release
        mv /tmp/Release Release

   (using `/tmp/Release` avoids a bug / awkward feature in `apt-ftparchive`)

   Sign the repository:

        gpg -abs -o Release.gpg Release

   :warning: By signing that file, you are endorsing two files:

     - `Sources`: this contains the checksums of *all*
       `tarsnap_X.Y.Z.orig.tar.gz`, `tarsnap_X.Y.Z-R.debian.tar.gz`, and
       `tarsnap_X.Y.Z-R.dsc` files in this repository.

     - `Release`: this contains the checksums of the `Sources` file.


