# libcamera for the Cosmo front camera

| File | What |
|---|---|
| `cosmo-libcamera-patches.py` | the five Cosmo patches (see `docs/camera.md`); run in a libcamera 0.7.2 source tree |
| `build-libcamera.sh` | container build of Debian packages (upstream 0.7.2 + trixie-backports packaging + patches) |
| `gc5035.yaml` | tuning file, `/usr/share/libcamera/ipa/simple/gc5035.yaml` |
| `configuration.yaml` | global configuration (GPU software ISP), `/etc/libcamera/configuration.yaml` |

Build (arm64 host, ~5 minutes on 8 cores):

    mkdir -p out
    podman run --rm -v $PWD:/cosmo:ro -v $PWD/out:/out \
        --tmpfs /build:rw,exec,size=12g docker.io/library/debian:trixie \
        sh /cosmo/build-libcamera.sh

Install on the Cosmo: `libcamera0.7`, `libcamera-ipa`, `libcamera-tools`,
`libcamera-v4l2`, `gstreamer1.0-libcamera` from `out/`; PipeWire and
WirePlumber come from trixie-backports (with `libspa-0.2-libcamera`).

The patches are meant for upstream; once merged, the stock libcamera
package plus the two YAML files is enough.
