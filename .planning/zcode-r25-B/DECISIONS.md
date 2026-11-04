# R25-B DECISIONS

1. **构建旗组沿用 r23-B**：系统 GDAL 3.13.3、无 ONNX/TRT/OV、无 OTB/Python——Linux 宿主已验证组合；GPU 类测试 NOT_EXECUTED 如实标注。
2. **F1 放 src/data/（非 geospatial）**：预检报告的消费者是 DataManager 注册决策与 D 轨 UI/CLI；实现复用 geospatial 生产件（registry/identity/doctor 选择性），但对外契约在 data 层——与 registerSource 同层，避免第二 catalog。
3. **F1 不调用完整 doctor**（避免导入路径成本翻倍）：只取 cheap 检查（可读性/CRS/NoData 声明/波段结构），doctor 保持独立工具。预检身份用 assetIdentityToken 的 metadata/content 强度声明，不强制全文件 hash（预算内）。
4. **F2 plan 不重跑 readiness 的网络/磁盘重活**：干跑 = resolve + 静态契约门 + 网格预算（打开输入读 geotransform/尺寸/波段，不逐 tile 读像素）+ EO 预检（元数据级）。与 runModelInference 共享同一批 gate 函数，不允许旁路。
5. **F3 v2 向后兼容**：新增 deviation_details[]，保留 deviations 字符串数组与 verdict 语义；kIdentityDimensions 集合不动（#1547 修复语义的回归钉在测试里）。
6. **共享宿主**：-j2 上限、flock 单锁、ctest -j1；兄弟轨 R25-A/C 并行在跑，本轨构建排队串行，不在墙钟上设硬门槛。
