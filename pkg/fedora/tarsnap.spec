Name:           tarsnap
Version:        1.0.37
Release:        1%{?dist}
Summary:        Secure, efficient online backup service

License:        Proprietary
URL:            http://www.tarsnap.com/
Source0:        http://www.tarsnap.com/download/tarsnap-autoconf-%{version}.tgz

BuildRequires:  gcc
BuildRequires:  bzip2-devel
BuildRequires:  e2fsprogs-devel
BuildRequires:  libacl-devel
BuildRequires:  libattr-devel
BuildRequires:  lzma-devel
BuildRequires:  openssl-devel
BuildRequires:  xz-devel
BuildRequires:  zlib-devel
BuildRequires:  pkgconfig(bash-completion)

%description
This package contains the client for Tarsnap: a secure, efficient online
backup service.
  - Your data is encrypted with your personal keys.
  - The client source code is available.
  - Deduplication finds unique data between your current data and encrypted
    archives.


%prep
%setup -qn tarsnap-autoconf-%{version}


%build
%configure --with-bash-completion-dir
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
%make_install
mkdir -p $RPM_BUILD_ROOT/%{_pkgdocdir}
mv $RPM_BUILD_ROOT/%{_sysconfdir}/tarsnap.conf.sample \
        $RPM_BUILD_ROOT/%{_pkgdocdir}


%files
%{_bindir}/*
%{_datarootdir}/bash-completion/completions/*
%{_mandir}/man*/*
%{_pkgdocdir}/*
%license COPYING


%changelog
* Mon Mar  7 2016 Graham Percival <gperciva@tarsnap.com> 1.0.37-1
- Upstream version 1.0.37
- Reinstate libattr dependency, and add lzma.

* Fri Mar  4 2016 Benjamin Gilbert <bgilbert@backtick.net> 1.0.36.1-3
- Drop unneeded libattr dependency
- Only use pkgconfig() BuildRequires for packages detected via pkg-config

* Tue Mar  1 2016 Benjamin Gilbert <bgilbert@backtick.net> 1.0.36.1-2
- Link against libacl and libattr

* Tue Mar  1 2016 Benjamin Gilbert <bgilbert@backtick.net> 1.0.36.1-1
- Initial package
