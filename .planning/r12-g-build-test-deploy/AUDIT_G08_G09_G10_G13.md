# AUDIT_G08_G09_G10_G13 — 只读审计（构建 / 打包 / 脚本回归）

- 审计者：general-purpose-2（只读，未修改仓库内任何文件）
- 目标 worktree：`C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test`（分支 `hardening/r12-g-build-test-offline`）
- 宿主环境：Windows 11 / Python 3.13.14 / Git-Bash；**PowerShell 7.6.5**（PowerShell 工具），同时存在 Windows PowerShell 5.1（`C:\WINDOWS\System32\WindowsPowerShell\v1.0\powershell.exe`，实测 `ConvertFrom-Json` 产出 `Int32`）
- 夹具位置：`C:\Users\wangj.KEVIN\AppData\Local\Temp\r12g-audit\`（worktree 外）
- 退出码语义（契约）：`0` 通过 / `1` 已验证但失败 / `2` 无法验证

> 说明：所有"真实输出"均来自本机实际执行。凡未执行的项一律放第 4 节 NOT_RUN，不做推断填充。
> G09-1 / G09-2 两项同时命中 G08 与 G09，只在第一处完整展开，另一处交叉引用。

---

## 1. 已核实事实（文件:行号 + 代码片段 / 实测输出）

### 1.1 G09 · 规范验证器与契约

`scripts/verify_bundle_manifest.py` 是唯一权威验证器（文件头第 2-8 行自述）。

- 逐文件 SHA256：`files[]` 每项带 `bytes` + `sha256`，`scripts/verify_bundle_manifest.py:158-167`
  ```python
  total += size
  if size != entry.get("bytes"):
      bad.append("size mismatch %s: %s != %s" % (rel, size, entry.get("bytes")))
      continue
  ...
  if digest != entry.get("sha256"):
      bad.append("sha256 mismatch %s" % rel)
  ```
- **流式**哈希：`scripts/verify_bundle_manifest.py:37-42`（1 MB 分块 `iter`）
  ```python
  def _digest(path):
      h = hashlib.sha256()
      with open(path, "rb") as fh:
          for chunk in iter(lambda: fh.read(1024 * 1024), b""):
              h.update(chunk)
      return h.hexdigest()
  ```
- **builder 侧非流式**：`scripts/build_offline_bundle.sh:207-208` 与 `packaging/build-appimage.sh:146-148`
  ```python
  files.append({"path": rel, "bytes": os.path.getsize(p),
                "sha256": hashlib.sha256(open(p, "rb").read()).hexdigest()})
  ```
- 路径 containment（字符串级，不看解析后真实路径）：`scripts/verify_bundle_manifest.py:136-139`
  ```python
  target = os.path.normpath(os.path.join(root, rel))
  if os.path.isabs(rel) or rel.startswith("..") or not target.startswith(root + os.sep):
      bad.append("unsafe manifest path: %s" % rel)
      continue
  ```
- 符号链接逃逸规则：`scripts/verify_bundle_manifest.py:147-151`、`173-183`
  ```python
  if os.path.islink(target):
      resolved = os.path.realpath(target)
      if not resolved.startswith(root + os.sep):
          bad.append("symlink escapes bundle: %s -> %s" % (rel, resolved))
          continue
  ```
- `manifest.json` 不得出现在 `files[]`：`scripts/verify_bundle_manifest.py:140-142`
  ```python
  if rel == "manifest.json":
      bad.append("manifest.json must not appear in files[]")
      continue
  ```
- `required[]` 前缀/存在性 + 逃逸前缀检查：`scripts/verify_bundle_manifest.py:66-82`（`_unsafe_bundle_rel`）、`189-203`
- `size_ceiling_mb` 超限：`scripts/verify_bundle_manifest.py:213-215`
- builder 与校验路径基准一致：`scripts/build_offline_bundle.sh:85` 定义 `bundle="$out_dir/sicnu-lab-$version"`，`191` 用同一变量写清单，`253` 用同一变量 `--verify` → **未发现生成/校验基准脱节**。
- 打包脚本无隐式联网前提：对 `scripts/build_offline_bundle.{sh,cmd}`、`packaging/bundle/*.{cmd,sh,ps1}` 过滤注释后 grep `curl|wget|pip install|git clone|Invoke-WebRequest|apt-get|yum|dnf|Start-BitsTransfer` → **0 命中**。AppImage 侧的 `curl` 是显式、pin 到固定 tag 且带 SHA256 校验的（`packaging/appimage_toolchain.sh:63-72`）。

### 1.2 G09 · Windows PS 孪生 `scripts/bundle_manifest.ps1`

- 明确声明**不镜像** symlink 逃逸规则：`scripts/bundle_manifest.ps1:16-18`
  ```
  # The POSIX verifier's escaping-symlink rule
  # is intentionally not mirrored: Windows builders ship dereferenced files
  # (robocopy default), so a bundle-side symlink is a POSIX-shaped anomaly.
  ```
- files[] 列表用 hashtable 记录（**键比较不区分大小写**）：`scripts/bundle_manifest.ps1:81`、`110`
  ```powershell
  $listed = @{}
  foreach ($f in $m.files) { $listed[$f.path] = $true }
  ...
  if ($rel -ne "manifest.json" -and -not $listed.ContainsKey($rel)) {
      $bad += "unlisted file: $rel"
  }
  ```
- 遍历只看"文件"，不进入重解析点：`scripts/bundle_manifest.ps1:47`、`108`
  ```powershell
  Get-ChildItem -LiteralPath $root -Recurse -File | ForEach-Object { ... }
  ```
- ceiling 类型判定：`scripts/bundle_manifest.ps1:152-156`（**只接受 `[int]`**）
  ```powershell
  $ceiling = $m.size_ceiling_mb
  if ($null -eq $ceiling -or $ceiling -isnot [int] -or $ceiling -lt 0) {
      $bad += "size_ceiling_mb must be a non-negative integer"
      $ceiling = 250
  }
  ```
  对照同文件 `140-144` 已同时接受 `[int]`/`[long]`：
  ```powershell
  if (($mr -isnot [int] -and $mr -isnot [long]) -or $mr -lt 1) {
  ```
- 包内镜像同样问题：`packaging/bundle/VERIFY.ps1:97-101`
  ```powershell
  $ceiling = $m.size_ceiling_mb
  if ($null -eq $ceiling -or $ceiling -isnot [int] -or $ceiling -lt 0) {
      $bad += "size_ceiling_mb must be a non-negative integer"
      $ceiling = 250
  }
  ```

### 1.3 G10 · 一键脚本

- `scripts/gen_samples.sh:16` `set -eu`；`:89` `"$bin" "$@"`；`:91-94` verify；`:80-83` `--out` 缺值 → `exit 2`。
- `scripts/gen_samples.cmd:14` `setlocal`；`:28-33` 未找到二进制 → `exit /b 1`；`:76-83` `set "rc=%errorlevel%"` → `exit /b %rc%` / `exit /b %vrc%`。
- `packaging/bundle/GRADE_ALL.sh:80-90`
  ```sh
  set +e
  "$cli" --offline lab --lab "$lab" --batch "$subs" --csv "$csv" ${SICNU_BATCH_FLAGS:-}
  rc=$?
  set -e
  ...
  exit "$rc"
  ```
- `scripts/run_classroom_batch.py:254`
  ```python
  if os.path.splitext(name)[1].lower() in (".csv", ".json"):
  ```
  （跳过 `.csv` / `.json` 提交）
- `scripts/run_classroom_batch.py:191-194`
  ```python
  except Exception as exc:  # noqa: BLE001 — one bad job must never abort the batch
      row["status"] = "error"
      row["message"] = "driver exception: %s" % exc
  ```
- `scripts/verify_lab_outputs.py:17-18`
  ```python
  import numpy ...
  from osgeo import gdal
  ```

### 1.4 G08 · Windows 打包

- `scripts/windows/_env.cmd:71` `set "QT_QPA_PLATFORM=offscreen"`；`packaging/bundle/RUN.cmd:22` 同样设置；`:24` `set "PATH=%BUNDLE%\bin;%PATH%"`（DLL 搜索顺序前置）。
- 全仓库 grep `QT_PLUGIN_PATH`：**`packaging/bundle/**` 与 `scripts/windows/**` 均未设置**（仅在 `cmake/SicnuTestEnv.cmake:173/269` 与诊断文案 `data/help/diagnostics.json:2429-2435` 中出现）。
- `packaging/bundle/*.cmd` 头部均声明 ASCII-only（例：`RUN.cmd:4`、`GENERATE_SAMPLES.cmd:3`）。

### 1.5 G13 · AppImage

- `packaging/appimage-tool-checksums.txt:11-12`（CRLF 行尾，已用 `cat -A` 确认）
  ```
  PENDING  linuxdeploy-x86_64.AppImage
  PENDING  linuxdeploy-plugin-qt-x86_64.AppImage
  ```
- `packaging/appimage_toolchain.sh:35-46`（PENDING → 打印 remedy → `return 1`）
- `packaging/build-appimage.sh:176-177` 调用 `fetch_tool`；`:22` `set -euo pipefail` → 返回 1 即中止。
- `packaging/build-appimage.sh:78-95`（OTB 注释标 required，实现仅 WARNING）
- `packaging/build-appimage.sh:127-132` 注释自述 AppImage 非自校验（manifest 只是 DESTDIR 快照）。
- `packaging/sicnu_geo_rs.desktop:6` `Exec=sicnu_geo_rs %F`；`:7` `Icon=sicnu_geo_rs`。

---

## 2. 候选缺陷清单

### G09-1 · P1 · Windows junction（目录重解析点）逃逸不被检测，包外内容可作为"包内文件"校验通过

- **根因**：`os.path.islink()` 在 Windows 只对 `IO_REPARSE_TAG_SYMLINK` 返回 True；junction 的 tag 是 `0xA0000003`（`IO_REPARSE_TAG_MOUNT_POINT`），`islink` 为 False，于是 `os.walk` 直接下降进入指向包外的 junction，把包外内容当作包内文件枚举/哈希。规范验证器的 `symlink escapes bundle` 规则（`verify_bundle_manifest.py:147-151`、`173-183`）在 Windows 上对此完全失效。
- **位置**：
  - `scripts/verify_bundle_manifest.py:147-151`
  - `scripts/verify_bundle_manifest.py:173-183`
  - `scripts/verify_bundle_manifest.py:136-139`（containment 只做字符串比较，未比对解析后真实路径）
  - `scripts/bundle_manifest.ps1:47` / `:108`（PS 侧 `Get-ChildItem -Recurse -File` 既不进入 junction 也不报告它 → 同样静默）
- **最小 RED 复现**（已真实执行；junction 用 `New-Item -ItemType Junction` 创建，无需特权）：
  1. 建干净 bundle `psbundle7`（`hello.txt` + `bin/tool.dat` + 正确 manifest）
  2. `New-Item -ItemType Junction -Path <bundle>\jlink -Target <bundle 外>\outsidedir`（`outsidedir\secret.txt` 内容 `OUTSIDE-SECRET\n`）
  3. 跑规范验证器；再把 `jlink/secret.txt`（包外内容）登记进 `files[]` 后再跑一次
- **预期**：两次都因"逃逸"被拒绝（至少第二次不能 PASS）。
- **真实输出**：
  ```
  jlink: islink=False isdir=True reparse_tag=0xa0000003
  === G09-15a: junction 存在但 manifest 未列入其内容 ===
  BUNDLE VERIFY FAIL C:\...\psbundle7 (2 files, 0.0 MB / ceiling 250 MB)
    unlisted file: jlink/secret.txt
  exit=1

  manifest 现在把包外文件 outsidedir\secret.txt（内容 b'OUTSIDE-SECRET\n'）登记为 jlink/secret.txt
  === G09-15b: manifest 登记越界内容后 ===
  BUNDLE VERIFY PASS C:\...\psbundle7 (3 files, 0.0 MB / ceiling 250 MB)
  exit=0
  ```
  补充诊断（同一 fixture）：
  ```
  jlink      S_ISLNK=False S_ISDIR=True  st_file_attributes=0x0410 st_reparse_tag=0xa0000003
  os.walk 结果: base=. dirs=['bin','jlink'] → base=jlink names=['secret.txt']   # 已进入包外目录
  ```
- **最小修复方向**（不实现）：containment 改为用 `os.path.realpath` 解析后与 `realpath(root)` 比对；`os.walk` 循环中对 `dirs` 用 `os.lstat().st_file_attributes & 0x400`（或 `st_reparse_tag != 0`）检测重解析点并 prune + 记为逃逸；PS 孪生 `Get-ManifestFiles`/`Test-Bundle` 同步补一条重解析点检测（或至少从"静默忽略"改为显式 finding）。

### G09-2 / G08-1 · P1 · PowerShell 7 上 `size_ceiling_mb` 被误判非法 → 每个 bundle 都被判 FAIL

- **根因**：`$ceiling -isnot [int]`。PowerShell 7 的 `ConvertFrom-Json` 把 JSON 整数解成 `Int64`，`-is [int]` 为 False；于是合法清单被加一条 finding，且 `$verdict` 被翻转（`bundle_manifest.ps1:158`）。同一文件 `142` 行已显式接受 `[int]`/`[long]`，两处不一致。
- **位置**：`scripts/bundle_manifest.ps1:153`；`packaging/bundle/VERIFY.ps1:98`（**随包分发给教师的校验器**）。
- **最小 RED 复现**（已真实执行）：对同一个"内容完全正确"的 `psbundle6` 分别跑 PowerShell 孪生 / 包内 VERIFY.ps1 / 规范 Python 验证器。
- **预期**：三者都 PASS、exit 0。
- **真实输出**：
  ```
  ConvertFrom-Json 整数类型 = Int64  (-is [int] => False)
  PSVersion = 7.6.5
  === PS 孪生验证 psbundle6 ===
  exit=1
    BUNDLE VERIFY FAIL C:\...\psbundle6 (2 files, 0 MB / ceiling 250 MB)
    size_ceiling_mb must be a non-negative integer

  === 包内 VERIFY.ps1 验证 psbundle6 ===
  exit=1
    BUNDLE VERIFY FAIL C:\...\psbundle6 (2 files, 0 MB / ceiling 250 MB)
    size_ceiling_mb must be a non-negative integer

  === 同一 bundle：规范 Python 验证器 ===
  BUNDLE VERIFY PASS C:\...\psbundle6 (2 files, 0.0 MB / ceiling 250 MB)
  exit=0
  ```
  版本对照（已实测）：
  ```
  powershell.exe = C:\WINDOWS\System32\WindowsPowerShell\v1.0\powershell.exe
  5.1 侧: Int32 | PS=5.1.26100.9444
  ```
- **影响面**：`scripts/build_offline_bundle.cmd:189/194` 的最终 `--verify`、`packaging/bundle/VERIFY.cmd:8` → 在任何 `powershell` 解析到 pwsh 7 的机器上恒定失败（误报 FAIL），教师侧"包是否完好"的门禁会永久红灯。
- **最小修复方向**：与同文件 `142` 行一致，改为 `($ceiling -isnot [int] -and $ceiling -isnot [long])`；或在判定前 `[long]$ceiling = [long]$m.size_ceiling_mb` 归一化。

### G09-3 · P2 · PS 孪生对 `files[]` 路径大小写不敏感，与规范验证器结论不一致

- **根因**：`$listed = @{}` 是 PowerShell hashtable，键比较默认 `OrdinalIgnoreCase`；Windows 文件系统同样不区分大小写，于是 `Hello.txt`（清单）vs `hello.txt`（磁盘）在 Windows 侧既不被判 missing 也不被判 unlisted。
- **位置**：`scripts/bundle_manifest.ps1:81`、`:110`；`packaging/bundle/VERIFY.ps1:82`（同构）。规范验证器对照：`scripts/verify_bundle_manifest.py:186`（`rel not in listed`，精确字符串比较）。
- **最小 RED 复现**（已真实执行）：manifest 写 `Hello.txt`、磁盘为 `hello.txt`，分别跑两侧。
- **预期**：两侧结论一致（都应报出路径不一致）。
- **真实输出**：
  ```
  # 规范 Python 验证器
  BUNDLE VERIFY FAIL C:\...\work (2 files, 0.0 MB / ceiling 250 MB)
    unlisted file: hello.txt
  exit=1

  # PS 孪生（pwsh 7）：除 G09-2 的 ceiling 噪声外，没有任何 "unlisted file" 条目
  === PS-C: 大小写不一致 Hello.txt vs hello.txt === exit=1
    BUNDLE VERIFY FAIL C:\...\psbundle3 (2 files, 0 MB / ceiling 250 MB)
    size_ceiling_mb must be a non-negative integer
  ```
  旁证（同机实测）：`ContainsKey(hello.txt)=True`（键为 `Hello.txt` 时）。
- **后果**：Windows 构建并自校验通过的 bundle，在 Linux 侧（`VERIFY.sh` / `tools/verify_bundle_manifest.py`）会 FAIL —— 同一产物跨平台结论不一致。
- **最小修复方向**：`$listed` 换成 `Ordinal` 比较器（如 `[System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)`），或额外做一次"清单路径 vs 磁盘实际大小写"精确核对。

### G09-4 · P2 · builder 侧清单哈希非流式，单文件内存峰值 = 文件大小

- **根因**：`hashlib.sha256(open(p,"rb").read())`（整体读入），与验证器 `_digest` 的 1MB 分块写法不一致。
- **位置**：`scripts/build_offline_bundle.sh:207-208`；`packaging/build-appimage.sh:146-148`。
- **最小 RED 复现**（已真实执行，200 MB 文件）：
  ```
  文件大小           : 200.0 MB
  read() 返回对象大小 : 200.0 MB (sys.getsizeof)
  流式写法单次缓冲    : 1.0 MB（固定）
  ```
  两种写法 hash 结果一致（`hash 一致: True`），即**不是正确性缺陷，是可伸缩性/内存缺陷**。
- **最小修复方向**：把两处内联 python 的哈希换成与 `verify_bundle_manifest.py:37-42` 相同的 1MB 分块实现（或 `import` 该文件的 `_digest`）。

### G09-5 · P2 · `./manifest.json` 等变体可绕过 "manifest.json 不得出现在 files[]" 的显式拒绝

- **根因**：`verify_bundle_manifest.py:140` 用精确字符串 `rel == "manifest.json"` 判断；`./manifest.json`、`sub/../manifest.json`、`Manifest.json` 均在 `136-139` 的 containment 之后落进"普通文件"分支。
- **位置**：`scripts/verify_bundle_manifest.py:140-142`。
- **最小 RED 复现**（已真实执行）：在 `files[]` 中加一条 `./manifest.json`。
- **预期**：`manifest.json must not appear in files[]`。
- **真实输出**：
  ```
  BUNDLE VERIFY FAIL C:\...\work (3 files, 0.0 MB / ceiling 250 MB)
    size mismatch ./manifest.json: 592 != 438
  exit=1
  ```
- **影响**：有限 —— 自指条目不可能自洽（manifest 需包含自身 hash），必然 FAIL；但 typed 诊断被降级成通用 `size mismatch`，排障信息丢失。`Manifest.json`（大小写变体）在 Linux 上还会额外产生 `missing` + `unlisted` 双报。
- **最小修复方向**：对 `rel` 先做规范化（posix 化、去掉前导 `./`、折叠 `..`）再与 `manifest.json` 比较。

### G13-1 · P1 · AppImage 工具链校验和仍为 PENDING，`fetch_tool` 必然失败；`--tools-dir` 预置路径同样不可用

- **根因**：`packaging/appimage-tool-checksums.txt:11-12` 两行仍是 `PENDING`；`appimage_toolchain.sh:35-46` 在没有 64-hex 记录时无条件 `return 1`（含"已预置本地文件"分支 —— 该分支在 `49-60`，但**先于它**的 `36` 行已因无 hash 返回）。
- **位置**：`packaging/appimage-tool-checksums.txt:11-12`；`packaging/appimage_toolchain.sh:36-46`；`packaging/build-appimage.sh:176-177`。
- **最小 RED 复现**（已真实执行）：
  ```sh
  bash -c 'source packaging/appimage_toolchain.sh; fetch_tool /tmp/x "https://example.invalid/linuxdeploy" linuxdeploy-x86_64.AppImage; echo "fetch_tool exit=$?"'
  bash -c 'export APPIMAGE_TOOLS_DIR=/tmp/r12g-audit/tools; source packaging/appimage_toolchain.sh; fetch_tool /tmp/ld2 "..." linuxdeploy-x86_64.AppImage; echo "fetch_tool exit=$?"'
  ```
  （第二个场景已预先放置同名文件到 `APPIMAGE_TOOLS_DIR`）
- **预期**：至少 `--tools-dir` 场景在已预置文件后可用（文件头 `16-18` 行宣称支持"offline reproducible once a maintainer has recorded the hashes once"）。
- **真实输出**（stderr）：
  ```
  appimage_toolchain: no SHA256 recorded for linuxdeploy-x86_64.AppImage yet.
    Record it once from the pinned release artifact:
      curl -fL -o linuxdeploy-x86_64.AppImage 'https://example.invalid/linuxdeploy' && sha256sum linuxdeploy-x86_64.AppImage >> .../packaging/appimage-tool-checksums.txt
    (or provision --tools-dir and record the same hash)
  fetch_tool exit=1          # 场景 1

  appimage_toolchain: no SHA256 recorded for linuxdeploy-x86_64.AppImage yet.
    ... 同上 ...
  fetch_tool exit=1          # 场景 2（已预置 APPIMAGE_TOOLS_DIR，仍然失败）
  ```
  未登记文件名的对照：`appimage_toolchain: some-other-tool.AppImage not listed in ...` / `exit=1`。
- **判定说明**：这是**刻意的 fail-closed**（`appimage_toolchain.sh:13-15`），本身是好事；作为"候选缺陷"列出，是因为它使 `build-appimage.sh` 在当前仓库状态下**不可能产出 AppImage**，且文档承诺的 `--tools-dir` 离线路径在 PENDING 期间同样不可用 —— 属于交付阻塞项而非安全漏洞。
- **最小修复方向**：由有网机器对两个固定 tag（`1-alpha-20240109`）执行一次 `sha256sum` 并替换 PENDING 两行；若希望 PENDING 期仍允许离线预置，需在 `36` 行前增加一个显式的"本地文件 + 显式确认"分支。

### G13-2 · P1 · OTB 注释标为 required，实现却把复制失败全部 `|| true` 并**无条件**打印成功

- **根因**：`packaging/build-appimage.sh:83-90` 四个 `cp` 全部 `2>/dev/null || true`；`:91` 的 `echo "OTB bundled successfully"` 与实际是否复制成功无关；`:93-94` 目录不存在时只 WARNING 不退出。
- **位置**：`packaging/build-appimage.sh:78`（`# Bundle OTB binaries (required — OTB is part of the application)`）、`83-90`、`91`、`93-94`。
- **最小 RED 复现**（已真实执行）：建一个只有空 `bin/` 的 OTB 源目录，原样跑 `78-95` 行这段逻辑。
- **预期**：复制 0 个文件时不应打印 "OTB bundled successfully"，required 组件缺失应中止。
- **真实输出**：
  ```
  OTB bundled successfully
  实际复制结果:
  (文件数=0)
  script exit=0
  ```
- **最小修复方向**：统计实际复制文件数（`shopt -s nullglob` + 计数），为 0 时 `exit 1`；成功消息只在计数 > 0 时打印；同理收紧 `:103-105` 的 GDAL"已 bundled"消息（GDAL 标为 optional，可仅改消息）。

### G13-3 · P2 · `resources/desktop/**` 在仓库中不存在；桌面入口只有 `packaging/` 下单个文件

- **位置**：G13 审计范围列出的 `resources/desktop/**`。
- **最小 RED 复现**（已真实执行）：
  ```
  EXISTS   packaging/sicnu_geo_rs.svg
  EXISTS   packaging/sicnu_geo_rs.desktop
  MISSING  resources/desktop
  EXISTS   packaging/appimage-tool-checksums.txt
  ```
- **判定**：要么是 G13 范围描述的路径写错（应为 `packaging/`），要么是桌面资源目录确实缺失（清单遗漏）。`packaging/build-appimage.sh:182-183` 实际引用的是 `packaging/sicnu_geo_rs.desktop` 与 `packaging/sicnu_geo_rs.svg`，二者均存在。
- **最小修复方向**：由 team-lead 确认范围；若 `resources/desktop/` 是规划中的目录，需在 `build-appimage.sh` 的 `61-65` 资源复制段补上对应 `cp`（否则不会进 AppImage）。

### G10-1 · P2 · `SICNU_GENERATE_SAMPLES` 环境变量不做存在性校验，掩盖明确诊断

- **根因**：只有"变量为空"才走明确诊断（`gen_samples.sh:33-38` / `gen_samples.cmd:22-33`）；变量非空但指向不存在路径时直接执行，落到 shell/cmd 的原生报错。
- **位置**：`scripts/gen_samples.sh:20-38`（对照 `:89`）；`scripts/gen_samples.cmd:17-33`（对照 `:76`）。
- **最小 RED 复现**（已真实执行）：
  ```sh
  SICNU_GENERATE_SAMPLES=/nonexistent/sicnu_generate_samples bash scripts/gen_samples.sh --out /tmp/...
  ```
- **预期**：`gen_samples: sicnu_generate_samples not found.` + 修复建议 + `exit 1`。
- **真实输出**：
  ```
  scripts/gen_samples.sh: line 89: /nonexistent/sicnu_generate_samples: No such file or directory
  exit=127
  # cmd 侧（python subprocess 驱动，env 指向不存在的 .cmd）
  '"C:/.../nope-nonexistent.cmd"' 不是内部或外部命令，也不是可运行的程序
  exit=9009
  ```
  （未设变量时的正确行为，已实测：`gen_samples: sicnu_generate_samples not found.` + 两行建议 + `exit=1`）
- **最小修复方向**：对 env 值补一次 `-x` / `if exist` 校验，失败时复用 `33-38` / `28-33` 的同一条诊断。

### G10-2 · P2 · `gen_samples.cmd` 缺少 `--out` 缺值校验（sh 版有 `exit 2`），两个"CLI 完全一致"的孪生在此分叉

- **位置**：`scripts/gen_samples.cmd:62-68`（`:maybe_rejoin` 只处理"下一个 token 不以 `-` 开头"，末尾 `--out` 直接原样转发）；对照 `scripts/gen_samples.sh:80-83`。
- **最小 RED 复现**（已真实执行；`SICNU_GENERATE_SAMPLES` 指向一个回显参数的假二进制）：
  ```
  === G10-23: --out 缺值（末尾 --out） ===
  stdout: ARG[--out]
  exit=3            # 3 是假二进制自己返回的，脚本没有 exit 2
  ```
  sh 侧对照（已实测）：
  ```
  gen_samples: --out requires a value (use --out=<value> or --out <value>)
  exit=2
  ```
- **最小修复方向**：在 `cmd` 的 `:args_parsed` 前补一个"上一个 key 未取到值"分支，`exit /b 2`。

### G10-3 · P2 · `GENERATE_SAMPLES.cmd` 非默认输出（或根本无 `--out`）时跳过校验，却仍打印"已校验"

- **根因**：`:55` `if not defined GEN_OUT goto :done`、`:56` 仅当目标等于 `data\samples` / `data/samples` 才 verify；`:66` 的 `Done. Sample data is in place and verified.` 对跳过路径也照打。
- **位置**：`packaging/bundle/GENERATE_SAMPLES.cmd:55`、`:56`、`:66`。
- **最小 RED 复现**（已真实执行）：把第 `19`/`28`/`58` 行的 `bin\sicnu_generate_samples.exe` 替换为等效 stub（**其余逻辑逐字未改**），分别以 `--seed=7`、`--out=data\samples_demo` 运行。
- **预期**：未执行校验时不应声称 verified。
- **真实输出**（stub 命中时会打印 `STUB-VERIFY`；下面两例没有该行）：
  ```
  === G10-26: 仅 --seed=7（无 --out） ===
  stdout: Generating deterministic sample rasters ... | ... | STUB-GEN[--seed] | STUB-GEN[7] | Done. Sample data is in place and verified.
  exit=0

  === G10-27: 自定义 --out=data\samples_demo ===
  stdout: ... | STUB-GEN[--out] | STUB-GEN[data\samples_demo] | Done. Sample data is in place and verified.
  exit=0
  ```
  对照（默认路径确实执行了校验）：
  ```
  === G10-25: 无参数 ===
  stdout: ... | Verifying generated samples against the manifest ... | STUB-VERIFY "--out=data\samples" | Done. Sample data is in place and verified.
  exit=0
  ```
- **最小修复方向**：`:66` 拆成两条消息（已校验 / 未校验），未校验时明确打印"verification skipped for non-default target"，并建议显式跑 `--verify`。

### G10-4 · P2 · `run_classroom_batch.py` 静默丢弃 `.csv` / `.json` 提交，无任何提示

- **位置**：`scripts/run_classroom_batch.py:254`（`if os.path.splitext(name)[1].lower() in (".csv", ".json"):`）；`:248` 还会跳过与输出文件同名的条目。
- **最小 RED 复现**（已真实执行）：提交目录放 3 个 `.tif` + 1 个 `.csv` + 1 个 `.json`。
- **预期**：要么批改 5 份，要么明确提示"已跳过 N 个非栅格提交"。
- **真实输出**：
  ```
  run_classroom_batch: stu01                    pass
  run_classroom_batch: stu02                    pass
  run_classroom_batch: stu03                    pass
  run_classroom_batch: graded=3 pass=3 fail=0 timeout=0 error=0 crash=0 capped=0
  exit=0
  ```
  （`notes.csv` / `report.json` 被静默忽略，无任何 stderr 提示）
- **最小修复方向**：在 `discover_submissions` 里累计 skip 计数并在汇总行/`--quiet` 关闭时打印一行提示。

### G10-5 · P2 · `verify_lab_outputs.py` 依赖缺失时抛原始 traceback（虽为 exit 1）

- **位置**：`scripts/verify_lab_outputs.py:17-18`（模块级 `import numpy` / `from osgeo import gdal`）。
- **最小 RED 复现**（已真实执行）：`python scripts/verify_lab_outputs.py`
- **真实输出**：
  ```
  Traceback (most recent call last):
    File "...\scripts\verify_lab_outputs.py", line 18, in <module>
      from osgeo import gdal
  ModuleNotFoundError: No module named 'osgeo'
  exit=1
  ```
  对照（明确的依赖探测，已实测）：`run_lab_pipelines.py` → `CLI binary not found: build/sicnu_geo_rs_cli (build first)` / `exit=1`。
- **最小修复方向**：把 `import` 包进 `try/except ImportError`，打印"需要 numpy + osgeo.gdal（见 docs）"后 `sys.exit(2)`（对齐"依赖缺失 = 无法验证"语义）。

### G10-6 · P2 · `GRADE_ALL.sh` 对任何非零 rc 都描述为 "isolated error rows"

- **位置**：`packaging/bundle/GRADE_ALL.sh:85-89`。
- **最小 RED 复现**（已真实执行；`bin/sicnu_geo_rs_cli` 用返回 `FAKE_RC` 的 stub）：
  ```
  === G10-30: GRADE_ALL.sh rc=7 ===
  CLI ARGS: --offline lab --lab ndvi_basics --batch ... --csv ...
  Finished with isolated error rows (exit 7) - they are kept in the CSV.
  exit=7
  ```
  rc 传播本身**正确**（`exit=7`）；只有措辞在 rc=2（usage）/rc=3（无法验证）/崩溃时会误导。
- **最小修复方向**：按 rc 分类给消息（0 / 1 / 2 / 3 / 其他）。

### G08-2 · P2 · `RUN.cmd` 仅凭 `manifest.json` 存在就跳过样本生成，未做任何校验

- **位置**：`packaging/bundle/RUN.cmd:27-32`
  ```
  rem Skip only when the foundry manifest proves the sample set is complete; ...
  if exist "data\samples\manifest.json" (
    echo     data\samples already present - skipping generation.
    goto :pipeline
  )
  ```
- **判定**：注释声称 manifest "proves the sample set is complete"，但代码只做 `if exist`，没有调用 `sicnu_generate_samples.exe --verify`（对照 `GENERATE_SAMPLES.cmd:58` 会真跑 verify）。`data\samples\manifest.json` 残留而内容被删改时会被静默跳过。
- **最小修复方向**：把 `if exist` 换成先跑一次 `--verify`，失败才重新生成。

---

## 3. 明确排除项（已实测，不构成缺陷）

1. **改包里一个字节必然失败** —— 已实测两种情形：
   ```
   # 同长度改字节
   BUNDLE VERIFY FAIL C:\...\fakebundle (2 files, 0.0 MB / ceiling 250 MB)
     sha256 mismatch hello.txt
   exit=1
   # 追加字节（长度变化）
   exit=1（size mismatch 数据/实验一.tif: 8 != 7）
   ```
   中文 + 空格路径下同样成立（见 5）。
2. **路径穿越防护有效**（Python 与 PS 孪生双侧实测）：
   ```
   unsafe manifest path: ../outside.txt
   unsafe manifest path: ..\outside.txt
   unsafe manifest path: C:/windows/win.ini
   unsafe manifest path: C://windows//win.ini
   unsafe manifest path: /etc/hostname
   unsafe manifest path: \\server\share\x
   unsafe manifest path: sub/../../outside.txt
   exit=1
   # PS 侧
   unsafe manifest path: ../outside.txt
   unsafe manifest path: C:/windows/win.ini
   unsafe manifest path: \\server\share\x
   unsafe manifest path: /etc/hostname
   ```
   `required[]` 同样拒绝：`unsafe required entry: '../outside.txt'` / `'C:/windows/'`（`verify_bundle_manifest.py:194-196`）。**未发现任何一次对包外文件的读取**。
3. **大小写冲突在规范验证器侧会被抓到**：`unlisted file: hello.txt`（exit 1）。（Windows PS 侧漏报 → 见 G09-3）
4. **空目录被忽略**：新建 `emptydir` 后仍 `BUNDLE VERIFY PASS exit=0`。这与契约一致（`files[]` 只覆盖普通文件），非缺陷。
5. **无扩展名白名单**：`.txt/.tif/.dat/.json` 一视同仁入清单；未发现任何按扩展名跳过普通文件的逻辑。
6. **中文 + 空格路径（G08/G09 交叉）正常工作**：
   ```
   bundle: C:/.../r12g-audit\中文 用户名 测试\sicnu 离线包
   BUNDLE VERIFY PASS ... (2 files, 0.0 MB / ceiling 250 MB)   exit=0
   # 改一字节后
   BUNDLE VERIFY FAIL ... 
     size mismatch 数据/实验一.tif: 8 != 7                      exit=1
   ```
7. **长路径（360 字符）正常**：`BUNDLE VERIFY PASS ... exit=0`。（`\\?\` 前缀混用正斜杠时会 `not a directory` → `exit=2`，属 fail-closed，不计缺陷。）
8. **退出码契约 0/1/2 全部符合**：
   ```
   无 manifest.json                       → no manifest.json under ...                        exit=2
   manifest 非法 JSON                     → manifest.json is not valid JSON: ...              exit=2
   schema /3                              → unsupported manifest schema: '.../3' ...          exit=2
   compat.min_reader_schema=9             → compat.min_reader_schema 9 exceeds ... (2)        exit=2
   bundle 目录不存在                       → verify_bundle_manifest: not a directory: ...      exit=2
   files[] 非数组                          → BUNDLE VERIFY FAIL + files[] must be an array     exit=1
   size_ceiling_mb=1 / 实际 3MB            → size 3.0 MB exceeds ceiling 1 MB                  exit=1
   新增未列文件 / 删除已列文件               → unlisted file / missing ...                      exit=1
   ```
9. **管道吞退出码：G10 范围内未发现**。`packaging/bundle/*.sh|*.cmd` 与 G10 指定脚本均无 `| tee`；GRADE_ALL.sh 用 `set +e` → `rc=$?` → `set -e` 正确捕获（实测 rc=7 传播）。全仓唯一 `| tee` 在 `scripts/build.sh:138`（**G10 范围外**），且已正确使用 `${PIPESTATUS[0]}`。
10. **`packaging/bundle/VERIFY.ps1:124` 的空 `catch { }` 未吞掉失败**：紧随的 `125-128` 行判空后 `exit 1`。
11. **`scripts/bundle_manifest.ps1:165` 的 `if ($Verify -ne "")` 不会破坏写模式**：实测 `[string]$v=$null ; $v -ne "" => False`（`[string]` 参数把 `$null` 归一为 `''`）；实际以 `-Bundle` 调用可正常产出 manifest（`manifest: 2 files (schema sicnu.offline_bundle/2)`）。
12. **`scripts/build_offline_bundle.sh:106` 的 `>/dev/null 2>&1`** 只作用于"增量复用时先试跑一次 `--verify`"的判定，失败即走 `:109` 重新生成，不掩盖生成失败。
13. **打包脚本无隐式联网前提**（见 1.1 末）：离线 bundle 全链路无 `curl/wget/pip/git clone`。

---

## 4. NOT_RUN 清单（本机无法验证，未做任何推断填充）

| # | 项 | 无法验证的原因 |
|---|---|---|
| 1 | `scripts/windows/build.cmd` 全流程（MSVC x64 编译、`:run_logged`、Unicode fixture 自检） | 硬约束禁止重型 C++ 编译 |
| 2 | `scripts/windows/setup.cmd`（vcvars / cmake configure / ninja） | 同上 |
| 3 | `scripts/windows/bundle_dependency_report.ps1`（dumpbin 依赖闭包、`$systemDlls` 白名单、`$shipped` 大小写不敏感） | 需要真实构建产物 + `dumpbin` |
| 4 | `scripts/windows/check_mcp.cmd` / `check_mcp_stdio.ps1`（stdout/stderr 死锁防护、`BeginErrorReadLine`） | 需要真实 MCP 二进制 |
| 5 | **Windows 真实文件符号链接**逃逸（`verify_bundle_manifest.py:147-151` 对文件 symlink 的行为） | 本机未开启 Developer Mode：`os.symlink` 与 `New-Item -ItemType SymbolicLink` 均退化为普通文件（实测：新建对象 `size=0`、`st_file_attributes=0x0020`、`st_reparse_tag=0x00000000`，无重解析点）。**junction 分支已实测（G09-1），文件 symlink 分支未测** |
| 6 | `scripts/bundle_manifest.ps1` 写模式的完整产物校验（`-ComponentsFromBin`、BOM/编码、`required[]` 全量） | 需真实 `bin\*.dll`；仅验证了写模式能产出 manifest |
| 7 | `QT_PLUGIN_PATH` / Qt plugin 发现 / GDAL provider 运行时解析（G13 运行时资源查找） | 需要真实 Windows bundle 与 Linux AppImage 运行环境；静态结论：仓库内 `packaging/bundle/**`、`scripts/windows/**` 均未设置 `QT_PLUGIN_PATH` |
| 8 | AppImage 实际启动、`resources/desktop/**` 内容、卸载/清理一致性 | 本机为 Windows；AppImage 在 PENDING 状态下根本无法构建（G13-1） |
| 9 | `lab --batch` 在"部分行失败"时的真实退出码语义 → 因而 `GRADE_ALL.sh:85-86` 的 "Done. All submissions graded" 是否会在存在失败行时出现 | 需要真实 `sicnu_geo_rs_cli` |
| 10 | GBK / UTF-8 控制台编码对 `.cmd` 输出的系统性影响（`chcp 65001`） | 仅观察到：cmd 以 GBK 回显中文（G10-21 中 `实验输出` 显示为乱码），但**参数本身完整传给子进程**（`ARG[--out=.../实验输出]`）；未做全量编码矩阵验证 |
| 11 | Windows PowerShell 5.1 下 `bundle_manifest.ps1` / `VERIFY.ps1` 的完整行为 | 5.1 解释器存在（已确认路径与版本），但本次仅通过 5.1 侧单点确认 `ConvertFrom-Json` 产出 `Int32`；未跑完整脚本（PowerShell 工具回话为 7.6.5，且不回显 stdout，需写文件再读，未对 5.1 做完整跑测） |
| 12 | `run_classroom_batch.py` 超时 kill（`--timeout` + 进程组 kill）的真实行为 | 需构造挂死 worker，时间成本高，未执行 |

---

## 附：本次审计实际执行过的命令（摘录，供复核）

```sh
# 全局夹具目录（worktree 外）
C:\Users\wangj.KEVIN\AppData\Local\Temp\r12g-audit\

# G09 核心
python scripts/verify_bundle_manifest.py <fakebundle>                    # PASS/FAIL/exit
python - <<'PY' ... 构造 manifest、改字节、穿越条目、大小写、junction ... PY
# G08/G09 中文路径
python scripts/verify_bundle_manifest.py "...\中文 用户名 测试\sicnu 离线包"
# G09 PS 孪生（PowerShell 工具，结果写入临时文件后读取）
& scripts\bundle_manifest.ps1 -Verify <bundle>
& packaging\bundle\VERIFY.ps1 -Root <bundle>
New-Item -ItemType Junction -Path <bundle>\jlink -Target <bundle 外>\outsidedir
# G10
bash scripts/gen_samples.sh ...                    # sh 侧
python -c 驱动 cmd /c scripts\gen_samples.cmd ...  # cmd 侧
python scripts/run_classroom_batch.py --cli <stub> --lab lab1 --submissions ... --out-prefix ...
bash <fakebundle>/GRADE_ALL.sh <subs> ndvi_basics <csv>
python scripts/gen_lab_packs.py --check
python scripts/check_lab_registry.py ; python scripts/run_lab_pipelines.py ; python scripts/verify_lab_outputs.py
# G13
bash -c 'source packaging/appimage_toolchain.sh; fetch_tool ...'
```
