%global tardirname sorki-sidc-cf8e2fa
# trick rpmlint into ignoring the lib path for tmpfiles.d
%global _mylib lib
%global _tmpfilesdir %{_prefix}/%{_mylib}

Name:           sidc
Version:        1.8
Release:        1%{?dist}
Summary:        A VLF signal monitor for recording sudden ionospheric disturbances

Group:          Applications/Communications
License:        GPLv2
URL:            http://github.com/sorki/sidc
Source0:        https://github.com/sorki/sidc/tarball/v%{version}/sidc-v%{version}.tar.gz

BuildRequires:  autoconf
BuildRequires:  fftw-devel
BuildRequires:  alsa-lib-devel
BuildRequires:  systemd-units
Requires:       fftw
Requires:       alsa-utils
Requires(pre):  shadow-utils

%description
sidc is a simple C program to monitor and record VLF signal
for sudden ionospheric disturbance detection.

%prep
%setup -q -n %{tardirname}

%build
autoconf
%configure
make %{?_smp_mflags}

%install
install -d %{buildroot}%{_bindir}
install -d %{buildroot}%{_sysconfdir}
install -d %{buildroot}%{_localstatedir}/lib/sidc
install -d %{buildroot}%{_localstatedir}/log/sidc
install -d -m 0755 %{buildroot}/run/sidc/

make DESTDIR=%{buildroot} install

install -Dm 644 sidc.service %{buildroot}%{_unitdir}/sidc.service
install -Dm 644 sidc.sysconf %{buildroot}%{_sysconfdir}/sysconfig/sidc
install -Dm 644 sidc.logrotate %{buildroot}%{_sysconfdir}/logrotate.d/sidc
install -Dm 644 sidc.tmpfiles %{buildroot}%{_tmpfilesdir}/tmpfiles.d/sidc.conf

%files
%doc README.rst
%doc AUTHORS
%doc LICENSE
%{_bindir}/sidc
%config(noreplace) %{_sysconfdir}/sidc.conf
%config(noreplace) %{_sysconfdir}/sysconfig/sidc
%config(noreplace) %{_sysconfdir}/logrotate.d/sidc
%{_unitdir}/sidc.service
%dir %attr(-, sidc, sidc) %{_localstatedir}/lib/sidc
%dir %attr(-, sidc, sidc) %{_localstatedir}/log/sidc
%dir %attr(-, sidc, sidc) /run/sidc/
%{_tmpfilesdir}/tmpfiles.d/sidc.conf

%pre
getent group sidc >/dev/null || groupadd -r sidc
getent passwd sidc >/dev/null || \
useradd -r -g sidc -d %{_localstatedir}/lib/sidc -s /sbin/nologin \
        -c "sidc daemon" -G audio sidc
exit 0

%post
systemctl --system daemon-reload

%changelog
* Mon Jul 23 2012 Richard Marko <rmarko@redhat.com> - 1.8-1
- Version bump, use tmpfiles.d
* Mon Jul 23 2012 Richard Marko <rmarko@redhat.com> - 1.7-2
- Replaced define macro with global
* Sat Jul 21 2012 Richard Marko <rmarko@redhat.com> - 1.7-1
- Version bump, adding logrotate and sysconfig config files
* Fri Jul 20 2012 Richard Marko <rmarko@redhat.com> - 1.6-1
- Version bump, fixed attr issue
* Fri Jul 20 2012 Richard Marko <rmarko@redhat.com> - 1.5-1
- Version bump, systemd compatible now
* Thu Jul 19 2012 Richard Marko <rmarko@redhat.com> - 1.4-1
- Version bump
* Mon Jul 16 2012 Richard Marko <rmarko@redhat.com> - 1.3-1
- Update
* Wed Jun 29 2009 Marek Mahut <mmahut@fedoraproject.org> - 1.0-1
- Initial packaging attempt
