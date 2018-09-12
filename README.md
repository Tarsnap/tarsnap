Client code for Tarsnap
=======================

Tarsnap is a secure, efficient online backup service: "Online
backups for the truly paranoid".

:exclamation: We strongly recommend that people follow the installation
instructions at https://www.tarsnap.com/download.html to use an official
release.

> This repository is intended for developers who may wish to watch changes in
> progress, investigate bugs, or test new (unreleased) features.


News
----

A list of major changes in each version is given in [NEWS.md](NEWS.md).


Building
--------

The official releases should build and install using autotools:

    ./configure
    make
    make install

See the [BUILDING](BUILDING) file for more details.


Packaging notes
---------------

Bash completion scripts: optional `configure` argument
`--with-bash-completion-dir[=DIR]`.

* If `DIR` is specified, it installs to that directory.

* If `DIR` is left blank, it attempts to use `pkg-config` and
  `bash-completion >= 2.0` to determine where to put the bash
  completion scripts.  If your system does not match those
  requirements, please specify `DIR` explicitly.

