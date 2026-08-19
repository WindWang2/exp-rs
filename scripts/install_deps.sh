#!/bin/bash
# install_deps.sh — 一键安装 SICNU GEO RS 所有系统依赖
#
# 用法:
#   chmod +x scripts/install_deps.sh
#   sudo ./scripts/install_deps.sh
#
# 支持: Arch Linux, Ubuntu/Debian, Fedora, macOS (Homebrew)

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

VENDORED=false
for arg in "$@"; do
    case $arg in
        --vendored) VENDORED=true ;;
    esac
done

info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; }

detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        echo "$ID"
    elif command -v brew &>/dev/null; then
        echo "macos"
    else
        echo "unknown"
    fi
}

install_arch() {
    info "Arch Linux 检测到，安装依赖..."
    local pkgs=(
        qt6-base qt6-svg qt6-tools qt6-multimedia qt6-5compat qt6-declarative
        sqlite zlib libzip zstd jsoncpp
        protobuf curl expat pcre2
        qca-qt6
        cmake make gcc
        bison flex
        python
    )
    if [ "$VENDORED" = false ]; then
        pkgs+=(gdal proj geos)
    else
        info "跳过系统 GDAL/PROJ/GEOS（使用 vendor 模式）"
    fi
    pkgs+=(opencv boost boost-libs libsvm muparser gsl)
    info "boost = headers; boost-libs = runtime (OTB 需要头文件与库版本一致)"
    info "若无 sudo: ./scripts/fetch_boost_headers.sh 可拉取匹配头文件到 vendor/boost_sys/"
    info "OTB 可选: yay -S muparserx  (BandMathX / RadiometricIndices 应用)"
    pacman -S --needed --noconfirm "${pkgs[@]}"
}

install_ubuntu() {
    info "Ubuntu/Debian 检测到，安装依赖..."
    apt-get update
    local pkgs=(
        qt6-base-dev qt6-svg-dev qt6-tools-dev qt6-multimedia-dev
        qt6-declarative-dev libqt6core5compat6-dev libqt6test6
        libsqlite3-dev zlib1g-dev libzip-dev libzstd-dev libjsoncpp-dev
        libprotobuf-dev protobuf-compiler
        libcurl4-openssl-dev libexpat1-dev libpcre2-dev
        libqca-qt6-dev qtkeychain-qt6-dev
        cmake make g++
        bison flex
        python3-dev
        libopencv-dev
        libgsl-dev
    )
    if [ "$VENDORED" = false ]; then
        pkgs+=(libgdal-dev libproj-dev libgeos-dev)
    else
        info "跳过系统 GDAL/PROJ/GEOS（使用 vendor 模式）"
    fi
    apt-get install -y "${pkgs[@]}"
}

install_fedora() {
    info "Fedora/RHEL 检测到，安装依赖..."
    local pkgs=(
        qt6-qtbase-devel qt6-qtsvg-devel qt6-qttools-devel
        qt6-qtmultimedia-devel qt6-qtdeclarative-devel qt6-qt5compat-devel
        sqlite-devel zlib-devel libzip-devel libzstd-devel jsoncpp-devel
        protobuf-devel curl-devel expat-devel pcre2-devel
        qca-qt6-devel
        cmake make gcc-c++
        bison flex
        python3-devel
        opencv-devel
        gsl-devel
    )
    if [ "$VENDORED" = false ]; then
        pkgs+=(gdal-devel proj-devel geos-devel)
    else
        info "跳过系统 GDAL/PROJ/GEOS（使用 vendor 模式）"
    fi
    dnf install -y "${pkgs[@]}"
}

install_macos() {
    info "macOS (Homebrew) 检测到，安装依赖..."
    local pkgs=(
        qt6
        sqlite3 zlib libzip zstd jsoncpp
        protobuf curl expat pcre2
        qca qtkeychain
        cmake bison flex
        python3
        opencv
        gsl
    )
    if [ "$VENDORED" = false ]; then
        pkgs+=(gdal proj geos)
    else
        info "跳过系统 GDAL/PROJ/GEOS（使用 vendor 模式）"
    fi
    brew install "${pkgs[@]}"
}

# Main
DISTRO=$(detect_distro)
info "检测到发行版: $DISTRO"

case "$DISTRO" in
    arch|manjaro|endeavouros)
        install_arch
        ;;
    ubuntu|debian|linuxmint|pop)
        install_ubuntu
        ;;
    fedora|rhel|centos|rocky|alma)
        install_fedora
        ;;
    macos)
        install_macos
        ;;
    *)
        error "不支持的发行版: $DISTRO"
        echo "请手动安装以下依赖:"
        echo "  Qt6 (Core, Gui, Widgets, Svg, Tools, Multimedia, 5Compat)"
        echo "  GDAL >= 3.4, PROJ >= 8, GEOS >= 3.10"
        echo "  SQLite3, ZLIB, LibZip, ZSTD, Protobuf, CURL, EXPAT, PCRE2"
        echo "  QCA (Qt Cryptographic Architecture)"
        echo "  CMake >= 3.20, BISON, FLEX"
        echo "  Python 3 (build-time only)"
        echo "  OpenCV >= 4.5 (optional, for SIFT/classification)"
        exit 1
        ;;
esac

info "所有依赖安装完成！"
echo ""
echo "构建项目:"
echo "  mkdir build && cd build"
echo "  cmake .. -DCMAKE_BUILD_TYPE=Release"
echo "  make -j\$(nproc)"
echo "  ./sicnu_geo_rs"
