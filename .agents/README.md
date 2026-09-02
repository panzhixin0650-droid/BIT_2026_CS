# Codex 项目 Skill

[`skills/bit-charging-dev/SKILL.md`](skills/bit-charging-dev/SKILL.md) 是 Codex 可自动发现的仓库级 skill。跨工具的权威规则位于根目录 [`PROJECT_RULES.md`](../PROJECT_RULES.md)，Claude Code 使用同内容的 `.claude/skills/bit-charging-dev/SKILL.md` 和根 `CLAUDE.md`。

仓库级副本会随 Git 克隆传播；需要跨仓库使用时再把它安装到个人 skill 目录。以后修改 skill 时必须同步 `.agents`、`.claude` 和需要保留的个人安装副本；通用的长期硬规则优先只改 `PROJECT_RULES.md`，避免多份 skill 漂移。
