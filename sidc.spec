Name:           sidd
Version:        1.0
Release:        1%{?dist}
Summary:        A VLF signal monitor for recording sudden ionospheric disturbances

Group:          Applications/Communications
License:        GPLv2
URL:            http://abelian.org/sid/
Source0:        sidd-1.0.tgz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  fftw-devel
Requires:       fftw

%description
A program to record sudden ionospheric disturbances
via VLF.

%prep
%setup -q


%build
%configure
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
mkdir -p /var/log/sidd /var/lib/sidd
install -m 0755 sidd $RPM_BUILD_ROOT%{_bindir}/sidd
install -m 0644 sidd.conf $RPM_BUILD_ROOT%{_sysconfdir}/sidd.conf

%clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-,root,root,-)
%doc README
%{_bindir}/sidd
%config(noreplace) %{_sysconfdir}/sidd.conf
%dir /var/lib/sidd
%dir /var/log/sidd


%changelog
* Wed Jun 29 2009 Marek Mahut <mmahut@fedoraproject.org> - 1.0-1
- Inital packaing attempt
