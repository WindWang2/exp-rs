#!/bin/bash
# Local build at hard-capped parallelism, with 60s resource sampling.
LOG=.planning/lab-content-expansion/logs/build.log
MON=.planning/lab-content-expansion/logs/resources.log
(
  while true; do
    load=$(cut -d' ' -f1 /proc/loadavg)
    rss=$(free -m | awk '/Mem:/ {printf "%.0f", $3/$2*100}')
    echo "$(date +%T) load1=$load rss_pct=$rss" >> "$MON"
    sleep 60
  done
) &
MON_PID=$!
export CMAKE_BUILD_PARALLEL_LEVEL=2 CTEST_PARALLEL_LEVEL=1
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER_LAUNCHER=/usr/bin/ccache \
  -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON \
  -DENABLE_LTO=OFF -DENABLE_SANITIZERS=OFF -DENABLE_UNITY_BUILDS=OFF \
  -DSICNU_EMBED_PYTHON=ON -DSICNU_BUILD_PYTHON_BINDINGS=TRUE \
  -DSICNU_VENDOR_GDAL=OFF -DSICNU_BUILD_OTB=OFF -DSICNU_WITH_ONNX_RUNTIME=OFF \
  -DWITH_QTWEBENGINE=OFF -DWITH_QTGAMEPAD=OFF -DFETCHCONTENT_SOURCE_DIR_PYBIND11=/home/kevin/projects/rs-studio/main/build/_deps/pybind11-src \
  > "$LOG" 2>&1
CONF=$?
if [ $CONF -eq 0 ]; then
  cmake --build build --target sicnu_geo_rs_cli -j2 >> "$LOG" 2>&1
  echo "BUILD_EXIT=$?" >> "$LOG"
fi
kill $MON_PID 2>/dev/null
echo "CONFIGURE_EXIT=$CONF" >> "$LOG"
