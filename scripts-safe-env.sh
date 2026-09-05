#!/bin/zsh
# Clean environment wrapper: sandbox-poisoned full env breaks cmake prefix detection.
export HOME=/home/kevin
export PATH="/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
export LANG=C.UTF-8 LC_ALL=C.UTF-8 TERM=xterm
export CMAKE_BUILD_PARALLEL_LEVEL=2
exec "$@"
