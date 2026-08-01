# CLI Python Plugin Host Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract the Python hosting core out of `PluginManager` into a GUI-free **Python Plugin Host** (`sicnu_python_isolated` static lib) and wire it into `sicnu_geo_rs_cli` behind repeatable `--python-plugin` options, so headless pipelines can execute `py:` algorithms via a main-thread-marshaled JobEngine prefix executor, with pipeline outputs registered as Data Assets.

**Architecture:** `PythonPluginAdapter` is decoupled from `SicnuAppInterface` (takes `DataManager*`/`QMenu*`/`ActiveViewHost*` directly) and moved into a new `sicnu_python_isolated` static lib alongside the existing IPC/pool/proxy/bridge sources plus the new `PythonPluginHost`. The host owns the worker pool, plugin loading (metadata → adapter → `classFactory`), and registers a `py:` JobEngine prefix executor that marshals `AlgorithmEngine::executeAlgorithm` onto the host's (main) thread via `Qt::BlockingQueuedConnection`. `PluginManager` thins to a GUI adapter composing the host. The CLI creates a `DataManager` + host, loads declared plugins fail-fast, pumps Qt events in the pipeline wait loop, and registers completed step outputs via `DataManager::registerSource` + `attachDerivationRecord` (NOT `OutputCommitter` — its temp→stable rename and `DeletableSource` ownership semantics do not fit user-declared final output paths; verified in `output_committer.cpp:103-128`).

**Tech Stack:** C++17, Qt 6 (Core/Gui/Widgets/Network), GDAL, Catch2, CMake, Python 3 (worker daemon, unchanged).

**Spec:** `docs/superpowers/specs/2026-08-01-cli-python-plugin-host-spec.md`
**Domain terms:** `CONTEXT.md` — **Python Plugin Host**, **Plugin Host**, **Headless Asset Seam**, ADR 0023.

## Global Constraints

- No behavior change for the GUI: `tests/test_python_plugin_manager "[python]"` (14 cases / 107 assertions) must stay green after every task. It is the regression lock.
- Do NOT modify `src/python/isolated/python_ipc_server.*` or `src/python/scripts/worker_daemon.py` (ticket 02's verified mechanisms stay untouched).
- Do NOT use `OutputCommitter`/`commitTaskOutput` for pipeline output registration (it renames temp→stable and asserts `DeletableSource`; CLI outputs are user-declared final paths). Use `DataManager::registerSource` + `attachDerivationRecord`.
- The `py:` executor must never run `AlgorithmEngine::executeAlgorithm` on a JobEngine worker thread: direct call when on the host's thread, `Qt::BlockingQueuedConnection` otherwise. The host's thread must pump events while pipelines run.
- `SICNU_EMBED_PYTHON` guards in `PluginManager` stay as they are; the new lib and the CLI path compile unguarded (hosting is out-of-process, nothing is embedded).
- Timeout semantics unchanged: 300 s inside the proxy's execute lambda (ticket 02). No new timeout config.
- Match surrounding style: 2-space indent + `QStringLiteral` in `src/python/isolated/*.cpp` (note: `plugin_manager.cpp` and `python_plugin_adapter.cpp` use 4-space indent — keep each file's existing style); Catch2 `TEST_CASE`/`SECTION` conventions.
- Build: `cmake --build build --target <target> -j"$(nproc)"`. Regression suite: `./build/tests/test_python_plugin_manager "[python]"`. New suite: `./build/tests/test_python_plugin_host "[python]"`.
- Commit after every task (per-task commits, same pattern as ticket 02).

---

### Task 1: Decouple `PythonPluginAdapter` from the app shell

**Files:**
- Modify: `src/app/python/python_plugin_adapter.h` (ctor signature + members)
- Modify: `src/app/python/python_plugin_adapter.cpp:1-8` (includes), `:19-39` (ctor), `:92-104` (initialize proxy wiring)
- Modify: `src/core/plugin_manager.cpp:1-16` (includes), `:175` (adapter construction call site)
- Test: `tests/test_python_plugin_manager.cpp` (regression only — no edits)

**Interfaces:**
- Consumes: `PythonAppInterfaceProxy( PythonIpcServer *, sicnu::data::DataManager *, QMenu *, ActiveViewHost *, QObject * )` (`python_app_interface_proxy.h:27-31`), `setActiveViewHost( ActiveViewHost * )`.
- Produces (Tasks 2-4 rely on this exact signature):
  ```cpp
  PythonPluginAdapter( const QString &pluginDir, const QString &packageName,
                       const QString &name, const QString &description, const QString &version,
                       sicnu::data::DataManager *dataManager,
                       QMenu *pluginMenu,
                       ActiveViewHost *activeViewHost,
                       sicnu::python::isolated::PythonWorkerProcessPool *pool );
  ```

- [ ] **Step 1: Change the adapter header**

In `src/app/python/python_plugin_adapter.h`: remove `class SicnuAppInterface;`, add forward declarations and swap the ctor parameter:

```cpp
class QMenu;
class ActiveViewHost;
namespace sicnu::data { class DataManager; }
```

```cpp
    explicit PythonPluginAdapter( const QString &pluginDir,
                                  const QString &packageName,
                                  const QString &name,
                                  const QString &description,
                                  const QString &version,
                                  sicnu::data::DataManager *dataManager,
                                  QMenu *pluginMenu,
                                  ActiveViewHost *activeViewHost,
                                  sicnu::python::isolated::PythonWorkerProcessPool *pool = nullptr );
```

Replace the `SicnuAppInterface *m_appInterface` member with:

```cpp
    sicnu::data::DataManager *m_dataManager = nullptr;
    QMenu *m_pluginMenu = nullptr;
    ActiveViewHost *m_activeViewHost = nullptr;
```

- [ ] **Step 2: Change the adapter cpp**

In `src/app/python/python_plugin_adapter.cpp`:
- Remove includes `sicnu_app_interface.h` and `project_context.h` (keep `python_app_interface_proxy.h`, `python_worker_process_pool.h`, `data/data_manager.h`).
- Ctor: replace `SicnuAppInterface *appInterface` param with the three new pointers and initialize `m_dataManager( dataManager ), m_pluginMenu( pluginMenu ), m_activeViewHost( activeViewHost )` instead of `m_appInterface( appInterface )`.
- In `initialize()`, replace lines 92-104 (the `m_appInterface`-derived wiring) with:

```cpp
    // Attach UI RPC Proxy Facade (headless asset seam: DataManager is the
    // asset authority; menu and view host remain optional GUI enhancements).
    m_uiProxy = std::make_unique<PythonAppInterfaceProxy>( m_workerNode->server, m_dataManager, m_pluginMenu );
    if ( m_activeViewHost )
    {
        m_uiProxy->setActiveViewHost( m_activeViewHost );
    }
```

- [ ] **Step 3: Update the PluginManager call site**

In `src/core/plugin_manager.cpp`:
- Add includes at the top (inside the existing `SICNU_EMBED_PYTHON` guard block):

```cpp
#include "app/python/sicnu_app_interface.h"
#include "app/project_context.h"
```

(If `project_context.h` resolves elsewhere, locate it with `find src -name project_context.h` and adjust; `sicnu_app_interface.h` is at `src/app/python/sicnu_app_interface.h`.)

- Replace the adapter construction (line 175) with:

```cpp
    QMenu *pluginMenu = m_appInterface ? m_appInterface->pluginMenu() : nullptr;
    sicnu::data::DataManager *dataManager = nullptr;
    if ( m_appInterface && m_appInterface->projectContext() )
    {
        dataManager = &m_appInterface->projectContext()->dataManager();
    }
    ActiveViewHost *activeViewHost = m_appInterface ? m_appInterface->activeViewHost() : nullptr;
    auto *adapter = new PythonPluginAdapter( pluginDir, packageName, name, description, version,
                                             dataManager, pluginMenu, activeViewHost, m_pythonPool );
```

- [ ] **Step 4: Build and run the regression lock**

Run: `cmake --build build --target test_python_plugin_manager -j"$(nproc)" && ./build/tests/test_python_plugin_manager "[python]"`
Expected: PASS — all 14 cases / 107 assertions (pure refactor, no behavior change).
Also build the app: `cmake --build build --target sicnu_geo_rs -j"$(nproc)"` (use the app target name from `src/app/CMakeLists.txt` if different) — Expected: links clean.

- [ ] **Step 5: Commit**

```bash
git add src/app/python/python_plugin_adapter.h src/app/python/python_plugin_adapter.cpp src/core/plugin_manager.cpp
git commit -m "refactor(python): decouple PythonPluginAdapter from SicnuAppInterface

The adapter now takes DataManager/QMenu/ActiveViewHost directly so it can
live in a GUI-free library; PluginManager derives the pointers from its
app interface. No behavior change."
```

---

### Task 2: Create the `sicnu_python_isolated` static lib

**Files:**
- Create: `src/python/isolated/CMakeLists.txt`
- Move (git mv): `src/app/python/python_plugin_adapter.{h,cpp}` → `src/python/isolated/`
- Modify: `CMakeLists.txt` (root, `add_subdirectory` near line 518-523)
- Modify: `src/app/CMakeLists.txt:285-300` (remove isolated + adapter sources, link the lib)
- Modify: `tests/CMakeLists.txt:2867-2881` (`test_python_plugin_manager` links the lib instead of compiling sources)
- Modify: `src/core/plugin_manager.cpp:4` (include path for the moved adapter header)

**Interfaces:**
- Consumes: Task 1's decoupled adapter.
- Produces (all later tasks rely on this): CMake target `sicnu_python_isolated` (STATIC) exposing `src/python/isolated` headers, linking `Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network sicnu_processing Sicnu::data qgis_core qgis_gui`.

- [ ] **Step 1: Move the adapter and create the lib CMakeLists**

```bash
git mv src/app/python/python_plugin_adapter.h src/python/isolated/python_plugin_adapter.h
git mv src/app/python/python_plugin_adapter.cpp src/python/isolated/python_plugin_adapter.cpp
```

Create `src/python/isolated/CMakeLists.txt`:

```cmake
# GUI-free Python plugin hosting core (ADR 0023): out-of-process worker pool,
# IPC server, app-interface proxy/bridge, and the plugin adapter/host.
qt_add_library(sicnu_python_isolated STATIC
    app_interface_bridge.cpp
    python_app_interface_proxy.cpp
    python_ipc_server.cpp
    python_plugin_adapter.cpp
    python_worker_process.cpp
    python_worker_process_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/app/active_view_host.cpp
)

target_include_directories(sicnu_python_isolated PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(sicnu_python_isolated PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    sicnu_processing
    Sicnu::data
    qgis_core
    qgis_gui
)
```

Note: `active_view_host.cpp` is compiled in because `AppInterfaceBridge`/`PythonAppInterfaceProxy` call `ActiveViewHost` methods; `tests/CMakeLists.txt:2869` proves this single source is sufficient. If the link reports more missing app symbols, check `tests/CMakeLists.txt:2867-2881` — it is the known-good recipe.

- [ ] **Step 2: Rewire the root, app, and test CMake**

Root `CMakeLists.txt` — add before `add_subdirectory(src/cli)` (line ~523; must precede both `src/cli` and `src/app`):

```cmake
add_subdirectory(src/python/isolated)
```

`src/app/CMakeLists.txt` — remove these lines from the app target sources (around 285-300): `python_ipc_server.cpp`, `python_worker_process.cpp`, `app_interface_bridge.cpp`, `python_app_interface_proxy.cpp`, `python_worker_process_pool.cpp`, `python_plugin_adapter.cpp` (exact list: read the block before editing); add `sicnu_python_isolated` to the app target's `target_link_libraries`.

`tests/CMakeLists.txt` (`test_python_plugin_manager`, lines 2867-2881) — remove sources: `src/app/python/python_plugin_adapter.cpp` and the five `src/python/isolated/*.cpp` lines (keep `active_view_host.cpp` removal optional — it is now in the lib; drop it to avoid duplicate symbols); add `sicnu_python_isolated` to `target_link_libraries`.

`src/core/plugin_manager.cpp:4` — change `#include "app/python/python_plugin_adapter.h"` to `#include "python/isolated/python_plugin_adapter.h"`.

- [ ] **Step 3: Build everything affected and run the regression lock**

Run: `cmake --build build --target test_python_plugin_manager -j"$(nproc)" && ./build/tests/test_python_plugin_manager "[python]"`
Expected: PASS (14/107). Then build the app and CLI: `cmake --build build -j"$(nproc)" --target sicnu_geo_rs_cli` + the app target.
Expected: all link clean. If `test_python_plugin_manager` hits duplicate-symbol or missing-symbol errors, reconcile its source list with Step 2 (sources must come from exactly one place: the lib).

- [ ] **Step 4: Commit**

```bash
git add src/python/isolated/CMakeLists.txt src/python/isolated/python_plugin_adapter.* src/app/python/ CMakeLists.txt src/app/CMakeLists.txt tests/CMakeLists.txt src/core/plugin_manager.cpp
git commit -m "build(python): extract sicnu_python_isolated static lib

Moves the out-of-process hosting sources (IPC server, worker pool, proxy,
bridge, plugin adapter) into a GUI-free static library the CLI can link.
App and test targets link the lib instead of compiling sources directly."
```

---

### Task 3: `PythonPluginHost` + new headless test target

**Files:**
- Create: `src/python/isolated/python_plugin_host.h`
- Create: `src/python/isolated/python_plugin_host.cpp`
- Modify: `src/python/isolated/CMakeLists.txt` (add `python_plugin_host.cpp`)
- Create: `tests/test_python_plugin_host.cpp`
- Modify: `tests/CMakeLists.txt` (new `test_python_plugin_host` target, after `test_python_plugin_manager` block at line ~2911)

**Interfaces:**
- Consumes: `PythonPluginAdapter` (Task 1 signature), `PythonWorkerProcessPool( int poolSize, QObject * )` + `initialize( pythonPath, scriptPath )` / `shutdown()`, the metadata/`worker_daemon.py` resolution logic being moved out of `PluginManager` in Task 4 (this task writes it into the host first; Task 4 deletes the old copy).
- Produces (Tasks 4-8 rely on these exact names):
  ```cpp
  namespace sicnu::python::isolated {
  class PythonPluginHost : public QObject
  {
    Q_OBJECT
  public:
    explicit PythonPluginHost( int poolSize = 2, QObject *parent = nullptr );
    ~PythonPluginHost() override;

    PythonPluginAdapter *loadPlugin( const QString &pluginDir,
                                     sicnu::data::DataManager *dataManager,
                                     QMenu *pluginMenu,
                                     ActiveViewHost *activeViewHost,
                                     QString *errorOut = nullptr );
    void unloadAll();
    QStringList loadedPlugins() const;
    PythonWorkerProcessPool *pool() const { return m_pool; }
  };
  }
  ```

- [ ] **Step 1: Write the failing test**

Create `tests/test_python_plugin_host.cpp`:

```cpp
// test_python_plugin_host.cpp — headless Python Plugin Host seam tests (ADR 0023)
#include <catch2/catch.hpp>

#include "python_plugin_host.h"
#include "python_plugin_adapter.h"
#include "data/data_manager.h"

#include <QCoreApplication>
#include <QDir>

using namespace sicnu::python::isolated;

TEST_CASE( "PythonPluginHost loads a Python plugin headlessly with a real DataManager", "[python][host]" )
{
  sicnu::data::DataManager dataManager;
  PythonPluginHost host( 2 );

  const QString pluginDir = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "plugins/sample_plugin" ) );
  QString error;
  PythonPluginAdapter *adapter = host.loadPlugin( pluginDir, &dataManager, nullptr, nullptr, &error );
  INFO( error.toStdString() );
  REQUIRE( adapter != nullptr );
  CHECK( !adapter->name().isEmpty() );
  CHECK( host.loadedPlugins() == QStringList{ adapter->name() } );

  host.unloadAll();
  CHECK( host.loadedPlugins().isEmpty() );
}

TEST_CASE( "PythonPluginHost reports a clean error for a missing plugin directory", "[python][host]" )
{
  sicnu::data::DataManager dataManager;
  PythonPluginHost host( 2 );

  QString error;
  CHECK( host.loadPlugin( QStringLiteral( "/nonexistent/plugin/dir" ), &dataManager, nullptr, nullptr, &error ) == nullptr );
  CHECK( !error.isEmpty() );
}
```

Add the test target to `tests/CMakeLists.txt` after the `test_python_plugin_manager` block:

```cmake
qt_add_executable(test_python_plugin_host
  test_python_plugin_host.cpp
)
target_link_libraries(test_python_plugin_host PRIVATE
  Catch2::Catch2
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  sicnu_python_isolated
  sicnu_processing
  sicnu_operators
  sicnu_task_center
  sicnu_jobs
  sicnu_workflow
  Sicnu::data
  qgis_core
  GDAL::GDAL
)
target_include_directories(test_python_plugin_host PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_SOURCE_DIR}/src/python/isolated
  ${CMAKE_BINARY_DIR}
)
target_compile_definitions(test_python_plugin_host PRIVATE
  SICNU_EMBED_PYTHON=1
  TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)
sicnu_discover_tests(test_python_plugin_host)
```

(If Catch2's header is `<catch2/catch_test_macros.hpp>` in this repo, match the include used by `test_python_plugin_manager.cpp`.)

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_python_plugin_host -j"$(nproc)"`
Expected: BUILD FAILURE — `python_plugin_host.h` does not exist.

- [ ] **Step 3: Implement `PythonPluginHost`**

Create `src/python/isolated/python_plugin_host.h`:

```cpp
// src/python/isolated/python_plugin_host.h
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

class QMenu;
class ActiveViewHost;

namespace sicnu::data
{
class DataManager;
}

namespace sicnu::python::isolated
{

class PythonPluginAdapter;
class PythonWorkerProcessPool;

/**
 * @brief GUI-free lifecycle owner for Python plugins (ADR 0023).
 *
 * Owns the PythonWorkerProcessPool and the full plugin lifecycle: metadata
 * parse, adapter creation, worker acquisition, classFactory init, and unload.
 * Headless surfaces (CLI, later MCP) consume it directly; the desktop
 * PluginManager composes it and adds menu/window wiring. The host holds no
 * loading policy — callers decide which plugin directories to load.
 */
class PythonPluginHost : public QObject
{
  Q_OBJECT

  public:
    explicit PythonPluginHost( int poolSize = 2, QObject *parent = nullptr );
    ~PythonPluginHost() override;

    /**
     * Loads the Python plugin at @a pluginDir (metadata.txt + __init__.py).
     * Returns the adapter (the host retains ownership) or nullptr on failure,
     * in which case @a errorOut receives the reason.
     */
    PythonPluginAdapter *loadPlugin( const QString &pluginDir,
                                     sicnu::data::DataManager *dataManager,
                                     QMenu *pluginMenu,
                                     ActiveViewHost *activeViewHost,
                                     QString *errorOut = nullptr );

    void unloadAll();
    QStringList loadedPlugins() const;

    PythonWorkerProcessPool *pool() const { return m_pool; }

  private:
    bool ensurePool( QString *errorOut );

    int m_poolSize = 2;
    PythonWorkerProcessPool *m_pool = nullptr; // owned
    std::vector<std::unique_ptr<PythonPluginAdapter>> m_adapters;
};

} // namespace sicnu::python::isolated
```

Create `src/python/isolated/python_plugin_host.cpp` (the metadata parse and daemon-path candidates are moved verbatim from `plugin_manager.cpp:118-173` — Task 4 deletes the old copy):

```cpp
// src/python/isolated/python_plugin_host.cpp
#include "python_plugin_host.h"

#include "python_plugin_adapter.h"
#include "python_worker_process_pool.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

using namespace sicnu::python::isolated;

PythonPluginHost::PythonPluginHost( int poolSize, QObject *parent )
  : QObject( parent )
  , m_poolSize( poolSize )
{
}

PythonPluginHost::~PythonPluginHost()
{
  unloadAll();
  if ( m_pool )
  {
    m_pool->shutdown();
    delete m_pool;
    m_pool = nullptr;
  }
}

bool PythonPluginHost::ensurePool( QString *errorOut )
{
  if ( m_pool )
    return true;

  // Resolve worker_daemon.py from common layouts: installed app, source tree
  // relative to the binary, and cwd when developing from the repo root.
  const QStringList candidates = {
    QDir( QCoreApplication::applicationDirPath() ).filePath( QStringLiteral( "../src/python/scripts/worker_daemon.py" ) ),
    QDir( QCoreApplication::applicationDirPath() ).filePath( QStringLiteral( "../../src/python/scripts/worker_daemon.py" ) ),
    QDir::current().filePath( QStringLiteral( "src/python/scripts/worker_daemon.py" ) ),
    QDir::current().filePath( QStringLiteral( "../src/python/scripts/worker_daemon.py" ) ),
  };
  QString scriptPath;
  for ( const QString &candidate : candidates )
  {
    if ( QFileInfo::exists( candidate ) )
    {
      scriptPath = QFileInfo( candidate ).absoluteFilePath();
      break;
    }
  }
  if ( scriptPath.isEmpty() )
  {
    if ( errorOut ) *errorOut = QStringLiteral( "worker_daemon.py not found; Python plugins cannot load" );
    return false;
  }

  m_pool = new PythonWorkerProcessPool( m_poolSize, this );
  if ( !m_pool->initialize( QString(), scriptPath ) )
  {
    if ( errorOut ) *errorOut = QStringLiteral( "PythonWorkerProcessPool initialize failed: %1" ).arg( scriptPath );
    delete m_pool;
    m_pool = nullptr;
    return false;
  }
  return true;
}

PythonPluginAdapter *PythonPluginHost::loadPlugin( const QString &pluginDir,
                                                   sicnu::data::DataManager *dataManager,
                                                   QMenu *pluginMenu,
                                                   ActiveViewHost *activeViewHost,
                                                   QString *errorOut )
{
  const QString metadataPath = pluginDir + QStringLiteral( "/metadata.txt" );
  if ( !QFileInfo::exists( metadataPath ) )
  {
    if ( errorOut ) *errorOut = QStringLiteral( "No metadata.txt in %1" ).arg( pluginDir );
    return nullptr;
  }

  QMap<QString, QString> metadata;
  QFile file( metadataPath );
  if ( file.open( QIODevice::ReadOnly | QIODevice::Text ) )
  {
    QTextStream in( &file );
    while ( !in.atEnd() )
    {
      QString line = in.readLine().trimmed();
      if ( line.isEmpty() || line.startsWith( '#' ) || line.startsWith( '[' ) )
        continue;
      const int idx = line.indexOf( '=' );
      if ( idx > 0 )
        metadata[line.left( idx ).trimmed()] = line.mid( idx + 1 ).trimmed();
    }
  }

  if ( !ensurePool( errorOut ) )
    return nullptr;

  const QString packageName = QDir( pluginDir ).dirName();
  const QString name = metadata.value( QStringLiteral( "name" ), packageName );
  const QString description = metadata.value( QStringLiteral( "description" ), QString() );
  const QString version = metadata.value( QStringLiteral( "version" ), QStringLiteral( "1.0" ) );

  auto adapter = std::make_unique<PythonPluginAdapter>( pluginDir, packageName, name, description, version,
                                                        dataManager, pluginMenu, activeViewHost, m_pool );
  if ( !adapter->initialize( nullptr, nullptr ) )
  {
    if ( errorOut ) *errorOut = QStringLiteral( "Python plugin initialization failed: %1" ).arg( name );
    return nullptr;
  }

  PythonPluginAdapter *raw = adapter.get();
  m_adapters.push_back( std::move( adapter ) );
  return raw;
}

void PythonPluginHost::unloadAll()
{
  for ( auto &adapter : m_adapters )
  {
    adapter->unload();
  }
  m_adapters.clear();
}

QStringList PythonPluginHost::loadedPlugins() const
{
  QStringList names;
  for ( const auto &adapter : m_adapters )
    names << adapter->name();
  return names;
}
```

Add `python_plugin_host.cpp` to `src/python/isolated/CMakeLists.txt`.

- [ ] **Step 4: Run the new suite + the regression lock**

Run: `cmake --build build --target test_python_plugin_host -j"$(nproc)" && ./build/tests/test_python_plugin_host "[python]"`
Expected: PASS — 2 cases (headless load of `data/plugins/sample_plugin` with real worker processes; clean error for missing dir).
Run: `./build/tests/test_python_plugin_manager "[python]"`
Expected: PASS (14/107 — untouched).

- [ ] **Step 5: Commit**

```bash
git add src/python/isolated/python_plugin_host.h src/python/isolated/python_plugin_host.cpp src/python/isolated/CMakeLists.txt tests/test_python_plugin_host.cpp tests/CMakeLists.txt
git commit -m "feat(python): PythonPluginHost — GUI-free plugin lifecycle owner

Owns the worker pool, metadata parse, adapter creation and classFactory
lifecycle with an injected DataManager; headless surfaces consume it
directly. Covered by a new real-worker headless test target."
```

---

### Task 4: `PluginManager` thins to a GUI adapter composing the host

**Files:**
- Modify: `src/core/plugin_manager.h` (members + accessors)
- Modify: `src/core/plugin_manager.cpp` (dtor, `loadPythonPlugin`, `unloadAll`)
- Test: `tests/test_python_plugin_manager.cpp` (regression only — no edits)

**Interfaces:**
- Consumes: Task 3's `PythonPluginHost`.
- Produces: none new for headless consumers (GUI-facing behavior is unchanged).

- [ ] **Step 1: Verify no external users of the pool accessors**

Run: `grep -rn "setPythonWorkerProcessPool\|pythonWorkerProcessPool" src/ tests/ --include=*.cpp --include=*.h | grep -v "src/core/plugin_manager"`
Expected: no hits. If there are hits, stop and reconcile before proceeding.

- [ ] **Step 2: Thin the header**

In `src/core/plugin_manager.h`:
- Replace the pool accessor pair with host access:

```cpp
    sicnu::python::isolated::PythonPluginHost *pythonPluginHost() const { return m_pythonHost.get(); }
```

- Replace the forward declaration block and members:

```cpp
namespace sicnu::python::isolated {
    class PythonPluginHost;
}
```

```cpp
    std::unique_ptr<sicnu::python::isolated::PythonPluginHost> m_pythonHost;
```

(add `#include <memory>`; delete `m_pythonPool` and `m_ownsPythonPool`.)

- [ ] **Step 3: Thin the cpp**

In `src/core/plugin_manager.cpp`:
- Includes inside the `SICNU_EMBED_PYTHON` guard: replace `python/isolated/python_worker_process_pool.h` with `python/isolated/python_plugin_host.h` (keep the adapter include).
- Dtor: delete the pool teardown block (the host member handles it); keep `unloadAll()`.
- `loadPythonPlugin`: replace the whole metadata-parse + pool-create + adapter-create body (lines 118-192) with:

```cpp
    if ( !m_pythonHost )
    {
        m_pythonHost = std::make_unique<PythonPluginHost>( 2, this );
    }

    QMenu *pluginMenu = m_appInterface ? m_appInterface->pluginMenu() : nullptr;
    sicnu::data::DataManager *dataManager = nullptr;
    if ( m_appInterface && m_appInterface->projectContext() )
    {
        dataManager = &m_appInterface->projectContext()->dataManager();
    }
    ActiveViewHost *activeViewHost = m_appInterface ? m_appInterface->activeViewHost() : nullptr;

    QString error;
    PythonPluginAdapter *adapter = m_pythonHost->loadPlugin( pluginDir, dataManager, pluginMenu, activeViewHost, &error );
    if ( !adapter )
    {
        emit pluginError( QDir( pluginDir ).dirName(), error );
        qWarning() << "PluginManager: Python plugin load failed:" << pluginDir << error;
        return false;
    }

    PluginInfo info;
    info.instance = adapter; // non-owning: the host owns adapters
    info.loader = nullptr;
    info.loaded = true;
    info.isPython = true;
    m_plugins[adapter->name()] = info;

    qDebug() << "Loaded Python plugin:" << adapter->name();
    emit pluginLoaded( adapter->name() );

    return true;
```

- `unloadAll()`: Python entries are no longer deleted per-entry (the host owns them) — replace the loop body's `else if (it.value().isPython) { delete it.value().instance; }` branch with nothing (skip), and after the loop add host teardown BEFORE `m_plugins.clear()`:

```cpp
#if defined( SICNU_EMBED_PYTHON ) && SICNU_EMBED_PYTHON
    if ( m_pythonHost ) {
        m_pythonHost->unloadAll();
    }
#endif
```

- [ ] **Step 4: Build and run the regression lock**

Run: `cmake --build build --target test_python_plugin_manager -j"$(nproc)" && ./build/tests/test_python_plugin_manager "[python]"`
Expected: PASS (14/107). Build the app target too — Expected: links clean.

- [ ] **Step 5: Commit**

```bash
git add src/core/plugin_manager.h src/core/plugin_manager.cpp
git commit -m "refactor(python): PluginManager composes PythonPluginHost

GUI PluginManager keeps C++ plugins, directory scanning and menu wiring;
the Python hosting machinery (pool, metadata, adapter lifecycle) lives in
the composed PythonPluginHost. No GUI behavior change."
```

---

### Task 5: `py:` JobEngine prefix executor with main-thread marshaling

**Files:**
- Modify: `src/python/isolated/python_plugin_host.h` (private method declaration)
- Modify: `src/python/isolated/python_plugin_host.cpp` (file-static context + executor + ctor registration)
- Test: `tests/test_python_plugin_host.cpp` (new `[python][host][exec]` TEST_CASE)

**Interfaces:**
- Consumes: `JobEngine::instance().registerExecutor( const std::string &prefix, JobExecutor )` (`src/jobs/job_engine.h:74`); `JobExecutor = std::function<Json::Value( const JobRequest &, RSOperatorContext & )>` (`job_engine.h:44-45`); `JobRequest.algorithmId` / `JobRequest.params` (`src/jobs/job_types.h:22-30`); `AlgorithmEngine::instance().executeAlgorithm( const QString &id, const QVariantMap &params, std::function<void(double)>, QString &error )` (`algorithm_engine.h:69`); `jsonParamsToVariantMap( const Json::Value & )` (`src/processing/framework/json_params_converter.h` — verify the exact name with `grep -n "jsonParamsToVariantMap" src/processing/framework/json_params_converter.h`); the daemon's `processing.test_register_algorithm` helper (ticket 02).
- Produces (Tasks 6-8 rely on this): any `JobRequest` whose `algorithmId` starts with `py:` executes through the marshaling executor as long as a live `PythonPluginHost` exists on the main thread.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_python_plugin_host.cpp`:

```cpp
#include "python_ipc_server.h"
#include "python_worker_process_pool.h"
#include "processing/framework/algorithm_engine.h"
#include "jobs/job_engine.h"
#include "jobs/job_types.h"

#include <QThread>
#include <QTest>

TEST_CASE( "py: prefix executor executes from a worker thread marshaled to the main thread", "[python][host][exec]" )
{
  using namespace sicnu::jobs;

  sicnu::data::DataManager dataManager;
  PythonPluginHost host( 2 );

  // Occupy one worker with the sample plugin, then use the second worker to
  // register py:echo_test through the daemon's public-path test helper.
  const QString pluginDir = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "plugins/sample_plugin" ) );
  QString loadError;
  REQUIRE( host.loadPlugin( pluginDir, &dataManager, nullptr, nullptr, &loadError ) != nullptr );

  WorkerNode *node = host.pool()->acquireWorker();
  REQUIRE( node != nullptr );
  REQUIRE( node->server != nullptr );

  QJsonObject regResult;
  bool regIsError = false;
  REQUIRE( node->server->sendRequestAndAwait( QStringLiteral( "processing.test_register_algorithm" ),
                                              QJsonObject(), regResult, regIsError, 10000 )
           == AwaitStatus::Ok );
  REQUIRE( !regIsError );

  SECTION( "registered py: algorithm succeeds via JobEngine (worker thread caller)" )
  {
    JobRequest req;
    req.algorithmId = "py:echo_test";
    req.params["value"] = 42;
    const std::string jobId = JobEngine::instance().submit( req );

    // The main thread must pump events while jobs run — this is exactly the
    // CLI runner pattern; without it the marshaled call deadlocks.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 30 );
    for ( ;; )
    {
      QCoreApplication::processEvents();
      const auto snap = JobEngine::instance().snapshot( jobId );
      if ( snap && ( snap->state == JobState::Succeeded || snap->state == JobState::Failed ) )
      {
        INFO( snap->error );
        CHECK( snap->state == JobState::Succeeded );
        break;
      }
      if ( std::chrono::steady_clock::now() > deadline )
      {
        FAIL( "py: job did not finish within 30 s (marshaling deadlock?)" );
        break;
      }
      QThread::msleep( 5 );
    }
  }

  SECTION( "unknown py: algorithm fails cleanly via JobEngine" )
  {
    JobRequest req;
    req.algorithmId = "py:ghost_unknown";
    const std::string jobId = JobEngine::instance().submit( req );

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 30 );
    for ( ;; )
    {
      QCoreApplication::processEvents();
      const auto snap = JobEngine::instance().snapshot( jobId );
      if ( snap && ( snap->state == JobState::Succeeded || snap->state == JobState::Failed ) )
      {
        CHECK( snap->state == JobState::Failed );
        CHECK( snap->error.find( "Unknown algorithm" ) != std::string::npos );
        break;
      }
      if ( std::chrono::steady_clock::now() > deadline )
      {
        FAIL( "py: job did not finish within 30 s" );
        break;
      }
      QThread::msleep( 5 );
    }
  }

  host.pool()->releaseWorker( node );
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_python_plugin_host -j"$(nproc)" && ./build/tests/test_python_plugin_host "[python][host][exec]"`
Expected: FAIL — `py:echo_test` job fails (`Unknown algorithm` from JobEngine, no prefix executor registered yet).

- [ ] **Step 3: Implement the marshaling executor**

In `src/python/isolated/python_plugin_host.cpp`, add includes:

```cpp
#include "processing/framework/algorithm_engine.h"
#include "processing/framework/json_params_converter.h"
#include "jobs/job_engine.h"

#include <QPointer>
#include <QThread>
```

Add above the class implementation (anonymous namespace):

```cpp
namespace
{

/// Main-thread context for marshaled py: execution. Written by the most
/// recently constructed PythonPluginHost (main thread, before any job runs);
/// QPointer self-clears when the host dies. py: adapters and the IPC server
/// are main-thread citizens (ticket 02), so worker-thread jobs must marshal.
QPointer<QObject> g_pyMainContext;

Json::Value runPythonPrefixJob( const sicnu::jobs::JobRequest &req,
                                sicnu::operators::RSOperatorContext &operatorContext )
{
  Q_UNUSED( operatorContext );
  if ( g_pyMainContext.isNull() )
  {
    throw std::runtime_error( "No PythonPluginHost alive on the main thread for py: execution" );
  }

  const QString algoId = QString::fromStdString( req.algorithmId );
  const QVariantMap params = sicnu::jsonParamsToVariantMap( req.params );

  QString error;
  bool ok = false;
  auto execute = [&]() {
    ok = sicnu::AlgorithmEngine::instance().executeAlgorithm( algoId, params, nullptr, error );
  };

  if ( QThread::currentThread() == g_pyMainContext->thread() )
  {
    execute();
  }
  else if ( !QMetaObject::invokeMethod( g_pyMainContext, std::move( execute ), Qt::BlockingQueuedConnection ) )
  {
    throw std::runtime_error( "Failed to marshal py: execution to the main thread" );
  }

  if ( !ok )
  {
    throw std::runtime_error( error.toStdString() );
  }
  return Json::Value( Json::objectValue );
}

} // namespace
```

In `PythonPluginHost`'s ctor, register (re-registration is harmless: JobEngine's first-match-wins resolution keeps one effective executor, and every registration funnels through the same `g_pyMainContext`, which the newest host refreshes):

```cpp
  g_pyMainContext = this;
  sicnu::jobs::JobEngine::instance().registerExecutor( "py:", &runPythonPrefixJob );
```

(`sicnu::jsonParamsToVariantMap` — if the converter lives in a different namespace, match the `using`/qualification in `tool_call_dispatcher.cpp`.)

- [ ] **Step 4: Run the full new suite + the regression lock**

Run: `cmake --build build --target test_python_plugin_host -j"$(nproc)" && ./build/tests/test_python_plugin_host "[python]"`
Expected: PASS — host load cases + both exec sections.
Run: `./build/tests/test_python_plugin_manager "[python]"`
Expected: PASS (14/107).

- [ ] **Step 5: Commit**

```bash
git add src/python/isolated/python_plugin_host.h src/python/isolated/python_plugin_host.cpp tests/test_python_plugin_host.cpp
git commit -m "feat(python): py: JobEngine prefix executor marshaled to the main thread

Worker-thread py: jobs block on a BlockingQueuedConnection to the host's
main thread, preserving the ticket-02 threading assumption; the main
thread pumps events while pipelines run. Unknown ids fail cleanly."
```

---

### Task 6: Runner event pumping + `py:` pipeline end-to-end test

**Files:**
- Modify: `src/cli/rs_pipeline_runner.cpp` (include + one line in the wait loop, ~line 232-236)
- Create: `tests/data/plugins/echo_plugin/metadata.txt`
- Create: `tests/data/plugins/echo_plugin/__init__.py`
- Test: `tests/test_python_plugin_host.cpp` (new `[python][host][pipeline]` TEST_CASE)
- Modify: `tests/CMakeLists.txt` (add `rs_pipeline_runner.cpp` source + `sicnu_cli` include if needed to `test_python_plugin_host`)

**Interfaces:**
- Consumes: Task 5's registered `py:` executor; `RsPipelineRunner::runFromJson( const Json::Value & )` (`rs_pipeline_runner.h:74`).
- Produces: the guarantee that `RsPipelineRunner` delivers marshaled `py:` executions while waiting (Tasks 7-8 build on it).

- [ ] **Step 1: Create the test plugin fixture**

Create `tests/data/plugins/echo_plugin/metadata.txt`:

```
[general]
name=Echo Test Plugin
description=Headless test plugin registering py:echo_plugin
version=1.0
```

Create `tests/data/plugins/echo_plugin/__init__.py`:

```python
def classFactory(iface):
    iface.registerProcessingAlgorithm(
        "py:echo_plugin",
        "Echo Plugin",
        execute_fn=lambda p: {"echo": p},
    )
    return EchoPlugin()


class EchoPlugin:
    def initGui(self):
        pass

    def unload(self):
        pass
```

- [ ] **Step 2: Write the failing test**

Append to `tests/test_python_plugin_host.cpp`:

```cpp
#include "cli/rs_pipeline_runner.h"

TEST_CASE( "CLI runner executes a py: pipeline step end-to-end", "[python][host][pipeline]" )
{
  sicnu::data::DataManager dataManager;
  PythonPluginHost host( 2 );

  const QString pluginDir = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "plugins/echo_plugin" ) );
  QString error;
  INFO( error.toStdString() );
  REQUIRE( host.loadPlugin( pluginDir, &dataManager, nullptr, nullptr, &error ) != nullptr );

  sicnu::cli::RsPipelineRunner runner;
  Json::Value pipeline( Json::objectValue );
  pipeline["name"] = "echo-pipeline";
  Json::Value step( Json::objectValue );
  step["id"] = "s1";
  step["operator"] = "py:echo_plugin";
  step["params"]["value"] = 7;
  pipeline["steps"].append( step );

  const auto result = runner.runFromJson( pipeline );
  INFO( result.errorMessage );
  CHECK( result.success );
  REQUIRE( result.steps.size() == 1 );
  CHECK( result.steps[0].success );
}
```

In `tests/CMakeLists.txt`, add to `test_python_plugin_host` sources:

```cmake
  ${CMAKE_SOURCE_DIR}/src/cli/rs_pipeline_runner.cpp
```

and change its `TEST_DATA_DIR` define to `"${CMAKE_SOURCE_DIR}/tests/data"` (the fixture lives there; the earlier host tests use `data/plugins/sample_plugin` — update those two paths in `test_python_plugin_host.cpp` from `plugins/sample_plugin` to `../data/plugins/sample_plugin`, or keep `TEST_DATA_DIR` as `${CMAKE_SOURCE_DIR}/data` and reference the fixture as `../tests/data/plugins/echo_plugin`. Pick one convention and apply it consistently).

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target test_python_plugin_host -j"$(nproc)" && ./build/tests/test_python_plugin_host "[python][host][pipeline]"` — with a 60 s timeout on the command (`timeout 60 ./build/tests/...`).
Expected: FAIL or TIMEOUT — the runner's wait loop never pumps events, so the marshaled `py:` execution is never delivered (this is the deadlock the next step fixes). Do not wait out the 30-minute pipeline timeout; the `timeout 60` kill is the expected red signal.

- [ ] **Step 4: Pump events in the runner wait loop**

In `src/cli/rs_pipeline_runner.cpp`:
- Add `#include <QCoreApplication>` to the includes.
- In the wait loop (after the `waitForPipeline` call at line 235), add:

```cpp
    // Deliver marshaled py: executions (ADR 0023): the py: prefix executor
    // blocks a JobEngine worker on a BlockingQueuedConnection to this thread.
    QCoreApplication::processEvents();
```

- [ ] **Step 5: Run the full suites**

Run: `cmake --build build --target test_python_plugin_host -j"$(nproc)" && ./build/tests/test_python_plugin_host "[python]"`
Expected: PASS — all cases including the pipeline test (now completes in seconds).
Run: `./build/tests/test_python_plugin_manager "[python]"`
Expected: PASS (14/107).

- [ ] **Step 6: Commit**

```bash
git add src/cli/rs_pipeline_runner.cpp tests/data/plugins/echo_plugin/ tests/test_python_plugin_host.cpp tests/CMakeLists.txt
git commit -m "feat(cli): pump Qt events in pipeline wait loop; py: pipeline e2e test

The runner's wait loop now processes events so marshaled py: executions
are delivered; adds the echo_plugin test fixture and a real-worker
pipeline test proving a py: step runs end-to-end through TaskCenter."
```

---

### Task 7: Pipeline output asset registration

**Files:**
- Modify: `src/cli/rs_pipeline_runner.h` (member + setter + private method)
- Modify: `src/cli/rs_pipeline_runner.cpp` (registration implementation; call after pipeline success in `runFromJson`, ~line 223-235 area)
- Test: `tests/test_python_plugin_host.cpp` (new `[python][host][assets]` TEST_CASE with a file-writing test operator)

**Interfaces:**
- Consumes: `TaskCenter::getPipelineInfo( long )` → `PipelineExecutionInfo.stepToTaskId` (`task_center.h:84-94`); `TaskCenter::getTaskInfo( long )` → `AlgorithmTaskInfo{ status, algorithmId, parameterMap, outputLayerPath }` (`task_center.h:52-83`); `DataManager::registerSource( const RegisterRequest & )` → `RegisterResult.assetId` (`data_manager.h:79`, `data_asset.h:28-44`); `DataManager::attachDerivationRecord( AssetId, const DerivationRecord & )` (used in `output_committer.cpp:154`); `DerivationRecord{ algorithmId, algorithmVersion, parameters, inputs, outputAssetId, taskReference, softwareVersion, completedAtUtc }` (`src/data/derivation_record.h:37-57`); `SourceDescriptor{ providerKey, canonicalSource }` (used in `output_committer.cpp:118-120`).
- Produces (Task 8 relies on this):
  ```cpp
  void RsPipelineRunner::setAssetRegistry( sicnu::data::DataManager *dataManager ); // nullptr clears (default)
  ```

- [ ] **Step 1: Write the failing test**

Append to `tests/test_python_plugin_host.cpp`:

```cpp
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"

#include <gdal.h>
#include <gdal_priv.h>

namespace
{

/// Test operator writing a tiny valid GeoTIFF to params["output"].
class TiffMakerOperator : public sicnu::operators::RSOperator
{
  public:
    std::string name() const override { return "test:tiff_maker"; }

    Json::Value run( const Json::Value &params, sicnu::operators::RSOperatorContext &context ) override
    {
      Q_UNUSED( context );
      const std::string output = params["output"].asString();
      GDALAllRegister();
      GDALDriverH driver = GDALGetDriverByName( "GTiff" );
      if ( !driver )
        throw sicnu::operators::RSOperatorError( "GTiff driver unavailable" );
      GDALDatasetH ds = GDALCreate( driver, output.c_str(), 4, 4, 1, GDT_Byte, nullptr );
      if ( !ds )
        throw sicnu::operators::RSOperatorError( "Failed to create " + output );
      GDALClose( ds );
      Json::Value result( Json::objectValue );
      result["output"] = output;
      return result;
    }
};

REGISTER_RS_OPERATOR( TiffMakerOperator, "test:tiff_maker" )

} // namespace

TEST_CASE( "CLI runner registers completed step outputs as Data Assets", "[python][host][assets]" )
{
  sicnu::data::DataManager dataManager;
  // No plugin host needed for this case — registration is operator-driven.
  const QString outputPath = QDir::temp().filePath( QStringLiteral( "sicnu_tiff_maker_test.tif" ) );
  QFile::remove( outputPath );

  sicnu::cli::RsPipelineRunner runner;
  runner.setAssetRegistry( &dataManager );

  Json::Value pipeline( Json::objectValue );
  pipeline["name"] = "tiff-pipeline";
  Json::Value step( Json::objectValue );
  step["id"] = "s1";
  step["operator"] = "test:tiff_maker";
  step["params"]["output"] = outputPath.toStdString();
  pipeline["steps"].append( step );

  const auto result = runner.runFromJson( pipeline );
  INFO( result.errorMessage );
  REQUIRE( result.success );

  const auto assets = dataManager.assets();
  REQUIRE( assets.size() == 1 );
  CHECK( assets[0].canonicalSource == outputPath ); // adjust to the AssetSnapshot field name
  CHECK( dataManager.provenance( assets[0].id ).has_value() ); // adjust to the AssetSnapshot id field

  QFile::remove( outputPath );
}
```

(Verify the `AssetSnapshot` field names with `grep -n "struct AssetSnapshot" -A 20 src/data/*.h` and `RSOperatorError`'s ctor with `grep -n "class RSOperatorError" -A 10 src/operators/framework/rs_operator_error.h`; adjust the two marked lines.)

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_python_plugin_host -j"$(nproc)" && ./build/tests/test_python_plugin_host "[python][host][assets]"`
Expected: BUILD FAILURE — `setAssetRegistry` does not exist.

- [ ] **Step 3: Implement the registration**

In `src/cli/rs_pipeline_runner.h`:
- Add forward declaration `namespace sicnu::data { class DataManager; }` (before `namespace sicnu::cli`).
- Add to the public section:

```cpp
    /**
     * \brief Optional DataManager receiving completed step outputs as
     * TaskTemporary Data Assets (ADR 0023). Null by default: no registration.
     */
    void setAssetRegistry(sicnu::data::DataManager* dataManager);
```

- Add to the private section:

```cpp
    void registerStepOutputs(long pipelineId);
    sicnu::data::DataManager* m_dataManager = nullptr; // not owned
```

In `src/cli/rs_pipeline_runner.cpp`:
- Add includes:

```cpp
#include "data/data_manager.h"
#include "data/data_asset.h"
#include "data/derivation_record.h"
#include "data/source_descriptor.h"

#include <gdal.h>

#include <QDateTime>
#include <QJsonObject>
```

- Implement:

```cpp
void RsPipelineRunner::setAssetRegistry(sicnu::data::DataManager* dataManager)
{
    m_dataManager = dataManager;
}

void RsPipelineRunner::registerStepOutputs(long pipelineId)
{
    auto& taskCenter = sicnu::TaskCenter::instance();
    const auto pipeInfo = taskCenter.getPipelineInfo(pipelineId);
    for (const long taskId : pipeInfo.stepToTaskId) {
        const auto task = taskCenter.getTaskInfo(taskId);
        if (task.status != sicnu::TaskStatus::Completed || task.outputLayerPath.isEmpty())
            continue;

        const QString path = task.outputLayerPath;
        ensureGdalInit();
        GDALDatasetH ds = GDALOpenEx(path.toUtf8().constData(),
                                     GDAL_OF_READONLY | GDAL_OF_RASTER | GDAL_OF_VECTOR,
                                     nullptr, nullptr, nullptr);
        if (!ds) {
            reportLog("warning", "Skipping asset registration; output not openable: "
                      + path.toStdString());
            continue;
        }
        const bool isRaster = GDALGetRasterCount(ds) > 0;
        GDALClose(ds);

        sicnu::data::SourceDescriptor source;
        source.providerKey = isRaster ? QStringLiteral("gdal") : QStringLiteral("ogr");
        source.canonicalSource = path;

        sicnu::data::RegisterRequest request;
        request.source = source;
        request.persistence = sicnu::data::PersistencePolicy::TaskTemporary;

        const auto registered = m_dataManager->registerSource(request);
        if (registered.assetId.isNull()) {
            reportLog("warning", "Asset registration failed for: " + path.toStdString());
            continue;
        }

        sicnu::data::DerivationRecord derivation;
        derivation.algorithmId = task.algorithmId;
        derivation.parameters = QJsonObject::fromVariantMap(task.parameterMap);
        derivation.taskReference = QString::number(taskId);
        derivation.completedAtUtc = QDateTime::currentDateTimeUtc();
        m_dataManager->attachDerivationRecord(registered.assetId, derivation);
    }
}
```

- In `runFromJson`, locate where the pipeline is known to have succeeded (after the wait loop, where `result.success` is set) and capture the `pipelineId` returned by `submitPipeline` (line ~223) into a local; after success add:

```cpp
    if (m_dataManager && result.success) {
        registerStepOutputs(pipelineId);
    }
```

(If `pipelineId` is scoped inside a block, hoist it. `ensureGdalInit` is declared in `processing/gdal/gdal_dataset_wrapper.h` — already used by `main_cli.cpp`.)

- [ ] **Step 4: Run the suites + link the CLI against sicnu_data**

In `src/cli/CMakeLists.txt`, add `Sicnu::data` to `target_link_libraries(sicnu_geo_rs_cli PRIVATE ...)` (needed by Task 8; harmless now since the runner object already references DataManager symbols via the test target's link of `Sicnu::data`).

Run: `cmake --build build --target test_python_plugin_host -j"$(nproc)" && ./build/tests/test_python_plugin_host "[python]"` and `cmake --build build --target sicnu_geo_rs_cli -j"$(nproc)"`
Expected: PASS all cases; CLI links clean.

- [ ] **Step 5: Commit**

```bash
git add src/cli/rs_pipeline_runner.h src/cli/rs_pipeline_runner.cpp src/cli/CMakeLists.txt tests/test_python_plugin_host.cpp
git commit -m "feat(cli): register pipeline step outputs as TaskTemporary Data Assets

Completed pipeline outputs are registered via DataManager::registerSource
with a Derivation Record (registerSource + attachDerivationRecord, not
OutputCommitter — its temp→stable rename and DeletableSource semantics do
not fit user-declared final paths)."
```

---

### Task 8: CLI wiring — `--python-plugin` + DataManager + host

**Files:**
- Modify: `src/cli/main_cli.cpp`
- Modify: `src/cli/CMakeLists.txt` (link `sicnu_python_isolated`)

**Interfaces:**
- Consumes: Task 3's `PythonPluginHost::loadPlugin`, Task 7's `RsPipelineRunner::setAssetRegistry`.
- Produces: none (terminal wiring).

- [ ] **Step 1: Wire the CLI**

In `src/cli/main_cli.cpp`:
- Add includes:

```cpp
#include "python/isolated/python_plugin_host.h"
#include "data/data_manager.h"

#include <memory>
```

- Add the option after `schemaOption` (line ~46):

```cpp
    const QCommandLineOption pythonPluginOption(
        QStringList() << "python-plugin",
        "Load the Python plugin directory before running the pipeline (repeatable).",
        "dir");
    parser.addOption(pythonPluginOption);
```

- After `ensureGdalInit();` (line 50), add the fail-fast plugin loading block:

```cpp
    // Headless Python Plugin Host (ADR 0023): explicit declaration only.
    const QStringList pythonPluginDirs = parser.values(pythonPluginOption);
    std::unique_ptr<sicnu::data::DataManager> dataManager;
    std::unique_ptr<sicnu::python::isolated::PythonPluginHost> pythonHost;
    if (!pythonPluginDirs.isEmpty()) {
        dataManager = std::make_unique<sicnu::data::DataManager>();
        pythonHost = std::make_unique<sicnu::python::isolated::PythonPluginHost>(2);
        for (const QString& dir : pythonPluginDirs) {
            QString error;
            if (!pythonHost->loadPlugin(dir, dataManager.get(), nullptr, nullptr, &error)) {
                std::cerr << "Failed to load Python plugin '" << dir.toStdString()
                          << "': " << error.toStdString() << "\n";
                return 1;
            }
            std::cout << "Loaded Python plugin: " << dir.toStdString() << "\n";
        }
    }
```

- Before constructing the runner result (line 94-95), pass the registry:

```cpp
    RsPipelineRunner runner(progressCb, logCb);
    if (dataManager) {
        runner.setAssetRegistry(dataManager.get());
    }
    const auto result = runner.runFromFile(pipelinePath.toStdString());
```

`pythonHost`/`dataManager` outlive the run and are destroyed before `app` teardown (declare them after `app`, before `runner` — declaration order gives correct reverse destruction).

- In `src/cli/CMakeLists.txt`, add `sicnu_python_isolated` to `target_link_libraries(sicnu_geo_rs_cli PRIVATE ...)`.

- [ ] **Step 2: Build and smoke-test the binary**

Run: `cmake --build build --target sicnu_geo_rs_cli -j"$(nproc)"`

Create a smoke pipeline `/tmp/echo_pipeline.json`:

```json
{
  "name": "echo",
  "steps": [
    {"id": "s1", "operator": "py:echo_plugin", "params": {"value": 1}}
  ]
}
```

Run from the repo root (the host resolves `worker_daemon.py` via the cwd candidates):

```bash
./build/sicnu_geo_rs_cli --pipeline /tmp/echo_pipeline.json --python-plugin tests/data/plugins/echo_plugin
```

Expected: `Loaded Python plugin: tests/data/plugins/echo_plugin`, then `Pipeline succeeded (1 steps)`, exit 0.

Negative checks:
- `./build/sicnu_geo_rs_cli --pipeline /tmp/echo_pipeline.json` (no plugin) → Expected: `Pipeline failed:` mentioning `py:echo_plugin` (unknown algorithm), exit 1.
- `./build/sicnu_geo_rs_cli --pipeline /tmp/echo_pipeline.json --python-plugin /nonexistent` → Expected: `Failed to load Python plugin` + exit 1, before any pipeline output.

- [ ] **Step 3: Run the full verification matrix**

```bash
./build/tests/test_python_plugin_manager "[python]"
./build/tests/test_python_plugin_host "[python]"
cmake --build build -j"$(nproc)"   # full build, all targets
```

Expected: all green.

- [ ] **Step 4: Commit**

```bash
git add src/cli/main_cli.cpp src/cli/CMakeLists.txt
git commit -m "feat(cli): --python-plugin loads Python plugins in headless pipelines

CLI creates a DataManager + PythonPluginHost, loads declared plugins
fail-fast before the pipeline starts, registers step outputs as assets,
and executes py: steps via the marshaling prefix executor (ADR 0023)."
```

---

### Task 9: Spec status flip + consistency sweep

**Files:**
- Modify: `docs/superpowers/specs/2026-08-01-cli-python-plugin-host-spec.md` (status line)

- [ ] **Step 1: Flip the spec status**

Change `**Status:** Draft — pending user review` to `**Status:** Implemented`.

- [ ] **Step 2: Consistency sweep**

- `grep -rn "PythonPluginHost" CONTEXT.md src/python/isolated/` — the term in code matches the CONTEXT.md entry.
- `grep -n "OutputCommitter" docs/superpowers/specs/2026-08-01-cli-python-plugin-host-spec.md` — the spec must describe the registerSource decision (already updated by the plan author; confirm no stale text).
- Confirm `./build/tests/test_python_plugin_manager "[python]"` and `./build/tests/test_python_plugin_host "[python]"` both pass from a clean shell.

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-08-01-cli-python-plugin-host-spec.md
git commit -m "docs: mark CLI Python Plugin Host spec implemented"
```

---

## Self-Review Notes (completed by plan author)

- **Spec coverage:** §1 host extraction (Tasks 1-3); §2 PluginManager thinning (Task 4); §3 CLI wiring incl. fail-fast load and declaration-only policy (Task 8); §4 marshaling executor + runner event pumping (Tasks 5-6); §5 output asset registration — **revised during planning** to `registerSource` + `attachDerivationRecord` after verifying `OutputCommitter::commit` renames temp→stable and asserts `DeletableSource` (`output_committer.cpp:103-128`), which would delete-then-fail on `stablePath == tempPath` and hand DataManager deletion rights over user-declared final outputs; the spec and ADR 0023 were updated to match (Task 7); §6 worker lifecycle (host dtor shuts the pool down before app teardown — Tasks 3/8); Testing Decisions 1-3 (Tasks 3, 5, 6; regression lock runs in every task); Out of Scope respected (no MCP allow-list change, no `processing:` executor in CLI, no `sendRequestAndAwait` refactor, no daemon changes).
- **Deviations from the spec (spec text updated accordingly):** (1) output registration uses `registerSource` instead of `OutputCommitter` (reason above); (2) the new lib and CLI path compile unguarded by `SICNU_EMBED_PYTHON` — nothing is embedded; the guard stays only on `PluginManager`'s GUI path, unchanged.
- **Type consistency:** `PythonPluginHost::loadPlugin( pluginDir, dataManager, pluginMenu, activeViewHost, errorOut )` identical in Tasks 3/4/8; `PythonPluginAdapter` ctor identical in Tasks 1/3; `setAssetRegistry` identical in Tasks 7/8; `AwaitStatus::Ok` / `sendRequestAndAwait` reused from ticket 02 unchanged; `JobRequest{ algorithmId, params }` matches `job_types.h:22-30`.
- **Ordering guarantees relied on:** (a) the daemon sends `processing.register_algorithm` before its load/test-helper response, so adapters exist when `loadPlugin`/`sendRequestAndAwait` returns (ticket 02 precedent); (b) `g_pyMainContext` is only written by host construction on the main thread before jobs start, and `QPointer` self-clears, so the executor never dereferences a dead context; (c) `main_cli.cpp` declaration order (`app` → `dataManager`/`pythonHost` → `runner`) gives reverse-order destruction: runner, then host (pool shutdown), then DataManager, then `QCoreApplication`.
- **Known soft spots (executor discretion with verification commands given inline):** exact `jsonParamsToVariantMap` namespace; `AssetSnapshot` field names (`canonicalSource`, `id`); `RSOperatorError` ctor; `project_context.h` include path; Catch2 header include style. Each is marked at its step with the grep to resolve it.
