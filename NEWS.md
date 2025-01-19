### Tarsnap 1.0.41 (unreleased)

- Fixed a problem with strict aliasing if somebody compiled with gcc11 or
  higher using -O3, does not have SSE2, and is using a passphrase-protected
  keyfile.
- tarsnap will read a config file in $XDG_CONFIG_HOME/tarsnap/tarsnap.conf;
  or ~/.config/tarsnap/tarsnap.conf if $XDG_CONFIG_HOME is not set.  The
  previous config file ~/.tarsnaprc is still supported, and will not be
  deprecated.
- tarsnap now accepts --null-output, which causes --list-archives to separate
  each archive name with a null character (like `find -print0`).  If one or
  more -v arguments are specified, multiple null characters are used to
  separate fields; see the man page for details.
- tarsnap now accepts --null-input as a synonym for --null.  For compatibility
  reasons, --null is still supported, and will not be deprecated.
- tarsnap now accepts --hashes, which causes --list-archives to print hashes
  of archive names.  If one or more -v arguments are specified, it will print
  other metadata (as per --list-archives).  This option is intended for the
  GUI and is not needed for command-line usage.
- tarsnap now accepts -f TAPEHASH with --list-archives --hashes, which prints
  metadata about the specified archive(s).  Multiple -f options may be
  specified.  This option is intended for the GUI and is not needed for
  command-line usage.
- If the server-side state was modified and tarsnap exits with an error, it
  will now have an exit code of 2.
- tarsnap -c now accepts --dry-run-metadata, which simulates creating an
  archive without reading any file data.  This is significantly faster than a
  regular --dry-run, and is suitable for checking which filesystem entries
  will be archived (with -v) or checking the total archive size (with --totals
  or --progress-bytes).


Tarsnap Releases
================

### Tarsnap 1.0.40 (February 10, 2022)

- tarsnap now accepts a --dump-config option to print the command-line and all
  non-blank lines read from config files.
- tarsnap now gives an error if there are unused command-line arguments.
  (i.e. "tarsnap -d -f a1 a2", where "a2" is unused.)
- Use RDRAND as an additional source of entropy on CPUs which support it.
- Use SHANI instructions on CPUs which support them.
- When sent SIGINFO or SIGUSR1, tarsnap now prints the number of files and the
  number of uncompressed bytes processed, in addition to the previous output.
- tarsnap now accepts a --passphrase method:arg option which accepts:
  * --passphrase dev:tty-stdin
  * --passphrase dev:stdin-once
  * --passphrase dev:tty-once
  * --passphrase env:VARNAME
  * --passphrase file:FILENAME
- tarsnap now accepts a --resume-extract option to skip extracting files whose
  filesize and mtime match existing files on disk.
- tarsnap now accepts --progress-bytes SIZE, which prints a progress message
  after each SIZE bytes are processed, up to once per file.  This can be
  disabled with --no-progress-bytes.
- Assorted compatibility fixes for MacOS X, FreeBSD, OpenBSD, Solaris, ZFS,
  and gcc 4.2.1.


### Tarsnap 1.0.39 (July 29, 2017)

- Fix a division-by-zero crash when reading corrupt password-encrypted tarsnap
  key files.
- Fix a crash in reading corrupt "cpio newc" format archives included via the
  tarsnap @archive directive, on 32-bit systems.
- Fix a bug in the handling of corrupt "ar" format archives included via the
  tarsnap @archive directive.
- Fix an abort trap which complained about files with a negative modification
  time.  (This assertion occured after the archive is stored, so no user data
  was at risk.)


### Tarsnap 1.0.38 (July 15, 2017)

- tarsnap now accepts an --initialize-cachedir command, which is intended for
  the GUI and is not needed for command-line usage.
- tarsnap now applies --humanize-numbers to the SIGINFO "progress" output.
- tarsnap now gives a warning for --configfile /nosuchfile.
- tarsnap now accepts --iso-dates, which prints file and directory dates as
  yyyy-mm-dd hh:mm:ss in "list archive" mode.  This can be cancelled with
  --no-iso-dates.
- the ./configure --with-conf-no-sample flag causes in the sample config file
  to be installed as tarsnap.conf instead of tarsnap.conf.sample.  This may
  result in overwriting an existing tarsnap.conf file.
- tarsnap now accepts --force-resources to force the decryption of a
  passphrase-encrypted key file to proceed even if it is anticipated to require
  an excessive amount of memory or CPU time.  This can be cancelled with
  --no-force-resources.
- tarsnap now supports OpenSSL 1.1.
- tarsnap now displays 'Deleting archive "foo"' when deleting a single archive
  in verbose mode.  (The former behaviour was to print that message only when
  multiple archives were being deleted by a single tarsnap command.)
- tarsnap now accepts --archive-names filename, which reads a list of
  archive names from a file in addition to any -f options.


### Tarsnap 1.0.37 (March 10, 2016)

- tarsnap-key(gen|mgmt|regen) now accept a --passphrase-time option, which
  specifies the duration to be spent on computing the derived keys used for
  encrypting the tarsnap key file.
- tarsnap now accepts a --keep-going option when deleting or printing
  statistics about multiple archives.
- tarsnap-keymgmt now accepts a --print-key-permissions option for listing the
  permissions contained in a key file.
- tarsnap --print-stats now accepts a --csv-file option for printing
  statistics in comma-separated-value format.
- tarsnap now accepts a --verify-config command which exits silently after
  checking the configuration file(s) for syntax errors.
- tarsnap now looks for a server named "v1-0-0-server.tarsnap.com" instead of
  the historic "betatest-server.tarsnap.com" hostname.  This should have no
  user-visible effect in most configurations.
- tarsnap now correctly warns if a sparse extract fails due to hardlinks.
- tarsnap now prints a warning if creating an empty archive.


### Tarsnap 1.0.36.1 (August 20, 2015)

- A [theoretically exploitable one-byte buffer overflow]
  (http://www.daemonology.net/blog/2015-08-21-tarsnap-1000-exploit-bounty.html)
  when archiving objects with long path names is fixed.
- A vulnerability which allowed a corrupt archive to cause tarsnap to allocate
  a large amount of memory upon listing archives or reading the corrupt
  archive is fixed.
- Tarsnap no longer crashes if its first DNS lookup fails.
- Tarsnap no longer exits with "Callbacks uninitialized" when running on a
  dual-stack network if the first IP stack it attempts fails to connect.
- tarsnap -c --dry-run can now run without a keyfile, allowing users to
  predict how much Tarsnap will cost before signing up.
- Tarsnap now includes bash completion scripts.
- Tarsnap now automatically detects and uses AESNI and SSE2 cpu features.
- Tarsnap now works around an OS X standards-compliance bug which was making
  tarsnap fail to build.


### Tarsnap 1.0.35 (July 25, 2013)

- A bug in tarsnap 1.0.34 which could cause tarsnap to crash (segmentation
  fault or bus error) when encountering network glitches or outages is fixed.
- When tarsnap encounters "insane" filesystems (procfs and other similar
  synthetic filesystems which are not reasonable to archive), it now archives
  the filesystem mount point but by default does not recurse into the
  filesystem. Previous releases (since 1.0.26) did not archive the synthetic
  filesystem mount point.


### Tarsnap 1.0.34 (July 13, 2013)

- Tarsnap now supports both IPv4 and IPv6.
- Tarsnap is now more resilient against short network glitches when it first
  connects to the Tarsnap server.
- Tarsnap now supports platforms with mandatory structure alignment (e.g., ARM
  OABI).
- Tarsnap now restores terminal settings if killed with ^C while reading a
  password or passphrase.
- Multiple minor bug fixes and cleanups.


### Tarsnap 1.0.33 (August 10, 2012)

- Tarsnap now caches archive metadata blocks in RAM, typically providing a 5x
  - 10x speedup and reduction in bandwidth usage in the "fsck" operation and
    when deleting a large number of archives at once.
- Tarsnap's internal "chunk" metadata structure is now smaller, providing a
  ~10% reduction in usage on 32-bit machines and a ~30% reduction in memory
  usage on 64-bit machines.
- Tarsnap's --newer* options now correctly descend into old directories in
  order to look for new files. (But note that tarsnap's snapshotting makes
  these options unnecessary in most situations.)
- Multiple minor bug fixes and cleanups.


### Tarsnap 1.0.32 (February 22, 2012)

- A bug affecting the handling of the --nodump option on Linux (and in most
  cases rendering it inoperative) is fixed.
- A workaround has been added for a compiler bug in OS X 10.7 (Lion).
- The NetBSD "kernfs" and "ptyfs" filesystems are now excluded from archival
  by default.


### Tarsnap 1.0.31 (November 17, 2011)

- A race condition in key generation has been fixed which could allow a
  newly-generated key file to be read by another local user if the key file is
  being generated in a world-readable directory and the user running
  tarsnap-keygen has a umask other than 0066.
- A bug in key generation has been fixed which could allow a newly-generated
  key file to be read by another local user if they key file is being
  generated in a world-writable directory (e.g., /tmp).
- Tarsnap now supports Minix.
- Tarsnap now ignores blank lines in key files; line-buffers its output (which
  makes tarsnap --list-archives | foo more responsive); and prints a progress
  indicator during tarsnap --fsck.
- Multiple minor bug fixes.


### Tarsnap 1.0.30 (August 25, 2011)

- A bug fix in the handling of readdir errors; in earlier versions, it was
  theoretically possible for a failing hard drive or other errors in reading
  directories to result in files being silently omitted from an archive.
- Several bug fixes relating to the handling of @archive directives with mtree
  files.
- A bug fix to prevent cache directory corruption resulting in tarsnap failing
  if it was interrupted at exactly the right (wrong) moment in its operation.
- A bug fix to correctly handle ~ in tarsnap -s path substitutions.
- Many more minor bug fixes.


### Tarsnap 1.0.29 (February 7, 2011)

- New tarsnap-keyregen and tarsnap-recrypt utilities have been added for
  downloading, decrypting, re-encrypting, and re-uploading Tarsnap data (aka.
  key rotation).


### Tarsnap 1.0.28 (January 18, 2011)

- A [critical security bug]
  (http://www.daemonology.net/blog/2011-01-18-tarsnap-critical-security-bug.html)
  has been fixed in Tarsnap's chunk encryption code.


### Tarsnap 1.0.27 (June 27, 2010)

- The tarsnap -d and tarsnap --print-stats commands can now take multiple -f
  <archive> options, in which case all the specified archives will be deleted
  or have their statistics printed.
- When the --dry-run option is used, the -f <archive> option is no longer
  required.
- By default tarsnap will no longer recurse into synthetic filesystems (e.g.,
  procfs). A new --insane-filesystems option disables this behaviour (i.e.,
  allows tarsnap to recurse into synthetic filesystems).
- A new --quiet option to tarsnap disables some usually-harmless warnings.
- A new --configfile <file> option to tarsnap makes it possible to specify an
  additional configuration file; and a --no-default-config option can instruct
  tarsnap to not process the default configuration files.
- The ~/.tarsnaprc configuration file and configuration settings which begin
  with ~ are now processed by expanding ~ to the home directory of the current
  user. This is a change from the behaviour in tarsnap 1.0.26 and earlier
  where ~ was expanded based on the $HOME environment variable, and will
  affect the use of tarsnap under sudo(8).
- The tarsnap --fsck command can now be run if either the delete or write keys
  are present and no longer attempts to prune corrupted archives from the
  server.
- A new tarsnap --recover command can be used to recover a checkpointed
  archive. Checkpointed archives will continue to be automatically recovered
  when tarsnap -c or tarsnap -d are run.


### Tarsnap 1.0.26 (December 24, 2009)

- Change to the Tarsnap client-server protocol in order to reduce the need for
  connections to be dropped and re-established; in particular, this should
  avoid some "Connection lost, waiting X seconds before reconnecting" warnings
  when starting or finishing an archive.
- Fixes to bandwidth limiting via the --maxbw-rate family of options; prior to
  this version, those options could break on systems with fast internet
  connections.
- Several improvements to the tarsnap(1) manual page.
- Minor build fixes: Tarsnap now supports out-of-tree builds, and now adds the
  necessary compiler flags when building SSE2 code.


### Tarsnap 1.0.25 (July 10, 2009)

- Portability improvement: Configuration files and files passed to the -T and
  -X options of tarsnap may contain Windows or Mac end-of-line characters.
- Portability improvement: A preprocessed version of the Tarsnap man pages is
  installed on systems such as Solaris which do not have mdoc macros.
- New command-line options, for overriding options set in configuration files:
  --normalmem, --no-aggressive-networking, --no-config-exclude,
  --no-config-include, --no-disk-pause, --no-humanize-numbers, --no-maxbw,
  --no-maxbw-rate-down, --no-maxbw-rate-up, --no-nodump, --no-print-stats,
  --no-snaptime, --no-store-atime, --no-totals.
- New configuration file options: disk-pause, humanize-numbers, maxbw,
  maxbw-rate, maxbw-rate-down, maxbw-rate-up, normalmem,
  no-aggressive-networking, no-config-exclude, no-config-include,
  no-disk-pause, no-humanize-numbers, no-maxbw, no-maxbw-rate-down,
  no-maxbw-rate-up, no-nodump, no-print-stats, no-snaptime, no-store-atime,
  no-totals.


### Tarsnap 1.0.24 (June 24, 2009)

- Build process reorganization: Code shared between the tarsnap client code
  and the tarsnap-keygen and tarsnap-keymgmt utilities is built into a library
  and then accessed from there instead of being compiled multiple times.
- The autoconf version of Tarsnap (above) should now be used on all supported
  platforms, including FreeBSD; there is no longer a "native" FreeBSD source
  tarball.
- Source code reorganization: Several source files added in version 1.0.22
  have been moved in order to make it easier to keep it in sync with scrypt
  source code.
- Updated to use libarchive 2.7.0: This brings improved UTF8 / Unicode and
  extended attribute support, along with minor bug fixes.


### Tarsnap 1.0.23 (June 10, 2009)

- Tarsnap now caches the result of a DNS lookup for the Tarsnap server; this improves performance when the --aggressive-networking option is used, and increases Tarsnap's resilience in the presence of temporary network glitches.
- New --disk-pause X option which allows a duration to be specified to wait after each file is archived and after every 64 kB within each file (thus, approximately after each physical disk operation).
- Added SIGNALS section in the Tarsnap man page describing how Tarsnap handles SIGINFO+SIGUSR1 (prints progress of current operation), SIGQUIT (truncates the current archive), and SIGUSR2 (creates a checkpoint in the current archive).
- Minor bug fixes and typographical corrections.


### Tarsnap 1.0.22 (June 3, 2009)

- Add support for passphrase-protected key files using AES256-CTR,
  HMAC-SHA256, and the scrypt key derivation function via a --passphrased
  option to tarsnap-keygen and tarsnap-keymgmt.
- Tarsnap key files are now base64-encoded (i.e., printable).
- Bug fix for non-FreeBSD systems: The autoconf script now correctly detects
  support for reading file flags, allowing them to be archived (and unbreaking
  --nodump support).
- Minor bug fixes and typographical corrections.
- Due to a change in the Tarsnap key file format, key files generated by the
  tarsnap-keygen and tarsnap-keymgmt utilities from Tarsnap 1.0.22 and later
  cannot be used by pre-1.0.22 versions of Tarsnap. (However, old key files
  can still be used with new versions of Tarsnap.)


### Tarsnap 1.0.21 (March 5, 2009)

- Serious bug fix: In version 1.0.20 of Tarsnap, if the --checkpoint-bytes option is used, it is possible for an entry in Tarsnap's chunk cache to become corrupted. If this occurs, then when an archive is created containing a file for which a corrupted cache entry exists, there is a very high probability (99.8% or higher, depending on the --checkpoint-bytes value used) that Tarsnap will print the error "Skip length too long" and exit; but there is also a small chance (0.2% or less) that the archive entry corresponding to that file will be silently corrupted.
- Signal handling bug fix for operating systems with SysV signal-handling semantics: In version 1.0.20 and earlier, Tarsnap can exit with a "User Signal 1" or "User Signal 2" error. This affects OpenSolaris and Solaris, and Linux if libc 5 or earlier is used.
- Bug fix for OS X: In version 1.0.20 and earlier, Tarsnap can exit with an error of "tcgetattr(stdin): Operation not supported by device" if there is no terminal attached (e.g., if Tarsnap is run from a cron job).
- Portability fix: Tarsnap now builds on Solaris 10.


### Tarsnap 1.0.20 (February 3, 2009)

- Add --checkpoint-bytes <bytespercheckpoint> option to Tarsnap and associated
  configuration file option. This instructs tarsnap -c to store a checkpoint
  every <bytespercheckpoint> bytes; if Tarsnap (or the system on which Tarsnap
  is running) crashes, or the system's internet connection fails, in the
  middle of creating an archive, it will now be truncated at the last
  checkpoint instead of being lost entirely.
- Add Debian package-building metadata and Arch Linux PKGBUILD file. Thanks to
  Mads Sülau Jørgensen and Aaron Schaefer for their help with these.
- Minor bug fixes to autoconf build.
- Due to changes in cache directory and archive formats, pre-1.0.20 versions
  of Tarsnap should not be used to create archives after 1.0.20 or later
  versions of Tarsnap are used by the same machine.


### Tarsnap 1.0.19 (January 17, 2009)

- Bugfix in the handling of hardlinked files. This corrects a problem where
  Tarsnap could under certain rare circumstances, exit with an error message
  of "Skip length too long" when creating an archive.
- After printing a "Connection lost" warning, print a "Connection
  re-established" message once communication with the Tarsnap server has
  resumed.
- Source tarballs have been renamed; the FreeBSD tarball has been renamed from
  tarsnap-VERSION.tgz to tarsnap-freebsd-VERSION.tgz, while the autoconfed
  tarball has been renamed from tarsnap-at-VERSION.tgz to
  tarsnap-autoconf-VERSION.tgz. For backwards compatibility, tarballs will
  continue to be generated with the old names for the near future.
- A GPG-signed file containing the SHA256 hashes of source tarballs is now
  provided.
- Minor changes to improve portability. Tarsnap should now build and run on
  FreeBSD, Linux (success reported on Debian, Ubuntu, CentOS, Slackware,
  Gentoo, RedHat, and Arch), OS X, NetBSD, OpenBSD, OpenSolaris, and Cygwin.
  Thanks to Simon Burns, Justin Haynes, Bruce Leidl, and Aaron Schaefer for
  their help with testing and fixing issues.


### Tarsnap 1.0.18 (December 25, 2008)

- Many portability fixes. Tarsnap should now build and run on FreeBSD, Linux
  (success has been reported on Debian, Ubuntu, CentOS, Slackware, Gentoo, and
  RedHat), OS X, NetBSD, OpenBSD, and OpenSolaris. Thanks to "Triskelios" (IRC
  nick), "WEiRDJE" (IRC nick), Thomas Hurst, and Justin Haynes for their help
  with testing and fixing portability issues.
- Minor bugfixes (mostly harmless).


### Tarsnap 1.0.17 (December 10, 2008)

- Minor fixes to error message reporting.
- Fixes to terminal setting code so that "tarsnap &" doesn't get stuck.
- Bandwidth limiting via the --maxbw-rate family of options is now far more
  CPU-efficient.
- Add "make uninstall" to FreeBSD make logic.
- A warning is now printed when SIGQUIT or ^Q is received, since it may take a
  few seconds for Tarsnap to finish and exit cleanly.
- Many minor improvements and bugfixes due to updating the libarchive and
  bsdtar code used, including new --numeric-owner, -S (extract files as sparse
  files), and -s (rewrite paths) options.


### Tarsnap 1.0.16 (November 15, 2008)

- Add --maxbw-rate, --maxbw-rate-down, and --maxbw-rate-up options for
  limiting the bandwidth used by Tarsnap in bytes per second.
- In the Tarsnap configuration file(s), any leading ~ in a configuration
  parameter is expanded to $HOME.
- Fix "make -jN" in FreeBSD build.


### Tarsnap 1.0.15 (November 14, 2008)

- When running without the --lowmem or --verylowmem options, Tarsnap conserves
  memory if it encounters lots of small files; at worst, Tarsnap's memory
  consumption is roughly double the memory which would be consumed if the
  --lowmem option were specified.
- Add --dry-run option to tarsnap -c which simulates storing an archive
  without actually sending anything over the network.
- The tarsnap.1 and tarsnap.conf.5 manual pages now reference the correct
  default configuration file location if the CONFIGDIR variable (on FreeBSD)
  or the sysconfdir variable (with autoconf) is not /usr/local/etc.
- The autoconf build code no longer passes -Werror to the compiler; this
  allows tarsnap to build with gcc 4.3.


### Tarsnap 1.0.14 (October 14, 2008)

- The tarsnap-keygen utility and tarsnap client now understand a "send more
  money" error response sent by the tarsnap server, and print an appropriate
  message.
- Earlier versions of tarsnap-keygen and tarsnap will instead error out with a
  "network protocol violation by server" message if a user's account balance
  is not positive.


### Tarsnap 1.0.13 (October 1, 2008)

- As a safeguard against accidental destruction of key files, tarsnap-keygen
  now refuses to overwrite files.
- Add --nuke command to tarsnap which requires only the delete authorization
  key and deletes all archives.
- Add --nuke option to tarsnap-keymgmt to allow creation of a key file which
  can be used to delete all of a machine's data but cannot be used to read or
  write archives.


### Tarsnap 1.0.12 (September 27, 2008)

- Source code layout has changed, with resulting changes in make logic; but
  once compiled, there should be no functional changes.


### Tarsnap 1.0.11 (August 21, 2008)

- Add --humanize-numbers option which "humanizes" statistics printed by the
  --print-stats option.
- Add --maxbw option to interrupt archival when a specified amount of upload
  bandwidth has been used.
- Make warnings about network glitches (e.g., "Connection lost, waiting X
  seconds before reconnecting") less noisy, but add a --noisy-warnings option
  to restore the old behaviour (probably useful for debugging purposes only).


### Tarsnap 1.0.10 (August 7, 2008)

- Restricted keyfiles can be generated using a new "tarsnap-keymgmt" utility
  containing the keys needed to write backups but not read or delete them, to
  read backups but not write or delete them, etc.
- Several minor bugfixes.


### Tarsnap 1.0.9 (June 30, 2008)

- Linux 2.4 kernel compatibility fix in TCP_CORK code.
- Linux compatibility fix in terminal handling for running tarsnap without a
  controlling terminal (e.g., from cron).
- Linux autoconf fixes for ext2fs header files.
- Workaround for FreeBSD net.inet.ip.portrange.randomized + pf interaction.


### Tarsnap 1.0.8 (June 21, 2008)

- Error out in ./configure if OpenSSL or zlib are not found.
- Correctly detect presence of chroot system call in ./configure.


### Tarsnap 1.0.7 (June 13, 2008)

- Add sample tarsnap configuration file.
- Add --aggressive-networking option for systems which are limited by internet
  congestion.
- Ignore blank lines in tarsnap configuration file.


### Tarsnap 1.0.6 (June 11, 2008)

- Minor bugfixes (mostly harmless).


### Tarsnap 1.0.5 (May 27, 2008)

- Add --keep-newer-files option to tarsnap -x.
- Add SIGINFO handling to read.c; add progress-within-files reporting to
  write.c.
- Clarify error message if fchmod fails in tarsnap-keygen.
- Updated to use a newer libarchive.


### Tarsnap 1.0.4 (May 12, 2008)

- Don't print a warning about not archiving the tarsnap cache directory if it
  is being ignored due to a command-line inclusion/exclusion.


### Tarsnap 1.0.3 (May 9, 2008)

- OS X portability fixes to network code.


### Tarsnap 1.0.2 (May 3, 2008)

- Prompt in tarsnap-keygen changed for clarity.
- OS X portability fixes to network code.
- Added warning if tarsnap is passed -f option along with --list-archives or
  --fsck.


### Tarsnap 1.0.1 (April 27, 2008)

- Add EXAMPLES section to tarsnap man page.
- Don't free cache records which are still in the cache trie — this fixes a
  core dump when writing out the updated cache after running tarsnap -c.


### Tarsnap 1.0.0 (April 25, 2008)

- First public version
