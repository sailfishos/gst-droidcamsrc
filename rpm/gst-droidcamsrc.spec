Name:           gstreamer0.10-droidcamsrc
Summary:        GStreamer source for Android camera hal
Version:        0.0.0
Release:        1
Group:          Applications/Multimedia
License:        LGPL v2.1+
URL:            http://jollamobile.com/
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  pkgconfig(gstreamer-0.10)
BuildRequires:  pkgconfig(gstreamer-plugins-base-0.10)
BuildRequires:  pkgconfig(gstreamer-video-0.10)
BuildRequires:  pkgconfig(gstreamer-plugins-bad-free-0.10)
BuildRequires:  pkgconfig(gstreamer-pbutils-0.10)
BuildRequires:  pkgconfig(gstreamer-tag-0.10)
BuildRequires:  pkgconfig(libhardware)
BuildRequires:  pkgconfig(libgstnativebuffer)
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
%{_libdir}/gstreamer-0.10/libgstdroidcamsrc.so
