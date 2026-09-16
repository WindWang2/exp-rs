# EVIDENCE — cloud-data-fabric-11

每条证据：日期 | 命令 | exit | 关键输出摘要 | 结论。

## Phase 0（2026-09-16）

- `git fetch origin --prune; git rev-parse origin/master` → exit 0，`a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
  （prompt 快照 ebcafb4d 已过期：#991/#992 已合并，新增 #1000）。
- `gh pr list --state open` → #1009（execution-runtime-convergence-11，UNSTABLE）、#1008（radiometric-spectral-workbench，DIRTY）；
  两者 changed files 与本 track 业务域 **零交集**（详见 PARALLEL_OWNERSHIP.md）。
- `gh issue list --state open` → #1001–#1007，全部为 io/workflow/dataset/agent/georef 域 R2 残留，
  无 fabric/multidim/cache/mirror/CLI-data 域 issue → 全部 OUT_OF_SCOPE。
- Subagent #1 只读缺口审计完成（存档 `audit/subagent1-gap-audit.md`）：确认 WP A–G 的真实缺口与
  15 项 sharp edges，全部带 file:line 证据。
- `git worktree add ../exp-rs-cloud-data-fabric-11 -b zcode/cloud-data-fabric-11 origin/master` → exit 0 @ a5b11b7f。
- `git check-ignore -v .planning/cloud-data-fabric-11/GOAL.md` → 白名单生效（md 可跟踪，非 md 忽略）。

## 构建环境（本机事实，Windows + MSVC 2022 + Ninja）

- cmake/ninja/cl 均不在 Git Bash PATH。配方（`build-dev/track-cmd.cmd`，D-1106）：
  vcvars64 + `C:\Qt\Tools\Ninja` + `C:\deps\winflexbison` 上 PATH；
  `-DCMAKE_PREFIX_PATH=C:/deps/Qt/6.8.0/msvc2022_64;…/build-dev/vcpkg_installed/x64-windows;C:/deps/qca-install;C:/deps/kc-install`；
  `-DFETCHCONTENT_SOURCE_DIR_CATCH2=C:/deps/catch2-src`；`-DProtobuf_DIR=…/share/protobuf`。
- vcpkg manifest（63 包）：`C:/deps/vcpkg/vcpkg.exe install --triplet x64-windows` → exit 0，
  13s（本机二进制缓存全部命中），GDAL 3.12 等就位于 `build-dev/vcpkg_installed/x64-windows`。
- 配置排障记录：①`CMAKE_FIND_PACKAGE_PREFER_CONFIG=ON` 会破坏 QGIS FindSqlite3（"Unconsistency"）→ 改用
  逐包 `-DProtobuf_DIR`；②root CMakeLists.txt:129 模块式 `find_package(Protobuf REQUIRED)` 先建
  CMake 模块目标、后 OpenCV find_dependency 再含 vcpkg config → "Some (but not all) targets already
  defined"（缺 libupb）→ **root CMakeLists.txt:129 改为 `find_package(Protobuf CONFIG REQUIRED)`（一行）**。
- 最终 configure：`Configuring done (521.1s)` + `Generating done (93.3s)`，build.ninja 生成。
- 主仓库无现成 build 目录；兄弟 track（advanced-insar-platform-11）工具链路径被本 track 复用（只读其
  CMakeCache 获取路径，不写入其目录）。

## WP A/WPB 代码落地（Phase 1 提前开始，Phase 0 收尾期间）

- 新增 `src/geospatial/remote/vsi_object_identity.{h,cpp}`（VSI-stack HEAD 探针，remote 层，避免
  fabric→remote 反向依赖）；注册进 src/geospatial/CMakeLists.txt。
- `fabric/object_store.{h,cpp}`：`CanonicalObjectKey/canonicalObjectKey/isObjectStoreVsiPath/
  probeObjectStoreIdentity/objectStoreIdentityToken/objectStoreCredentialContext/fabricAssetIdentity`；
  `fabricCachedPath` 扩展（对象路径包装 + scheme 拼写先归一）；ScopedObjectStoreCredentials 窗口
  安装/清除 range cache 凭据上下文（D-1102）。
- `remote/range_cache.{h,cpp}`：VSI-object 条目模式（vsiPath + credentialContext 键成分、
  VSIGetFileMetadata 身份探针与 ETag 比对失效、`fetchRangeVsi`、fallback 重开原 /vsi* 拼写、
  Stat 双模式、`setRangeCacheCredentialContext`）。
- fabric 四个 identity 调用方（virtual_cube/mirror/prefetch/query_planner）改走 `fabricAssetIdentity`。

## Phase 1/2 测试证据（2026-09-16）

- `test_io_fabric_identity_11.exe`（WP A/B 契约）→ exit 0，**66 assertions / 5 cases 全绿**：
  拼写收敛、credential-free index key、主体指纹分离、真实 loopback S3 ETag 探针、
  跨拼写 ri1 token 收敛、offline 零请求、包装路径真实读、A/B 账号缓存隔离。
- `test_io_fabric_replay_11.exe`（WP C）→ exit 0，**36 assertions / 2 cases 全绿**：
  **Oracle 1（forced-offline 零网络重放，requestCount 全程不变）**、byte-equal 重放、
  离线索引、诚实 miss、损坏 chunk 拒绝、过期控制。
- 踩坑记录（供后续 track 参考）：
  1. `build-dev` 根的 DLL 是运行时依赖（含自产 sicnu_runtime.dll）——清理误删导致
     0xC0000135；用 ninja 目标重建即恢复。
  2. vcpkg 工具链 + `VCPKG_MANIFEST_INSTALL=OFF` + `VCPKG_APPLOCAL_DEPS=ON` 为本机
     可靠形态（applocal 逐 exe 部署）；纯 PATH 方式对 ctest 的 catch_discover_tests
     启动探测不稳。
  3. 多次更换 toolchain 的 reconfigure 会让 qgis_core 的 AUTOMOC 状态损坏
     （moc_*.cpp 缺失）→ 删 `src/core/qgis_core_autogen` 重建即愈。
