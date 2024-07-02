Name:           symrouted
Version:        %{version}
Release:        1%{?dist}
Summary:        Symmetric Routing Daemon

License:        MIT
URL:            https://github.com/knneth/symrouted
Source:         %{name}-%{version}.tgz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  devtoolset-11-build
BuildRequires:  devtoolset-11-gcc-c++
BuildRequires:  devtoolset-11-make
BuildRequires:  libnl3-devel
Requires:       libnl3
%{?systemd_requires}
%if 0%{?enable_devtoolset11:1}
%enable_devtoolset11
%endif

%description
The symrouted program uses routing policies to allow traffic from different
local IP addresses to be forwarded to a different next hop (gateway).

%prep
%setup -q

%build
%{__make} %{?_smp_mflags}

%install
rm -rf "$RPM_BUILD_ROOT"
%{__make} install DESTDIR="$RPM_BUILD_ROOT" prefix=%{_prefix}

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
%{_prefix}/lib/systemd/system/%{name}.service
%{_sbindir}/*
%license LICENSE

%changelog
* Fri May 10 2024 Kenneth Klette Jonassen <kenneth@bridgetech.tv> - 1.0
- Add the --set-route-metric option, allowing user-specified metrics to be set on all replicated routes.
- Add --dump and --help options

* Thu Jul 26 2018 Kenneth Klette Jonassen <kenneth@bridgetech.tv> - 0.1.3
- Ignore routes to IPv6 link-local subnets

* Thu Jul 26 2018 Kenneth Klette Jonassen <kenneth@bridgetech.tv> - 0.1.2
- Support NL_ACT_CHANGE routing events

* Tue Jul 10 2018 Kenneth Klette Jonassen <kenneth@bridgetech.tv> - 0.1.1
- Exit when unknown command line arguments are given
- Warn when initial cleanup fails
- Ignore address attribute changes
