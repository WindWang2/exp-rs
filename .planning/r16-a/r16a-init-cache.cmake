# R16-A worktree 构建初始化缓存(cmake -C)。
# GDAL/PROJ/GEOS 钉到 paleo-singlefactor-closure 的共享只读 vendored 前缀
# (pw-seismic-runtime-closure),系统无这些包;其余依赖走系统。
set(VENDOR_USR /home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr)
# 前缀的 lib/x86_64-linux-gnu/cmake 下有 gdal/GEOS/proj 的 config 包,
# 进 CMAKE_PREFIX_PATH 让 find_package(PROJ/GEOS) 直接命中。
set(CMAKE_PREFIX_PATH ${VENDOR_USR}/lib/x86_64-linux-gnu CACHE STRING "vendored config packages")
set(PROJ_DIR ${VENDOR_USR}/lib/x86_64-linux-gnu/cmake/proj CACHE PATH "vendored proj config")
set(GEOS_DIR ${VENDOR_USR}/lib/x86_64-linux-gnu/cmake/GEOS CACHE PATH "vendored geos config")
set(GSL_INCLUDE_DIR ${VENDOR_USR}/include CACHE PATH "vendored gsl headers")
set(GSL_LIBRARY ${VENDOR_USR}/lib/x86_64-linux-gnu/libgsl.so CACHE FILEPATH "vendored gsl lib")
set(GSL_CBLAS_LIBRARY ${VENDOR_USR}/lib/x86_64-linux-gnu/libgslcblas.so CACHE FILEPATH "vendored gslcblas lib")
set(NETCDF_INCLUDE_DIR ${VENDOR_USR}/include CACHE PATH "vendored netcdf headers")
set(NETCDF_LIBRARY ${VENDOR_USR}/lib/x86_64-linux-gnu/libnetcdf.so CACHE FILEPATH "vendored netcdf lib")
set(GDAL_INCLUDE_DIR ${VENDOR_USR}/include/gdal CACHE PATH "vendored gdal headers")
set(GDAL_LIBRARY ${VENDOR_USR}/lib/x86_64-linux-gnu/libgdal.so CACHE FILEPATH "vendored gdal lib")
set(GEOS_INCLUDE_DIR ${VENDOR_USR}/include CACHE PATH "vendored geos headers")
set(GEOS_LIBRARY ${VENDOR_USR}/lib/x86_64-linux-gnu/libgeos_c.so CACHE FILEPATH "vendored geos_c lib")
set(PROJ_INCLUDE_DIR ${VENDOR_USR}/include CACHE PATH "vendored proj headers")
set(PROJ_LIBRARY ${VENDOR_USR}/lib/x86_64-linux-gnu/libproj.so CACHE FILEPATH "vendored proj lib")
set(CMAKE_BUILD_TYPE Debug CACHE STRING "")
set(ENABLE_TESTS ON CACHE BOOL "")
set(ENABLE_LOCAL_BUILD_SHORTCUTS ON CACHE BOOL "")
# github.com 网络间歇不可达(系统 git 代理 127.0.0.1:7890 断开,直连也失败):
# Catch2 用本机已有的 v3.7.1 源码树(与 CMakeLists.txt GIT_TAG v3.7.1 精确一致),
# pybind11 FetchContent 是可选绑定,跳过(不影响 verifier/grader/faultlab 测试目标)。
set(FETCHCONTENT_SOURCE_DIR_CATCH2 /home/kevin/bld-por/_deps/catch2-src CACHE PATH "local catch2 v3.7.1")
set(SICNU_LAB_SKIP_PYTHON_BINDINGS ON CACHE BOOL "no network fetch for pybind11")
