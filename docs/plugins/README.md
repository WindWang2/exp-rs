# ExpRS Developer Platform 3.0 — Developer Documentation

Welcome. This is the entry point for third-party developers, research teams,
institution tool chains, and agents extending exp-rs.

## The four surfaces

| Surface | Entry | Docs |
|---|---|---|
| C++ Plugin SDK | `find_package(ExpRS)` → `ExpRS::SDK` | [docs/sdk](../sdk/README.md) |
| Plugin system | `plugin.json` manifest + loader | [docs/plugins](../plugins/README.md) |
| Python SDK | `import exprs` | [docs/python](../python/README.md) |
| Headless CLI | `sicnu_geo_rs_cli <command>` | [docs/headless](../headless/README.md) |

## 30-second Hello Operator

```bash
# 1. install the SDK (from an exp-rs build tree)
cmake --install path/to/exp-rs-build --prefix ~/exp-rs-sdk

# 2. build the example plugin against ONLY the installed SDK
cmake -S examples/plugins/cpp-operator-demo -B /tmp/demo-build \
      -DCMAKE_PREFIX_PATH=~/exp-rs-sdk
cmake --build /tmp/demo-build

# 3. install it into the user plugin root
dest=~/.local/share/sicnu_geo_rs/plugins/org.example.cpp-operator-demo
mkdir -p "$dest"
cp examples/plugins/cpp-operator-demo/plugin.json "$dest/"
cp /tmp/demo-build/libcpp_operator_demo.so "$dest/"

# 4. it is now a first-class algorithm — same contract as built-ins
sicnu_geo_rs_cli plugin list
sicnu_geo_rs_cli run demo:stats --param 'values=[1,2,3]' --json
```

## Design rules (non-negotiable)

1. **One contract, many surfaces.** A plugin registers *once*; the operator
   appears in the Processing catalog, Workflow engine, CLI, and MCP tools.
   Plugins never write per-surface adapters.
2. **Manifests are the discovery index.** Startup never dlopens plugin
   binaries; descriptors declared in `plugin.json` power catalogs until the
   binary is actually needed (lazy loading).
3. **The C++ ABI is not assumed stable.** Versioned interfaces + a hard
   ABI gate checked *before* dlopen; see [versioning](../sdk/versioning.md).
4. **Failures diagnose, never crash.** Every plugin failure produces a
   structured `PluginDiagnostic` (E1xxx–E5xxx) and a state transition.
   Out-of-process payloads (Python, external tools) are crash-isolated.
5. **No parallel registries.** Plugin operators land in the existing
   `RSOperatorRegistry` / `AtomicAlgorithmRegistry`; plugin model runtimes in
   `ModelRuntimeRegistry`; plugin agent tools in `AgentToolCatalog`.
