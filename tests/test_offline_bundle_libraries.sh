#!/bin/sh
# #1749: installed and fallback Linux bundles carry nested project libraries.
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sandbox=$(mktemp -d "${TMPDIR:-/tmp}/sicnu-bundle-libs.XXXXXX")
trap 'rm -rf "$sandbox"' EXIT HUP INT TERM
mkdir -p "$sandbox/source" "$sandbox/probes"
# Component inventory must not launch a host QGIS desktop during this fixture.
printf '#!/bin/sh\nprintf "QGIS fixture\\n"\n' > "$sandbox/probes/qgis"
chmod +x "$sandbox/probes/qgis"
cat > "$sandbox/source/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.21)
project(BundleLibraries LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib")
add_library(sicnu_fixture_runtime SHARED runtime.cpp)
set_target_properties(sicnu_fixture_runtime PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/runtime" SOVERSION 1)
add_library(sicnu_fixture_processing SHARED processing.cpp)
set_target_properties(sicnu_fixture_processing PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/processing" SOVERSION 1)
target_link_libraries(sicnu_fixture_processing PRIVATE sicnu_fixture_runtime)
add_executable(sicnu_geo_rs_cli cli.cpp)
target_link_libraries(sicnu_geo_rs_cli PRIVATE sicnu_fixture_processing)
add_executable(sicnu_generate_samples generator.cpp)
install(TARGETS sicnu_fixture_runtime sicnu_fixture_processing
  sicnu_geo_rs_cli sicnu_generate_samples RUNTIME DESTINATION bin LIBRARY DESTINATION lib)
CMAKE
printf 'int runtime_value() { return 42; }\n' > "$sandbox/source/runtime.cpp"
printf 'int runtime_value(); int result() { return runtime_value() + 1; }\n' > "$sandbox/source/processing.cpp"
printf 'int result(); int main() { return result() == 43 ? 0 : 1; }\n' > "$sandbox/source/cli.cpp"
cat > "$sandbox/source/generator.cpp" <<'CPP'
#include <filesystem>
#include <fstream>
#include <string>
int main(int argc, char **argv) {
  if (argc != 2 || !std::string(argv[1]).starts_with("--out=")) return 1;
  const std::filesystem::path output = std::string(argv[1]).substr(6);
  std::filesystem::create_directories(output);
  std::ofstream(output / "fixture.txt") << "fixture sample payload\n";
  return 0;
}
CPP
if ! cmake -S "$sandbox/source" -B "$sandbox/build" > "$sandbox/configure.log" 2>&1; then
  cat "$sandbox/configure.log"; exit 1
fi
if ! cmake --build "$sandbox/build" -j2 > "$sandbox/build.log" 2>&1; then
  cat "$sandbox/build.log"; exit 1
fi
assemble() {
  if ! PATH="$sandbox/probes:$PATH" sh "$repo_root/scripts/build_offline_bundle.sh" \
      --build-dir "$sandbox/build" --out "$sandbox/out" --version "$1" \
      > "$sandbox/$1.log" 2>&1; then
    cat "$sandbox/$1.log"
    exit 1
  fi
}
assemble installed
# Removing this owned fixture's install script exercises the fallback path.
mv "$sandbox/build/cmake_install.cmake" "$sandbox/install.cmake.saved"
assemble fallback
# Remove access to the build paths before invoking either bundled executable.
mv "$sandbox/build" "$sandbox/hidden-build"
for version in installed fallback; do
  bundle="$sandbox/out/sicnu-lab-$version"
  for library in runtime processing; do
    for suffix in .so .so.1; do
      test -f "$bundle/lib/libsicnu_fixture_$library$suffix" || {
        echo "missing $version library: libsicnu_fixture_$library$suffix" >&2
        exit 1
      }
    done
  done
  if [ "$version" = installed ]; then
    (unset LD_LIBRARY_PATH; "$bundle/bin/sicnu_geo_rs_cli")
  else
    LD_LIBRARY_PATH="$bundle/lib" "$bundle/bin/sicnu_geo_rs_cli"
  fi
  sh "$repo_root/scripts/build_offline_bundle.sh" --verify "$bundle"
  python3 - "$bundle/dependencies.json" <<'PYTHON'
import json, sys
entries = json.load(open(sys.argv[1]))["dependencies"]
project = [e for e in entries if e.get("dependency", "").startswith("libsicnu_fixture_")]
assert project and all(e["status"] == "shipped" for e in project), project
PYTHON
done
printf 'PASS: installed and fallback bundles run with their nested project libraries\n'
