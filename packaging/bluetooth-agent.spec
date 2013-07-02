Name:       bluetooth-agent
Summary:    Bluetooth agent packages that support various external profiles
Version:    0.0.9
Release:    2
Group:      TO_BE/FILLED_IN
License:    TO BE FILLED IN
Source0:    %{name}-%{version}.tar.gz

BuildRequires:  pkgconfig(contacts-service2)
BuildRequires:  pkgconfig(dbus-glib-1)
BuildRequires:  pkgconfig(msg-service)
BuildRequires:  pkgconfig(email-service)
BuildRequires:  pkgconfig(tapi)
BuildRequires:  pkgconfig(dlog)
BuildRequires:  pkgconfig(vconf)
BuildRequires:  pkgconfig(appsvc)
BuildRequires:  cmake

%description
Bluetooth agent packages that support various external profiles

%prep
%setup -q

%build
cmake . -DCMAKE_INSTALL_PREFIX=/usr

make VERBOSE=1

%install
rm -rf %{buildroot}
%make_install

%files
%manifest bluetooth-agent.manifest
/opt/etc/smack/accesses.d/bluetooth-agent.rule
%defattr(-, root, root)
%{_bindir}/bluetooth-map-agent
%{_bindir}/bluetooth-pb-agent
%{_bindir}/bluetooth-hfp-agent
%{_datadir}/dbus-1/services/org.bluez.pb_agent.service
%{_datadir}/dbus-1/services/org.bluez.map_agent.service
%{_datadir}/dbus-1/services/org.bluez.hfp_agent.service
