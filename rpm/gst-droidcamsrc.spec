Name:           gstreamer1.0-droidcamsrc
Summary:        GStreamer source for Android camera hal
Version:        0.0.0
Release:        1
Group:          Applications/Multimedia
License:        LGPL v2.1+
URL:            http://jollamobile.com/
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  pkgconfig(gstreamer-1.0)
BuildRequires:  pkgconfig(gstreamer-plugins-base-1.0)
BuildRequires:  pkgconfig(gstreamer-video-1.0)
BuildRequires:  pkgconfig(gstreamer-plugins-bad-1.0)
BuildRequires:  pkgconfig(gstreamer-pbutils-1.0)
BuildRequires:  pkgconfig(gstreamer-tag-1.0)
BuildRequires:  pkgconfig(libhardware)
BuildRequires:  pkgconfig(libgstanativewindowbuffer-1.0)
BuildRequires:  pkgconfig(libexif)

%description
GStreamer source for Android camera hal

%prep
%setup -q

%build
./autogen.sh
%configure

make

%install
%make_install

%files
%defattr(-,root,root,-)
%{_libdir}/gstreamer-1.0/libgstdroidcamsrc.so
%{_sysconfdir}/xdg/*.conf
