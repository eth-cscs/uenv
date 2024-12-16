Name:           slurm-uenv-mount
Version:        UENVMNT_VERSION
Release:        RPM_SLURM_VERSION
Summary:        UENV cli and SLURM spank plugin.
Prefix:         /usr

License:        BSD3
URL:            https://github.com/eth-cscs/uenv2
Source0:        %{name}-%{version}.tar.gz

%define _build_id_links none

%description
A SLURM spank plugin to mount squashfs images.

%prep
%autosetup -c

%build
%meson_setup
%meson_build

%install
%meson_install

%post
REQ="required /usr/lib64/libslurm-uenv-mount.so"
CNF=/etc/plugstack.conf.d/99-slurm-uenv-mount.conf
mkdir -p /etc/plugstack.conf.d
echo "$REQ" > "$CNF"

%files
%license LICENSE
%{_libdir}/lib%{name}.so
%{_bindir}/uenv
%{_libexecdir}/oras
