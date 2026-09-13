# 技能镜像状态记录（.agents/skills/ × .claude/skills/）

Track: prompt-command-hygiene-review · Phase 6 · 2026-09-13
本文件是两侧技能目录差异的记录与解释落点。CLAUDE.md 的 Skills 节指向这里。

## 运行时映射（谁读哪个目录）

| 运行时 | 读取的项目级技能目录 | 证据 |
| --- | --- | --- |
| zcode | `.agents/skills/` | 本 track 会话的可用技能清单中，项目级技能全部解析自 `.agents\skills\` 路径；无一条来自 `.claude\skills\` |
| Claude Code | `.claude/skills/` | CLAUDE.md:64（修订后）与目录命名约定 |
| 两者共同的用户级补充 | `~/.zcode/skills/`（zcode）、`~/.claude/skills/`（Claude Code） | `ls ~/.zcode/skills/` 实测含 `gstack`、`planning-with-files` |

## 实测差异（2026-09-13，verified-by-execution）

- 共同技能 **37 个，字节级一致**（`diff -rq` 0 处 differ）。
- 仅 `.claude/skills/` 有 **13 个**：`frontend-design` + 12 个 `qt-*`（供应商技能）。
- 仅 `.agents/skills/` 有 **0 个**。
- 两侧目录均被 git 跟踪（`git ls-files`：37 / 50 个技能目录）。

## 一次性事实（历史）

- R1 track 开题时（基于 efc5c52f 前的观察）声称两侧漂移（`.agents` 有 teach、`.claude` 有 ask-matt）。实测该漂移**已被修复**：ask-matt 与 setup-matt-pocock-skills 双侧均在。开题信息过时，不构成当前缺陷（PROMPT_DEFECTS.md 撤下记录 R-1）。
- `karpathy-guidelines` 与 `gstack` 59 技能套件：`CHANGELOG.md:1017` 记录 2026-08-03 "Installed"，但从未落盘进仓库。现状：karpathy-guidelines 两侧均无（其内容实质已内联为 `.agents/AGENTS.md` 四原则）；gstack、planning-with-files 仅存在于用户级运行时。对这两个名字的引用规则见 `PROMPT_DEFECTS.md` D-001 与 `SKILL_INVENTORY.md`。

## 单侧技能的解释与规则

`.claude` 独有的 13 项供应商技能面向 Claude Code 的技能加载格式；zcode 运行时不读 `.claude/`，因此看不到它们。仓库内无文件声明这种单侧性是有意的——本文件将其记录为**有意的单侧部署**（zcode 侧无对应运行时消费），并立规则：

1. 向任一侧新增项目技能时，同步复制到另一侧并保持字节一致（`diff -rq` 验证）；供应商技能（上游来自 Claude Code 插件生态）允许单侧存在，但必须在本文件登记。
2. 本 track 的模板（goal-template / loop-template / command-vocabulary）只引用两侧共有的技能，引用路径一律 `.agents/skills/<name>/SKILL.md`（zcode 可解析；Claude Code 侧经镜像同样可解析）。
3. 引用单侧技能（如 qt-*）的文档必须写明"仅 Claude Code 可用"。

## 复核命令

```bash
diff -rq .agents/skills/ .claude/skills/
git ls-files .agents/skills/ | cut -d/ -f3 | sort -u | wc -l
git ls-files .claude/skills/ | cut -d/ -f3 | sort -u | wc -l
```

预期：第一条约 13 行且全部为 `Only in .claude/skills/: …`；计数 37 / 50（供应商技能入库后数字同步变化）。
