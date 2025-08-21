FROM opensuse/leap:15.5

RUN zypper install -y       \
    make                    \
    gcc12                   \
    gcc12-c++               \
    curl                    \
    tar                     \
    bzip2                   \
    git                     \
    wget                    \
    xz-devel                \
    libopenssl-devel        \
    libmount-devel          \
    bubblewrap              \
    rpm-build

# install python
RUN curl -O https://www.python.org/ftp/python/3.10.11/Python-3.10.11.tgz \
&& tar xzvf Python-3.10.11.tgz \
&& cd Python-3.10.11 \
&& ./configure \
&& make install -j \
&& cd ../ && rm -r Python-3.10.11

# rpmbuild > /usr/lib/rpm/macros.d/macros.meson are missing here ...
RUN python3 -m pip install --upgrade pip && python3 -m pip install meson ninja
# rpm build expects meson in /usr/bin/meson
RUN ln -s /usr/local/bin/meson /usr/bin/meson

# rpmbuild > /usr/lib/rpm/macros.d/macros.meson are missing here ...
COPY ./macros.meson /usr/lib/rpm/macros.d/macros.meson
