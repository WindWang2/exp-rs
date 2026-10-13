# PERFORMANCE BASELINE & PROFILING — Direction B (Geospatial Data Fabric)

**Date**: 2026-10-13  
**Target Environment**: Linux x86_64, GCC 16, Qt 6, GDAL  
**Constraints**: Host parallel limit `-j2`, CTest `-j1`  

---

## 1. 基线微基准测试 (Baseline Benchmarks)

| 指标领域 | 场景 | 基线表现 | 目标表现 | 衡量方法 |
|---|---|---|---|---|
| **Catalog Query** | In-Memory (1,000 records) | ~0.4 ms | < 0.5 ms | `test_io_fabric_catalog` |
| **Catalog Local Walk** | 100 STAC Item Files (Local VSI) | ~12 ms | < 15 ms | `test_io_fabric_catalog` |
| **Tile Read RSS** | 1024x1024 Float32 raster window read | ~18 MB peak | < 25 MB | `test_io_paths`, `test_virtual_cube_memory` |
| **Atomic Publication** | 4-member group publish + hardlink backup | ~2.1 ms | < 3.0 ms | `test_io_atomic_failures` |
| **Stage Ledger Attach** | Crash Recovery Attach Check | ~1.5 ms | < 2.0 ms | `test_io_stage_ledger` |

---

## 2. 内存与资源护栏 (Resource Guardrails)

- 杜绝整图载入：全部分块读取与遍历必须限制在 `TilePlan` 和 `windowByteBudget` 内。
- 杜绝分页死循环：任何 `searchAll` 遍历调用必须在有限轮次内收敛，严格禁止空步进循环。
