Name:           uenv
Version:        UENV_VERSION
Release:        UENV_RELEASE
Summary:        UENV cli and Slurm spank plugin.
Prefix:         /usr

License:        BSD3
URL:            https://github.com/eth-cscs/uenv
Source0:        %{name}-%{version}.tar.gz

%define _build_id_links none

%description
uenv cli, uenv Slurm spank plugin and squashfs-mount.

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
%{_libdir}/libslurm-uenv-mount.so
%{_bindir}/uenv
%{_bindir}/squashfs-mount
%attr(4755, root, root) %{_bindir}/squashfs-mount
/usr/libexec/oras
/usr/share/bash-completion/completions/uenv
