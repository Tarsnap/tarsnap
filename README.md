Client code for Tarsnap
=======================

Tarsnap is a secure, efficient online backup service: "Online
backups for the truly paranoid".  https://tarsnap.com


Building from git
-----------------

Normal users should only use the signed tarballs from tarsnap.com,
but for experimental development, use:

    autoreconf -i
    ./configure
    make


Packaging notes
---------------

Bash completion scripts: optional `configure` argument
`--with-bash-completion-dir[=DIR]`.

* If `DIR` is specified, it installs to that directory.

* If `DIR` is left blank, it attempts to use `pkg-config` and
  `bash-completion >= 2.0` to determine where to put the bash
  completion scripts.  If your system does not match those
  requirements, please specify `DIR` explicitly.

