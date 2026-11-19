#!/bin/sh
# Build Debian packages of libcamera with the Cosmo patches.
#
# Runs INSIDE a debian:trixie arm64 container, e.g.
#   podman run --rm -v $PWD:/cosmo:ro -v $PWD/out:/out \
#       --tmpfs /build:rw,exec,size=12g docker.io/library/debian:trixie \
#       sh /cosmo/build-libcamera.sh
# Upstream libcamera $LIBCAMERA_VERSION (default 0.7.2) + the Debian packaging
# of trixie-backports' libcamera 0.7.1 + cosmo-libcamera-patches.py.
# Output: /out/*.deb. The library series stays 0.7, so trixie-backports'
# PipeWire libcamera plugin (built against 0.7) keeps working.
set -e
LIBCAMERA_VERSION=${LIBCAMERA_VERSION:-0.7.2}
DEB_VERSION=${DEB_VERSION:-${LIBCAMERA_VERSION}-0cosmo2}
export DEBIAN_FRONTEND=noninteractive
cat > /etc/apt/sources.list.d/bpo.sources <<'SRC'
Types: deb deb-src
URIs: http://deb.debian.org/debian
Suites: trixie-backports
Components: main
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg
SRC
sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources
apt-get update
apt-get install -y --no-install-recommends dpkg-dev devscripts build-essential \
	fakeroot quilt python3 equivs curl ca-certificates
mkdir -p /build && cd /build
apt-get source -t trixie-backports libcamera
debdir=$(ls -d libcamera-0.7*/ | head -n1)
curl -s -f -L -o "libcamera_${LIBCAMERA_VERSION}.orig.tar.gz" \
	"https://codeload.github.com/libcamera-org/libcamera/tar.gz/refs/tags/v${LIBCAMERA_VERSION}"
tar xzf "libcamera_${LIBCAMERA_VERSION}.orig.tar.gz"
cp -a "$debdir/debian" "libcamera-${LIBCAMERA_VERSION}/"
cd "libcamera-${LIBCAMERA_VERSION}"
mk-build-deps -i -r -t "apt-get -y --no-install-recommends -t trixie-backports" debian/control
python3 /cosmo/cosmo-libcamera-patches.py
DEBFULLNAME="Cosmo build" DEBEMAIL="cosmo@localhost" dch -v "$DEB_VERSION" \
	"libcamera ${LIBCAMERA_VERSION} with the Cosmo Communicator patches (cosmo-libcamera-patches.py)."
DEB_BUILD_OPTIONS="nocheck nodoc parallel=$(nproc)" dpkg-buildpackage -b -uc -us
cp ../*.deb /out/
ls -la /out
