Name:           symrouted
Version:        %{version}
Release:        1%{?dist}
Summary:        Symmetric Routing Daemon

License:        MIT
URL:            https://github.com/knneth/symrouted
Source:         %{name}-%{version}.tgz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  gcc, libnl3-devel
Requires:       libnl3
%{?systemd_requires}

%description
The symrouted program uses routing policies to allow traffic from different
local IP addresses to be forwarded to a different next hop (gateway).

%prep
%setup -q

%build
%{__make} %{?_smp_mflags}

%install
rm -rf "$RPM_BUILD_ROOT"
make install DESTDIR="$RPM_BUILD_ROOT"

%clean
make clean
rm -rf "$RPM_BUILD_ROOT"

%post
%systemd_post %{name}.service

%postun
%systemd_postun %{name}.service

%preun
%systemd_preun %{name}.service

%files
%defattr(-,root,root)
/usr/lib/systemd/system/%{name}.service
%{_sbindir}/*
%license LICENSE

%changelog
