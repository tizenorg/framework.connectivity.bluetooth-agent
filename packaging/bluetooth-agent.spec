Name:       bluetooth-agent
Summary:    Bluetooth agent packages that support various external profiles
Version:    0.0.10
Release:    1
Group:      TO_BE/FILLED_IN
License:    Apache License, Version 2.0
Source0:    %{name}-%{version}.tar.gz

Requires(post): sys-assert
BuildRequires:  pkgconfig(aul)
BuildRequires:  pkgconfig(dbus-glib-1)
BuildRequires:  pkgconfig(dlog)
BuildRequires:  pkgconfig(appsvc)
BuildRequires:  pkgconfig(capi-appfw-application)
BuildRequires:  pkgconfig(capi-media-image-util)
BuildRequires:  pkgconfig(libexif)
BuildRequires:  pkgconfig(vconf)
%if %{_repository}=="wearable"
BuildRequires:  pkgconfig(bluetooth-api)
BuildRequires:  pkgconfig(alarm-service)
BuildRequires:  pkgconfig(capi-appfw-app-manager)
BuildRequires:  pkgconfig(syspopup-caller)
BuildRequires:  pkgconfig(deviced)
%endif
%if %{_repository}=="mobile"
BuildRequires:  pkgconfig(contacts-service2)
BuildRequires:  pkgconfig(msg-service)
BuildRequires:  pkgconfig(email-service)
BuildRequires:  pkgconfig(tapi)
%endif
BuildRequires:  cmake

%description
Bluetooth agent packages that support various external profiles

%prep
%setup -q

%build
%if %{_repository}=="wearable"
export CFLAGS="$CFLAGS -DTIZEN_DEBUG_ENABLE"
export CXXFLAGS="$CXXFLAGS -DTIZEN_DEBUG_ENABLE"
export FFLAGS="$FFLAGS -DTIZEN_DEBUG_ENABLE"
%endif

export CFLAGS+=" -fpie -fvisibility=hidden"
export LDFLAGS+=" -Wl,--rpath=/usr/lib -Wl,--as-needed -Wl,--unresolved-symbols=ignore-in-shared-libs -pie"

%if %{_repository}=="wearable"
cd wearable
cmake . -DCMAKE_INSTALL_PREFIX=/usr \
	-DFEATURE_TIZENW=YES \
%elseif %{_repository}=="mobile"
cd mobile
cmake . -DCMAKE_INSTALL_PREFIX=/usr
%endif

make VERBOSE=1

%install
rm -rf %{buildroot}

%if %{_repository}=="wearable"
cd wearable
%elseif %{_repository}=="mobile"
cd mobile
%endif

%make_install

%if %{_repository}=="wearable"
install -D -m 0644 LICENSE.APLv2 %{buildroot}%{_datadir}/license/bluetooth-agent
%elseif %{_repository}=="mobile"
mkdir -p %{buildroot}/usr/share/license
cp LICENSE.APLv2 %{buildroot}/usr/share/license/%{name}
%endif

%files
%if %{_repository}=="wearable"
%manifest %{_repository}/bluetooth-agent.manifest
%defattr(-, root, root)
%{_bindir}/bluetooth-hf-agent
%{_datadir}/license/bluetooth-agent
%{_datadir}/dbus-1/services/org.bluez.hf_agent.service
%elseif %{_repository}=="mobile"
%manifest %{_repository}/bluetooth-agent.manifest
/opt/etc/smack/accesses.d/bluetooth-agent.rule
%defattr(-, root, root)
%{_bindir}/bluetooth-map-agent
%{_bindir}/bluetooth-pb-agent
%{_bindir}/bluetooth-hfp-agent
%{_datadir}/dbus-1/services/org.bluez.pb_agent.service
%{_datadir}/dbus-1/services/org.bluez.map_agent.service
%{_datadir}/dbus-1/services/org.bluez.hfp_agent.service
/usr/share/license/%{name}
%endif

