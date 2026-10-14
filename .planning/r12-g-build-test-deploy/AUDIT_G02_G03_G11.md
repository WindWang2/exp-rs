# R12-G 只读审计报告 — G02 / G03 / G11

- 审计 worktree：`C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test`
- 分支：`hardening/r12-g-build-test-offline`（base = `28abef358`，工作树干净）
- 审计方式：纯只读静态审计 + 本地 python 复现验证（未做任何文件修改、未跑 cmake 构建）
- 行号基准：以上 worktree 当前文件内容（未改动）

---

## 1. 已核实事实

### G02 · 单进程测试退出的资源闭环

| 线索 | 结论 | 证据 |
|---|---|---|
| #1468「Windows injected main `_exit()` bypasses all static teardown」——对**共享** test main | **ALREADY_FIXED** | `tests/sicnu_test_main.cpp:64` 已在 `#ifdef _WIN32` 分支内 `return result;`，注释 `:53-63` 明确写了 "Return normally; never _exit" 并点名 `#1468`。非 Windows 分支 `:66` 亦为 `return`。 |
| 共享 main 是否真被所有 Catch2 目标使用 | **否** | `tests/CMakeLists.txt:21-22` 只在 `WIN32 AND TARGET Catch2WithMain` 时把 `Catch2WithMain` 的 SOURCES 换成 `sicnu_test_main.cpp`。另有 **20 个测试二进制 + 1 个 helper 自己定义了 `int main()`**（全部链接 `Catch2::Catch2` 而非 `Catch2WithMain`，已逐个核对 CMakeLists），因此**根本不经过 `sicnu_test_main.cpp`**。 |
| 这 21 处自定义 main 怎么退出 | **STILL_BROKEN** | 全部是 `#ifdef _WIN32 _exit( result ); #else return result;`（详见 §2 G02-1）。 |
| UTF-8 argv 处理 | **正确（未发现缺陷）** | `tests/sicnu_test_main.cpp:22-46`：`CommandLineToArgvW` → `WideCharToMultiByte(CP_UTF8)` → `utf8Args`/`utf8Argv` 均先 `reserve` 后填充（无重分配，指针稳定）→ `LocalFree(wArgv)`（`:46`）→ 传给 `Catch::Session().run(int, char**)` 的 argc = `utf8Argv.size()-1`（`:51`）正确。空串 `s[0]` 在 C++11 下合法。 |
| 其他 `_exit` / `ExitProcess` / `quick_exit` / `abort` 残留 | 见下 | `abort`：`tests/test_plugin_host_process.cpp:68`、`tests/fixtures/isolation_plugin/isolation_plugin.cpp:98`（有意故障注入，排除）。`_exit`：`tests/helper_external_process.cpp:106`（子进程度量 fixture，有意）、`tests/fixtures/isolation_plugin/isolation_plugin.cpp:323`（有意）。`std::_Exit`：`tests/support/offline_probe.h:254`（SKIP 路径，见 G02-3）。无 `ExitProcess` / `quick_exit`。 |
| `processScratchDir` 的清理机制 | 依赖静态析构 | `tests/support/process_scratch.h:61-70`：函数内局部 `static Cleanup cleanup{ root }`，靠析构 `remove_all`。**`_exit` 会跳过它。** 但全仓仅 `test_io_multidim.cpp:34`、`test_quality_mosaic_operator.cpp` 两个文件用 `processScratchDir`，二者都走 `Catch2WithMain`（即用修好的共享 main），**不与 `_exit` 交集** → 不构成缺陷。 |
| `qt_lifecycle.h` 的有序 teardown | **有效，覆盖完整** | `tests/support/qt_lifecycle.h:78-90` `orderlyTeardown()`（sendPostedEvents(DeferredDelete) → `exitQgis()` → `delete app`），由 `TeardownListener::testRunEnded`（`:97-112`）触发。引用 `qt_lifecycle.h` 的 49 个 cpp 中，48 个注册了 `CATCH_REGISTER_LISTENER`（唯一例外是 `sicnu_test_main.cpp`，它只是注释里提到）。→ 排除项。 |
| `scripts/cleanroom/**` | 仅 1 个文件 | `scripts/cleanroom/cleanroom_smoke.sh` 全流程 `set -eu`，只在 `:60`、`:70`、`:97` 写**固定**路径 `/tmp/cleanroom-verify.log` / `/tmp/cleanroom-env.log` / `/tmp/cleanroom-container.log`（非 pid 隔离，多实例并发会互相覆盖），`:68` 设 `HOME=/tmp`。属部署脚本，非测试进程闭环，列为 P2 观察项，不单列缺陷。 |

### G03 · 测试 HTTP/COG fixture 可取消与无挂死

| 关注点 | 结论 | 证据 |
|---|---|---|
| `recv` 是否有界 | **是**（4 个 fixture 全部） | `http_range_server.cpp:53-67` `boundSocketWait()` → `SO_RCVTIMEO=2s`；`http_s3_server.h:75-89` 同；`mini_cog_server.h:102-104` `SO_RCVTIMEO=30s`；`http_stac_server.cpp:207` 复用 `boundSocketWait`。 |
| `send` 是否有界 | **否，全仓 0 处** | `grep -rn "SO_SNDTIMEO\|SO_SENDTIMEO" tests/` → 空。见 G03-2。 |
| 无声客户端（连上不发数据） | **已修** | `http_range_server.cpp:237` + `:319-325`；`mini_cog_server.h:97-104`（注释直接引用 #1468）；`http_s3_server.h:191`、`http_stac_server.cpp:207`。 |
| `accept` 唤醒机制 | **range/stac 两个 fixture 的唤醒是死代码** | `http_range_server.cpp:94` 先 `shutdownSocket(mListener)`（= close），`:99-118` 再 self-connect 唤醒；`http_stac_server.cpp:106` / `:110-125` 同样顺序。**已实测：close 之后 connect 必然失败（Windows `10061 WSAECONNREFUSED`）**。对照 `http_s3_server.h:125-139`（先唤醒 → `:139` 再 close）顺序正确。见 G03-1。 |
| `bounded_wait.h` 是真有界还是假有界 | **真有界** | `waitUntil`（`:42-69`）：先测 `pred`，再测 `steady_clock` deadline，失败打印并返回 false；`TestGate::wait`（`:87-100`）用 `cv_.wait_until(deadline)` 且 `open()` 在 `mx_` 下 store 再 notify（`:107-114`），无丢通知窗口。→ 排除项。 |
| faultScript 注入 truncation 是否会挂 | **本身不会挂，但走 send 路径** | `http_range_server.cpp:651` `effective = !close && bodySize>8 ? 4 : bodySize`（12.0 已修「脚本注入 Truncated 不生效」问题，见 `:644-650` 注释）；`ShortRange :566-594`、`LongRange :521-565`、`ResetRanged :480-520` 都是先 `send` 再 return。send 无超时 → 见 G03-2。 |
| 析构 `join` | 有 join 但可能被无限阻塞 | `http_range_server.cpp:136-137`、`:149-151`；`http_stac_server.cpp:135-136`、`:147-149`；`http_s3_server.h:152-153`；`mini_cog_server.h:85-86`。 |

### G11 · 测试数据与临时目录隔离

| 关注点 | 结论 | 证据 |
|---|---|---|
| 每个测试是否有独立 root | **部分否** | 已修：`test_io_multidim.cpp:27-34`（`processScratchDir`，注释直接点名 "three cases share the cube9 scratch name … -j4"）。**未修**：`test_io_raster_contract.cpp:26-33`、`test_io_vector_contract.cpp:25-32` 仍是 `temp_directory_path()/ "sicnu_io_test_raster" / name`（无 pid）。见 G11-1。 |
| 公共环境变量污染不还原 | **进程内存在，跨进程无** | `exprs_test_env.h:66-73` `_putenv/setenv("SICNU_PLUGIN_USER_ROOT")` 且析构 `:96-100` 只删目录不还原原值；`offline_probe.h:108-128` 改写 `no_proxy/NO_PROXY` 不还原。均为进程本地（ctest 一 case 一进程），影响面限于「同一进程跑多 case」的直跑场景。 |
| HOME / QT_PLUGIN_PATH / QGIS_PREFIX_PATH / GDAL_DATA / PROJ_LIB / TMPDIR 是否被写脏不还原 | **未发现** | `SicnuTestEnv.cmake` 只在 ctest 进程层 set（`:196-270`），不改 HOME/TMPDIR；`test_mcp_server.cpp:1743-1759` 的 `SandboxEnvGuard` 完整 save/restore `SICNU_MCP_WORKSPACE`/`SICNU_MCP_ALLOW_REMOTE`/`HOME`/CWD（`:16-18`）。→ 排除项。 |
| Qt/QGIS 资源生命周期 | **已闭环** | 见 G02 表「`qt_lifecycle.h`」。 |
| GDAL 全局配置是否复原 | **否** | `grep -rn "CPLSetConfigOption" tests/` = 55 处；其中 `"GDAL_PAM_ENABLED","NO"` 36 处、`"GDAL_NUM_THREADS"` 5 处、`"SHAPE_ENCODING"` 5 处、`"AWS_SESSION_TOKEN","ambient"` 1 处 —— 全部**无复原**，全仓只有 1 个 ad-hoc RAII（`test_perf_operator_observatory.cpp:249-261` 保存/还原 `GDAL_CACHEMAX`）。见 G11-3。 |
| 临时文件是否清理 | **多数不清理** | 固定根（非 pid）的 scratch 只在开头 `remove_all`，进程退出不删 → `%TEMP%` 长期残留（有界，非无限增长）。见 G11-2。 |

---

## 2. 候选缺陷清单

### G02-1 · P1 · Windows 上 20 个测试二进制仍在 `_exit()`，绕过静态析构链

**根因**：#1468 只修了共享的 `sicnu_test_main.cpp`，没有修各文件自带的 `int main()`；这些目标链接 `Catch2::Catch2`（不带 main），所以永远拿不到修复。

**位置（21 处，全部形如 `#ifdef _WIN32  _exit( result );  #else  return result;`）**

```
tests/test_accuracy_panel_wiring.cpp:19
tests/test_active_view_host_data_context.cpp:100
tests/test_active_view_host_viewport.cpp:21
tests/test_colormap.cpp:44
tests/test_data_phase1_acceptance.cpp:101
tests/test_data_project_roundtrip.cpp:132
tests/test_fault_injection.cpp:214
tests/test_layer_adoption_safety_net.cpp:95
tests/test_log_panel_main.cpp:31
tests/test_paths.cpp:17
tests/test_project_context_reap.cpp:510
tests/test_python_engine.cpp:81
tests/test_python_plugin_host.cpp:253
tests/test_python_plugin_manager.cpp:99
tests/test_python_processing_provider.cpp:80
tests/test_raster_processing_dialog_base_main.cpp:42
tests/test_vector_layer_properties_lazy.cpp:73
tests/test_workflow_pipeline.cpp:16
tests/test_workspace_snapshot.cpp:29
tests/test_plugin_host.cpp:46
tests/helper_external_process.cpp:106   (子进程度量 fixture，退出码即被测对象 → 有意，建议豁免)
```

**其中 7 个同时持有 `static QTemporaryDir`（atexit 注册，被 `_exit` 跳过）→ 每次 Windows 运行遗留 1 个 %TEMP% 目录：**

| 文件 | `static QTemporaryDir` | `_exit` |
|---|---|---|
| `tests/test_active_view_host_data_context.cpp` | :37 | :100 |
| `tests/test_data_phase1_acceptance.cpp` | :42 | :101 |
| `tests/test_data_project_roundtrip.cpp` | :46 | :132 |
| `tests/test_layer_adoption_safety_net.cpp` | :33 | :95 |
| `tests/test_project_context_reap.cpp` | :38 | :510 |
| `tests/test_python_engine.cpp` | :33 | :81 |
| `tests/test_python_plugin_manager.cpp` | :51 | :99 |

（例：`test_data_project_roundtrip.cpp:46-47` `static QTemporaryDir dir; static QMap<QString,QString> cache;`，目录内会写 N 个合成 GeoTIFF。）

**附带后果**：`test_colormap.cpp:25`、`test_data_project_roundtrip.cpp:125`、`test_workspace_snapshot.cpp:23`、`test_log_panel_main.cpp:21` 的 `QgsApplication` 是 main 栈对象，`_exit` 使其析构函数永不执行（`test_colormap.cpp` 甚至没有 `exitQgis()`）。

**最小 RED 复现（无需任何编译）**

```bash
cd C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test
# 契约：tests/ 下除有意故障注入 fixture 外不得有裸 _exit 调用
grep -rn --include=*.cpp -E "^[[:space:]]*_exit[[:space:]]*\(" tests/ \
  | grep -v "tests/fixtures/isolation_plugin/" | grep -v "tests/helper_external_process.cpp"
# 期望：0 行
# 当前：20 行
```

**行为级 RED（只需构建 1 个二进制，非全量）**

```bash
before=$(ls "$TEMP" | wc -l)
ctest -R "test_data_project_roundtrip::" --output-on-failure   # 或 ctest -R "test_python_engine::"
after=$(ls "$TEMP" | wc -l)
# 期望：after - before == 0
# 当前：>= 1（QTemporaryDir 未被 removeRecursively）
```

**建议最小修复方向**（不实现）：把 21 处 `#ifdef _WIN32 _exit(result)` 统一改为 `return result;`；若某个二进制确实依赖「跳过 Qt 静态析构」才能不崩，按 `support/qt_lifecycle.h` 的契约改为「堆持有 app + `CATCH_REGISTER_LISTENER(TeardownListener)` + 正常 return」，而不是 `_exit`。可以给 `helper_external_process.cpp` 加显式豁免注释。

---

### G02-2 · P2 · `_exit` 不 flush stdio/iostream 缓冲，Windows 上重定向输出可能丢尾

**根因**：`_exit()` 不运行 `exit()` 的 stdio/iostream flush 阶段；`std::cout` 仅在静态析构（`std::ios_base::Init` 析构）时 flush，被 `_exit` 跳过。

**位置**：同 G02-1 的 20 处 `_exit`。

**置信度说明（不夸大）**：Catch2 v3 的 reporter 写 `Catch::cout()`；本报告**未能核实** Catch2 是否在 `testRunEnded` 后自行 flush（Catch2 未在本仓 vendor，`find . -name catch_session.hpp` 无结果）。若 Catch2 自带 `std::endl`/flush，此项退化为「仅测试自身 `printf/fprintf` 输出受影响」。请在修 G02-1 时顺带实测，不要单独立项。

**最小 RED（无需构建本仓）**

```bash
cat > %TEMP%/flushdemo.cpp <<'EOF'
#include <cstdio>
#include <cstdlib>
int main(int argc, char**) {
  std::printf("TAIL-MARKER-1234567890\n");
  if (argc > 1) return 0; else _exit(0);
}
EOF
cl /nologo /EHsc /Fe:%TEMP%/flushdemo.exe %TEMP%/flushdemo.cpp
%TEMP%/flushdemo.exe        > %TEMP%/a.txt   # _exit 分支
%TEMP%/flushdemo.exe keep   > %TEMP%/b.txt   # return 分支
find /c "TAIL-MARKER" %TEMP%/a.txt %TEMP%/b.txt
# 期望：两文件都能找到；_exit 分支在 stdout 为全缓冲（重定向到文件）时可能丢失
```

**建议修复方向**：随 G02-1 一起改为 `return`；若必须保留硬退出，至少在 `_exit` 前 `std::fflush(nullptr); std::cout.flush();`。

---

### G02-3 · P2 · `offline_probe.h` 的 `std::_Exit(77)` 在 SKIP 路径上跳过静态析构

**根因**：`skipWithReason()` 用 `std::_Exit(77)`（注释 `:241-243` 说明是刻意的），会跳过 `QSettingsUserRootRedirect`（`qsettings_redirect.h:31-43`，构造期即 `create_directories`）与 `UserRootRedirect`（`exprs_test_env.h:88-100`）的析构清理。

**位置**：`tests/support/offline_probe.h:254`（由 `:272`、`:275` 两条路径触发）。

**已核实的影响面（很小，勿过度修补）**：
- 注册 `SICNU_OFFLINE_GUARD()` 的 15 个文件（test_io_remote_range / test_io_range_cache / test_io_stac_client / test_cli_* / test_execution_benchmarks / test_provider_http / test_remote_source_cache / test_io_doctor / test_io_identity / test_io_remote_validator / test_surface_parity_r4 / test_cli_error_messages_r4）
- 使用 `QSettingsUserRootRedirect` 的 5 个文件（test_parity_*_r4）——**交集为 0**，即 SKIP 路径不会留下 qsettings scratch。
- 唯一残留影响：若 `SICNU_TRACE=1`，`sicnu_test_main.cpp:17` 装的 `FileTraceSink`（`src/runtime/observability/trace.cpp:272-278` 靠析构 join writer 线程）不会被析构 → ndjson 可能停在半行。**仅在显式开启 SICNU_TRACE 时可见。**

**最小 RED（无需构建）**

```bash
cd C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test
SICNU_FORCE_OFFLINE=1 SICNU_TRACE=1 SICNU_TRACE_DIR=%TEMP%\g02-3-trace \
  <已构建的 test_io_remote_range.exe> --list-tests   # 或直接跑一个 case
# 期望：退出 77，且 %TEMP%\g02-3-trace\*.ndjson 每行都是完整 JSON
# 当前：可能以半行结尾（FileTraceSink 未 join）
```

**建议修复方向**：把 `std::_Exit(77)` 换成「先做一次 `std::fflush(nullptr)` + 若有 trace sink 则显式 flush，再正常 `return` 出 main」；由于 guard 在 Catch 监听器里，最小改动是改为抛出一个 Catch2 级别的 skip 语义，或让 `skipWithReason` 只做 `fflush` + `std::_Exit` 前显式 `Trace::install(nullptr)`。

---

### G03-1 · P1 · `~HttpRangeServer` / `~HttpStacServer` 先 close 再 self-connect 唤醒 → 唤醒路径是死代码；POSIX 上析构 join 可挂死

**根因**：唤醒 accept 的 self-connect 必须在监听套接字仍然 listening 时发起；这两个析构函数先 `close` 了监听 fd，随后发起的 connect 必然 ECONNREFUSED，accept 永远收不到连接。POSIX 的 `close()` 不保证唤醒阻塞中的 `accept()`（这正是 `:95-98` 注释自己写下的前提），于是 `mThread.join()` 挂到 ctest 超时。

**位置**

- `tests/support/http_range_server.cpp:94` `shutdownSocket( mListener );`（= close）
  vs `:99-118` 之后的 wake connect（`:108` `::connect( wake, …, mPort )`）
  → `:136-137` `if ( mThread.joinable() ) mThread.join();`
- `tests/support/http_stac_server.cpp:106` `shutdownSocket( mListener );`
  vs `:110-125` wake connect（`:119`）
  → `:135-136` `mThread.join();`
- 对照组（**顺序正确**）：`tests/support/http_s3_server.h:125-138` 先 connect 唤醒，`:139` `s3CloseSocket( mListener )` 后 close。

**已实测证明（本机 python，无文件写入）**

```
connect while listening: 0
connect AFTER close (rc): 10061        # WSAECONNREFUSED
accept after close on Windows: error 10038 (WSAENOTSOCK), thread not blocked
```

结论：Windows 上 `closesocket()` 本身会唤醒 `accept()`（10038），所以 **Windows 掩盖了这个缺陷**；**Linux/POSIX 上不会**，唤醒又是死代码 → 挂死。

**触发条件（很常见）**：任何「构造 fixture 后、首个请求到达前」就析构的场景：
- 用例里第一个 `REQUIRE`（如 `REQUIRE(server.valid())`）失败，Catch2 抛异常 → 栈展开 → 析构；
- 用例只取 `url()` 但因别的前置条件 SKIP/提前 return；
- `ServerBehavior::Slow` 等路径上客户端在超时后放弃。
这些都会把一个**快速失败**变成 **ctest 超时**。

**最小 RED（纯 python，无需构建）**

```bash
python -c "
import socket,time
s=socket.socket(); s.bind(('127.0.0.1',0)); s.listen(4); p=s.getsockname()[1]
w=socket.socket(); print('wake while listening:', w.connect_ex(('127.0.0.1',p))); w.close()
s.close(); time.sleep(0.2)
w2=socket.socket(); print('wake AFTER close:', w2.connect_ex(('127.0.0.1',p))); w2.close()
"
# 期望(正确实现)：两次都是 0
# 当前(close-then-wake)：第二次 10061 / ECONNREFUSED → 唤醒失效
```

**C++ 级 RED（只编 2 个文件，非全量 cmake；需在 Linux 上跑，Windows 不会复现）**

```bash
cat > /tmp/g03_1.cpp <<'EOF'
#include "support/http_range_server.h"
int main() {
  std::vector<unsigned char> p(1024, 0x41);
  { sicnu::geo::testsupport::HttpRangeServer s(p); }   // 构造后立刻析构，无任何请求
  std::puts("destroyed OK");
}
EOF
g++ -std=c++17 -I tests /tmp/g03_1.cpp tests/support/http_range_server.cpp -o /tmp/g03_1 -pthread
timeout 20 /tmp/g03_1; echo "rc=$?"
# 期望：0 + "destroyed OK"
# 当前(Linux)：timeout → rc=124（join 挂死）
```

**建议最小修复方向**：把 `http_range_server.cpp:94` 与 `http_stac_server.cpp:106` 的 `shutdownSocket(mListener)` 挪到各自的 wake connect **之后**（对齐 `http_s3_server.h:125-139`）；更稳的替代是监听端改用 `poll(listener, timeoutMs)` 循环 + `mStop` 标志，彻底不依赖 close/connect 唤醒。

---

### G03-2 · P1 · 全仓 0 处发送超时：客户端「发完请求头但从不读」会让 `send()` 无限阻塞 → 析构 join 永久挂死

**根因**：4 个 fixture 都只设了 `SO_RCVTIMEO`，没有 `SO_SNDTIMEO`。TCP 发送缓冲区 + 对端接收窗口填满后，阻塞式 `send()` 永久阻塞；此时 `handleConnection` 不返回，`serveLoop` 回不到 `while(!mStop)`，析构的 `join()` 永久等待（析构里对 in-flight 的 `::shutdown` 是在**另一个**线程发起，不能保证唤醒已阻塞在 `send()` 的线程）。

**位置（发送循环，均无超时）**

- `tests/support/http_range_server.cpp:633-641`（响应头）、`:654-661`（响应体）
- `tests/support/http_range_server.cpp:498-509`（ResetRanged）、`:536-562`（LongRange）、`:580-591`（ShortRange）
- `tests/support/http_s3_server.h:333-345` `sendAll()`
- `tests/support/http_stac_server.cpp:67-78` `sendAll()`，调用点 `:292`、`:306-307`
- `tests/support/mini_cog_server.h:29-39` `sendAll()`，调用点 `:177`、`:188`

**已核实**：`grep -rn "SO_SNDTIMEO\|SO_SENDTIMEO" tests/` → 无任何匹配。

**最小 RED（只编 1 个 fixture + 1 个 driver；Windows/Linux 均可复现）**

```bash
# driver：8 MiB payload，启动后等客户端连上，然后析构
cat > %TEMP%/g03_2.cpp <<'EOF'
#include "support/http_range_server.h"
#include <cstdio>
#include <thread>
int main() {
  std::vector<unsigned char> p(8u*1024*1024, 0x41);
  sicnu::geo::testsupport::HttpRangeServer s(p);
  std::printf("PORT=%d\n", s.port()); std::fflush(stdout);
  std::this_thread::sleep_for(std::chrono::seconds(15));   // 等 python 客户端
  std::puts("destroying"); std::fflush(stdout);
  return 0;   // 析构 → join
}
EOF
cl /nologo /EHsc /std:c++17 /I"tests" %TEMP%/g03_2.cpp tests\support\http_range_server.cpp /Fe:%TEMP%/g03_2.exe ws2_32.lib

# 无声读客户端：发完请求头后 60s 不读任何字节
python -c "
import socket,time,sys
c=socket.create_connection(('127.0.0.1',<PORT>))
c.sendall(b'GET /fixture.tif HTTP/1.1\r\nHost: x\r\n\r\n')
time.sleep(60)   # 从不 recv
"
# 期望：服务端最迟 N 秒后放弃并正常析构退出
# 当前：服务端阻塞在 send()，析构 join 无限等待 → ctest TIMEOUT
```

**建议最小修复方向**：在 `boundSocketWait()`（`http_range_server.cpp:53-67`）里同时设 `SO_SNDTIMEO`（Windows `DWORD` ms / POSIX `timeval`），并让所有 `send` 循环在 `written <= 0` 时区分 `EAGAIN/EWOULDBLOCK`（超时放弃）与真错误；`http_s3_server.h:75-89`、`mini_cog_server.h:102-104` 同步加发送超时。

---

### G03-3 · P2 · `http_stac_server` 的 Content-Length 读体循环「每次有界、整体无界」（假有界）

**根因**：`:58-64` 的循环条件是 `body.size() < contentLength && body.size() < 1 MiB`，每次 `recv` 各有 2 s `SO_RCVTIMEO`，但**没有跨整个请求的总 deadline**。慢速滴灌客户端（每 2 s 送 1 字节）可把 handler 钉住 `256 × 2 s`（4096 B 缓冲）到更久。

**位置**：`tests/support/http_stac_server.cpp:58-64`（`readRequest`），配合 `:33` 的头部上限 `sizeof(buffer)*4 = 16 KiB`。

**最小 RED**：同 G03-2 形态的 python 客户端 —— 声明 `Content-Length: 1048576`，随后每 3 s 发 1 字节；观察 handler 迟迟不返回（服务端 requestCount 不增、下一连接不被受理）。

**建议修复方向**：在 `readRequest` 入口记录 `steady_clock` deadline（如 5 s），把 deadline 折算成剩余毫秒动态设 `SO_RCVTIMEO`，超时即 return。

---

### G03-4 · P2 · 析构里对「已拷贝的 in-flight fd」做 `::shutdown()` 存在 fd 复用 TOCTOU

**根因**：`mInFlight` 在锁内拷贝、锁外 `shutdown()`；拷贝与 `shutdown` 之间，拥有者线程可能已 `erase` + `close` 该 fd，fd 号被进程内其他组件（GDAL 打开文件、新 socket）复用 → `shutdown()` 打到无关对象上。这与 `:121-124` 注释里「close 会打到被复用的 fd，所以只有拥有者线程才 close」的自辩**自相矛盾**：`shutdown` 有同样的 fd 复用风险。

**位置**

- `tests/support/http_range_server.cpp:127-135`
- `tests/support/http_stac_server.cpp:126-134`
- `tests/support/http_s3_server.h:143-151`

**最小 RED**：静态审计即可定位（拷贝在 `:128-131` / `:129-130` / `:144-148`，`shutdown` 在 `:133-134` / `:132-133` / `:149-150`，中间无引用计数保护）。运行级复现需要在 handler 完成瞬间并发析构，属竞态，建议用 TSAN 或压力循环构造。

**建议修复方向**：给每个 connection 一个「关闭令牌」（`std::shared_ptr` 或一个自增 generation），`shutdown` 前校验 generation 未变；或改用 ` dup` 持有 / 拥有者线程自己在收到 mStop 后 shutdown。

---

### G03-5 · P2 · `MiniCogServer::stop()` 先 close 再 join，accept 端有 fd 复用竞态，且缺少 self-connect 唤醒

**根因**：`stop()`（`:78-87`）`::shutdown(m_listenFd, SHUT_RDWR)` + `::close(m_listenFd)` + `m_listenFd = -1` 之后才 `join()`；而 `acceptLoop`（`:90-108`）在 `:94` 读 `m_listenFd` 后就阻塞在 `::accept`。close 与 accept 之间没有同步，accept 可能作用在已被复用的 fd 号上；POSIX 下 close 也不保证唤醒 accept。与 `http_*` 三个 fixture 相比，它连 self-connect 唤醒都没有。

**位置**：`tests/support/mini_cog_server.h:82-86`（stop）与 `:94`（accept）、`:92`（`while(true)` 无 `mStop` 标志）。

**附加**：`:92` 的 `while ( true )` 没有停止标志，完全依赖 `accept` 返回错误退出；`acceptLoop` 每次只服务一条连接（`serveConnection` 末尾 `:193` 无条件 `return`），串行。

**最小 RED**：Windows 上 python 已验证 close 会唤醒 accept（10038），因此 Windows 不复现挂死；Linux 上按 G03-1 的 C++ driver 形态（构造 `MiniCogServer` 后立即析构）用 `timeout 20` 观察 rc=124。

**建议修复方向**：加 `std::atomic<bool> mStopping`，`acceptLoop` 用 `poll(m_listenFd, 200ms)` 循环并在 `mStopping` 时退出；`stop()` 里 `mStopping = true` → join → 再 close。

---

### G03-6 · P2 · 并发模式下 `mHandlers` 无回收，随连接数线性增长

**根因**：`mLiveHandlers` 只限制**同时存活**的 handler 数（≤8），但每个连接都 `mHandlers.push_back(std::move(handler))`，且这些 `std::thread` 对象只在析构时 join。长跑/压测用例（如 concurrency stress）会产生成百上千个 thread 对象和已结束线程的 joinable 槽位。

**位置**：`tests/support/http_range_server.cpp:247-260`（push_back 在 `:259`）；`tests/support/http_stac_server.cpp:218-230`（push_back 在 `:230`）。

**最小 RED**：静态审计定位即可；运行级需一个发起 >1000 连接的用例，观察 RSS/句柄线性上升。

**建议修复方向**：定期回收 `joinable()==false` 的槽位，或改用一个固定大小的线程池 + 任务队列。

---

### G11-1 · P1 · `test_io_raster_contract` / `test_io_vector_contract` 仍用非 pid 固定临时根，且同二进制内多个 TEST_CASE 复用同一 scratch 名 → `-j4` 并行互相删文件

**根因**：`scratchDir()` 用 `temp_directory_path() / "<固定名>" / name`，没有 pid 分量；而同一二进制里多个 TEST_CASE 使用了同一个 `name`。ctest 的 PRE_TEST 发现模式把每个 case 变成独立进程，`-j4` 下并行执行 → 后启动的 case 的 `remove_all(dir)` 会删掉先启动 case 正在写的文件。这正是 `process_scratch.h:5-12` 注释记录的 #1414/#1392 类故障；`test_io_multidim.cpp:34` 已改用 `processScratchDir` 修好，这两个文件没有。

**位置**

- `tests/test_io_raster_contract.cpp:26-33`（根 = `sicnu_io_test_raster`，`:30` `fs::remove_all( dir, ec )`）
  - `"budget"`：`:137`（TEST_CASE :135）、`:480`（TEST_CASE :475）、`:503`（TEST_CASE :501）→ **3 个 case 同名**
  - `"blocks"`：`:337`（TEST_CASE :330）、`:524`（TEST_CASE :522）→ **2 个**
  - `"mask874"`：`:583`（TEST_CASE :580）、`:610`（TEST_CASE :607）→ **2 个**
- `tests/test_io_vector_contract.cpp:25-32`（根 = `sicnu_io_test_vector`，`:30` `fs::remove_all`）
  - `"move850"`：`:336`（TEST_CASE :333）、`:355`+`:356`（TEST_CASE :352）、`:381`（TEST_CASE :376）、`:410`（TEST_CASE :403）→ **4 个 case 同名**
- 两者在 CMake 里都无 `RUN_SERIAL`：`tests/CMakeLists.txt:417-418` `sicnu_add_io_test(...)`（该函数体不含 `RUN_SERIAL`）。

**最小 RED（无需构建本仓；并行语义的等价模拟）**

```bash
python -c "
import os,tempfile,threading,time,shutil
root=os.path.join(tempfile.gettempdir(),'sicnu_io_test_raster','budget')
def case(k):
    for _ in range(200):
        shutil.rmtree(root, ignore_errors=True); os.makedirs(root, exist_ok=True)
        p=os.path.join(root,'big.tif')
        try:
            with open(p,'wb') as f: f.write(b'x'*8192)
            time.sleep(0.002)
            with open(p,'rb') as f: assert len(f.read())==8192
        except (FileNotFoundError,AssertionError) as e:
            print('COLLISION in case',k,type(e).__name__); return
ts=[threading.Thread(target=case,args=(i,)) for i in range(3)]
[t.start() for t in ts]; [t.join() for t in ts]
"
# 期望：无输出
# 当前：至少一个 "COLLISION"
```

**真实 RED（需构建 2 个二进制）**

```bash
ctest -R "^test_io_raster_contract::" -j4 --repeat until-fail:3 --output-on-failure
ctest -R "^test_io_vector_contract::" -j4 --repeat until-fail:3 --output-on-failure
# 期望：稳定通过
# 当前：间歇性 "Cannot open raster" / 文件不存在（#1414 同款）
```

**建议最小修复方向**：把这两个文件的 `scratchDir()` 换成 `sicnu_test::processScratchDir( "sicnu_io_test_raster", name )` / `( "sicnu_io_test_vector", name )`（照抄 `test_io_multidim.cpp:27-34`，只需 include `support/process_scratch.h`），或在 CMake 里给这两个目标加 `RUN_SERIAL TRUE`。前者更彻底（同时顺带解决 G11-2 的残留）。

---

### G11-2 · P2 · 大量固定根临时目录不在退出时清理，`%TEMP%` 长期残留

**根因**：固定根 scratch 只在开头 `remove_all`，进程退出不删。数量有界（按名字去重），但会永久驻留。

**位置（抽样，共约 30+ 处）**

```
tests/benchmark_io.cpp:44                      sicnu_io_benchmark
tests/test_adversarial_m3.cpp:28               sicnu_adv_m3
tests/test_agent_loop_journal_durability.cpp:40  sicnu_journal_durability
tests/test_cli_agent_ops.cpp:221               cli_agent_ops_reconcile
tests/test_contract_census_11.cpp:228,249      sicnu_census11_*
tests/test_contract_fuzz_payload.cpp:983       sicnu-fuzz-manifest
tests/test_env_doctor.cpp:234                  sicnu-envcheck-fixture-f19
tests/test_exprs_external_process_win.cpp:234  sicnu-ep-policy
tests/test_io_cog.cpp:28 / test_io_cog_options.cpp:41 / test_io_doctor.cpp:29
tests/test_io_fidelity.cpp:31 / test_io_atomic_failures.cpp:30 / test_io_finalize_manifest.cpp:40
tests/test_io_fabric_*.cpp (8 个文件)           sicnu_fabric_tests
tests/test_io_raster_contract.cpp:28 / test_io_vector_contract.cpp:27
```

**最小 RED（无需构建）**

```bash
ls "$TEMP" | grep -i "^sicnu\|^cli_agent_ops\|^agentbench" | wc -l
# 跑一次 ctest 后再统计，期望不增长
```

**建议修复方向**：随 G11-1 一起改成 `processScratchDir`（其 `static Cleanup` 在退出时 `remove_all`）。

---

### G11-3 · P2 · GDAL 全局配置（CPLSetConfigOption）在测试中几乎从不复原，且没有共享 RAII

**根因**：`CPLSetConfigOption` 是进程全局配置；测试里 55 处调用，绝大多数「setup 时设、析构时不还原」。ctest 一 case 一进程掩盖了它，但「同一进程跑多 case」的直跑/调试场景（`./test_x`）会出现用例间相互污染，且用例顺序会改变结果 —— 与 R12 的确定性目标冲突。

**位置（统计，非穷举）**

- `"GDAL_PAM_ENABLED","NO"` × 36（如各 io/算子测试开头）
- `"GDAL_NUM_THREADS","4"/"8"/nullptr` × 5
- `"SHAPE_ENCODING","UTF-8"/nullptr` × 5
- `"AWS_SESSION_TOKEN","ambient"/nullptr` × 2
- `"SICNU_DATA_DIR","/nonexistent-f19/数据目录"/nullptr` × 2
- `"GDAL_HTTP_CHUNK_SIZE","16"` × 1、`"GDAL_CACHEMAX"` × 2
- 唯一做了保存/还原的：仅 `tests/test_perf_operator_observatory.cpp:249-261`（ad-hoc，只覆盖 `GDAL_CACHEMAX`）

**最小 RED（无需构建）**

```bash
cd C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test
grep -rn --include=*.cpp "CPLSetConfigOption" tests/ | wc -l                 # 55
grep -rn --include=*.h   -E "class .*(GdalConfig|ConfigGuard)|struct .*(GdalConfig|ConfigGuard)" tests/ | wc -l   # 0（无共享 RAII）
# 期望：存在一个共享 RAII 且 CPLSetConfigOption 裸调用数显著下降
```

**建议修复方向**：在 `tests/support/` 新增一个 Qt-free 的 `ScopedGdalConfig{name,value}` RAII（构造保存 `CPLGetConfigOption` 旧值、析构复原），把 `test_perf_operator_observatory.cpp:249-261` 的 ad-hoc 实现提上去，然后按需替换高频的 `GDAL_PAM_ENABLED` / `GDAL_NUM_THREADS` 调用点。

---

## 3. 明确排除项（看起来可疑，但核实后**不是**缺陷，请勿重复修补）

| # | 项 | 排除理由 |
|---|---|---|
| E1 | `sicnu_test_main.cpp` 的 Windows `_exit` | **ALREADY_FIXED**。`:64` 已是 `return result;`；`:53-63` 注释完整记录了 #1468 的因果。不要「再修一遍」。 |
| E2 | `sicnu_test_main.cpp` 的 UTF-8 argv 转换 | 正确：`reserve` 保证 `&s[0]` 指针稳定、`LocalFree` 已调用、argc 换算 `size()-1` 正确、空串 `s[0]` 合法。 |
| E3 | `process_scratch.h:70` 的 `static Cleanup` 与 `_exit` 的冲突 | 全仓只用 `processScratchDir` 的 2 个文件（`test_io_multidim.cpp:34`、`test_quality_mosaic_operator.cpp`）都走 `Catch2WithMain`→ 修好的共享 main，**与 20 个 `_exit` 二进制零交集**。真正该修的是 G02-1，不是这里。 |
| E4 | `support/bounded_wait.h` 是否「假有界」 | **真有界**。`waitUntil` 有 `steady_clock` deadline 并打印诊断；`TestGate::wait` 用 `wait_until` 且 `open()` 在互斥下 store 再 notify（`:107-114`），无丢通知窗口。 |
| E5 | `qt_lifecycle.h` teardown 是否覆盖不全 | 覆盖完整：49 个引用者中 48 个注册了 `TeardownListener`；`orderlyTeardown()` 的顺序（DeferredDelete → `exitQgis()` → delete app）与 `src/app/main.cpp` 一致。 |
| E6 | `offline_probe.h:254` 的 `std::_Exit(77)` 是否会留下 qsettings scratch | **不会**。注册 `SICNU_OFFLINE_GUARD()` 的 15 个文件与使用 `QSettingsUserRootRedirect` 的 5 个文件**交集为 0**。仅剩 `SICNU_TRACE=1` 下的半行 ndjson（已列为 G02-3，P2）。 |
| E7 | `test_mcp_server.cpp` 改 `HOME` 不还原 | **有还原**：`SandboxEnvGuard`（`:1743-1759`）在析构里恢复 `HOME`、`SICNU_MCP_WORKSPACE`、`SICNU_MCP_ALLOW_REMOTE` 与 CWD。 |
| E8 | `exprs_test_env.h` / `qsettings_redirect.h` 的 env 污染跨进程 | 不会。两者都是**进程本地** env 写入（`_putenv`/`setenv`）+ pid 唯一 scratch；ctest 每个 case 独立进程，无跨进程污染。同进程内不还原只影响「直跑整个二进制」场景（已并入 G11-3 同类，不单列）。 |
| E9 | `cmake/SicnuTestEnv.cmake` 污染 QT_PLUGIN_PATH / GDAL_DRIVER_PATH / PYTHONHOME | 这些是**有意**的 harness pin（`#730`/`#1355`），作用域是 ctest 进程层（`CTestCustom.cmake`）与 per-test `ENVIRONMENT_MODIFICATION`，不写脏用户 shell，也不改 HOME/TMPDIR。 |
| E10 | `http_s3_server.h` 的析构顺序 | **正确**（`:125-138` 先唤醒 → `:139` 再 close）。它是 G03-1 的修复模板，不要动。 |
| E11 | `test_io_multidim.cpp` 的 `cube9` 重名 | **ALREADY_FIXED**：`:34` 已用 `processScratchDir`，`:29-33` 注释说明就是为修这个。不要重复改。 |
| E12 | `mini_cog_server.h` 的 30 s recv 窗口 | 那是 #1468 的既有修复（`:97-104`），有效。G03-5 说的是 **accept 侧**与 **send 侧**，不是 recv。 |
| E13 | `test_plugin_host_process.cpp:68` `std::abort()`、`fixtures/isolation_plugin/isolation_plugin.cpp:98/323` | 有意的故障注入/崩溃注入 fixture（被测对象就是崩溃语义），不是退出路径缺陷。 |
| E14 | `scripts/cleanroom/cleanroom_smoke.sh` 的 `/tmp/cleanroom-*.log` 固定路径 | 部署验收脚本，串行使用；仅在多实例并发时轻微互相覆盖。列入 G11-2 的同类观察即可，不单独立项。 |

---

## 4. 优先级建议（若只做一件事）

1. **G02-1**（20 处 `_exit` → `return`）：纯机械替换，直接终结 #1468 的剩余面，且是 G02-2 的前提。
2. **G03-1**（range/stac 析构 close-then-wake 顺序）：3 行移动，消除 Linux CI 上「快速失败变 ctest 超时」的挂死。
3. **G11-1**（两个 io contract 测试改用 `processScratchDir`）：照抄 `test_io_multidim.cpp:27-34`，同时解决并行碰撞与 G11-2 残留。
4. **G03-2**（补 `SO_SNDTIMEO`）：改动集中在 `boundSocketWait()` 及 4 个 `sendAll`，是「无挂死」承诺的最后一块缺口。
