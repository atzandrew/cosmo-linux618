# cosmo-linux618

Mainline Linux **6.18 LTS** for the Planet Computers **Cosmo Communicator**
(MediaTek MT6771 / Helio P70), replacing Gemian's vendor 4.4 kernel, with a
fresh Debian install to follow.

The approach follows the Gemini PDA projects
([gemini-nixos](https://github.com/atzandrew/gemini-nixos),
[gemini-debian](https://github.com/atzandrew/gemini-debian)): a pinned
upstream kernel plus a small tree of changed files. The Cosmo needs far less
than the Gemini did, because its MT6771 is register-compatible with the
MT8183 that mainline already supports well.

**Status:** early skeleton. Nothing has been flashed. See `AGENTS.md`.

## Build

Needs gcc for arm64 (native on an arm64 host, otherwise the cross
`aarch64-linux-gnu-gcc`), plus `flex`, `bison`, `bc` and the OpenSSL and
libelf headers:

- Debian/Ubuntu: `flex bison bc libssl-dev libelf-dev`
- Fedora: `flex bison bc openssl-devel elfutils-libelf-devel`

```sh
bin/build-kernel.sh dtbs   # device tree only (fast check)
bin/build-kernel.sh        # Image.gz, DTB, modules → out/
```

The first run clones the pinned stable tag into `build/src`.
