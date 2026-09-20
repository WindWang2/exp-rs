# build-from-scratch.md — a clean machine to a configured build

Reference host used for the gates in this directory (Windows 11, VS 2022
Community 14.38.33130, CMake 3.27, Ninja, Qt 6.8.0, vcpkg manifest mode). The
same gate runs on Linux/macOS with system packages.

## 0. The one-command check

```sh
scripts/build.sh selftest          # no toolchain needed; must print ALL PASS
```

If this fails, the wrapper itself is broken — fix that before anything else.

## 1. Toolchain floor

| tool | minimum | notes |
|---|---|---|
| CMake | 3.20 (3.21 if you use `environment` in presets) | `CMakePresets.json` now requires 3.21 |
| C++ compiler | C++20: GCC 12+ / MSVC 19.29+ / AppleClang 14+ | GCC 16 needs `cmake/raise-compiler-stack.sh` (wired automatically) |
| Ninja | any | the default local generator |
| Python | 3.8+ interpreter | build-time scripts only |

## 2. Dependencies by platform

### Windows (vcpkg, the supported Windows path)

Install [vcpkg](https://github.com/microsoft/vcpkg) and integrate it, then
configure with the toolchain file. The manifest `vcpkg.json` (windows-scoped
dependencies: gdal, proj, geos, protobuf, libzip, expat, sqlite3, pcre2, gsl,
curl, zstd, jsoncpp, opencv — read the file for the authoritative list)
supplies those. Outside vcpkg you still need:

* **Qt 6.8+** kit (the code uses QtTypes, 6.5+; minimum is 6.8),
* **winflexbison** (`win_bison.exe`, `win_flex.exe`),
* **QCA** and **Qt6Keychain** installs (or set `WITH_AUTH=OFF` if you know why),
* **MSVC developer environment** — the wrappers load `vcvars64.bat` for you
  (`scripts\windows\_env.cmd` probes the location).

```bat
rem first configure (the wrapper handles vcvars + caps + logs)
scripts\windows\build.cmd configure --preset dev-default

rem smoke from an EMPTY dir on a machine that already has a configured tree
scripts\windows\build.cmd smoke --base-cache build-dev\CMakeCache.txt
```

### Ubuntu / Debian

```sh
sudo ./scripts/install_deps.sh          # scripted package set
sudo apt install qt6-base-dev qt6-tools-dev qt6-multimedia-dev qt6-5compat-dev \
    qt6-declarative-dev libqt6svg6-dev libqca-qt6-plugins ...
cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

`scripts/install_deps.sh` is the authoritative package list (Arch/Fedora/
macOS variants included). Distro Qt is often too old (Ubuntu 24.04 ships
6.4.2 < 6.8): use `aqtinstall` or the distro backports when that bites.

### macOS (Homebrew)

```sh
brew install qt@6 gdal proj geos protobuf libzip expat sqlite zstd jsoncpp \
    curl pcre2 gsl bison flex qca qtkeychain opencv
cmake -S . -B build-dev -DCMAKE_PREFIX_PATH=$(brew --prefix qt@6)
```

## 3. The clean-tree gate

```sh
# POSIX: seeds toolchain locations from an existing tree, reuses the vcpkg
# installed tree read-only (offline), and configures from an EMPTY directory
scripts/build.sh smoke --base-cache /path/to/CMakeCache.txt
```

Pass = exit 0, the configure log contains `=== SICNU dependency summary ===`,
and the target you care about builds:

```sh
scripts/build.sh build --build-dir build-smoke sicnu_generate_samples
scripts/build.sh test  --build-dir build-smoke -R test_feature_probes
```

## 4. Offline / restricted-network scenarios

* **No vcpkg bootstrap**: the smoke gate uses `-DVCPKG_MANIFEST_MODE=OFF` and
  an existing installed tree; it never downloads ports.
* **No network at all**: the configure still fetches Catch2 (the repo's own
  `FetchContent`, locked tag `v3.7.1`). Point the build at a prepared clone:
  `-DFETCHCONTENT_SOURCE_DIR_CATCH2=/path/to/catch2`. Teaching machines should
  use the `offline-lab` preset, which drops the heavy optional dependencies
  and the verification suite (`cmake/SicnuLabProfile.cmake`).
* **Classroom first run**: `scripts/offline_smoke.sh` (documented in
  `docs/deployment/lab-offline.md`) is the bundle-level gate; this directory
  is about building, not shipping.

## 5. Common errors and their fixes

| error | cause | fix |
|---|---|---|
| `rc ... 系统找不到指定的文件` / compiler test fails | MSVC developer environment not loaded | use the wrappers (they load vcvars64) or a Developer Prompt |
| `Could NOT find Qt6` with version errors | Qt < 6.8 or wrong prefix | install Qt 6.8+, set `CMAKE_PREFIX_PATH`/`Qt6_DIR` |
| `... C:/Program ... is not a full path` | an unquoted `-D` value containing spaces | quote every `-DVAR=value`; the wrappers do this by construction |
| Protobuf not found on Ubuntu | `libprotobuf-dev` ships no `protobuf-config.cmake` | already handled (#1108): CONFIG first, MODULE fallback |
| `CPLErrorStateBackuper` compile error on GDAL 3.8 | 3.9-only single-argument ctor | already handled (#1108) in `src/geospatial/io/*`; the probe macro `SICNU_HAVE_GDAL_QUIET_ERROR_CTOR` reports which API your build has |
| `Could NOT find GDAL ... Minimum version` | GDAL missing/too old | the doctor message's install line for your platform |
| catch2 clone hangs | no egress | `FETCHCONTENT_SOURCE_DIR_CATCH2=<prepared clone>` |
| ninja: `CMAKE_MAKE_PROGRAM` not found | Ninja not installed | install Ninja or use another generator (presets assume Ninja paths only for `build-*` dirs) |
| configure succeeded once, now fails with stale-cache symptoms | reused a build dir across source moves | delete the build dir; `smoke` does this for you |

## 6. Reporting a build problem

Attach, in this order:

1. the failing command from the wrapper (`build-logs/*/SUMMARY.txt` names it),
2. the `=== SICNU dependency summary ===` block from the configure log,
3. `scripts/build.sh doctor --build-dir <dir>` output,
4. platform + compiler versions.
