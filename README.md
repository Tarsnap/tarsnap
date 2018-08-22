
<img src="https://user-images.githubusercontent.com/59632/44454690-c825ed00-a5c1-11e8-8532-c1c9a2e7be23.png" width=100/>

Tarsnap – Online backups for the truly paranoid
----

[![Slack](https://tarsnap.now.sh/badge.svg)](https://tarsnap.now.sh)
[![Travis CI build status](https://api.travis-ci.org/Tarsnap/tarsnap.svg?branch=master)](https://travis-ci.org/Tarsnap/tarsnap)
[![Join the community on Spectrum](https://withspectrum.github.io/badge/badge.svg)](https://spectrum.chat/tarsnap)
[![Twitter Follow](https://img.shields.io/twitter/follow/tarsnap.svg?style=social)](https://twitter.com/tarsnap)

Tarsnap is a secure, efficient online backup service.

:exclamation: We strongly recommend that people use the latest official
release tarball on https://www.tarsnap.com

> This repository is intended for developers who may wish to watch changes in
> progress, investigate bugs, or test new (unreleased) features.


Usage
----

To install the latest version of Tarsnap CLI on macOS, run this command:

```
brew install tarsnap
```

Otherwise you can [build from source](https://github.com/Tarsnap/tarsnap#building).

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

