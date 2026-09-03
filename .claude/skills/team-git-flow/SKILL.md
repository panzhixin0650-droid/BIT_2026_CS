---
name: team-git-flow
description: 接管 BIT_2026_CS 的本地 Git 与 GitHub 协作流程。用于同步 main、从现有改动建立任务分支、检查并提交、创建或更新 PR、邀请相关负责人审核、处理审核意见，以及获准后的合并清理。
---

# Agent Git 协作工作流

为五人团队自动完成日常 Git/GitHub 操作。Git 和 SSH 的人工配置见 `docs/guides/github-collaboration.md`，不要在这里重复教程。

## 解释用户授权

- “检查”“看看改动”：只读，不 commit、不 push、不修改 PR。
- “本地提交”：允许检查、测试和 commit，不允许远程写入。
- “提交/创建 PR”：允许为当前任务建分支、commit、push、创建或更新 PR、邀请审核。
- “更新 PR”：允许把当前分支的新改动测试、commit、push 到原 PR，并更新说明。
- “处理审核意见”：允许处理当前 PR 内明确且不扩展范围的意见，然后测试、commit、push 和回复。
- “合并”：仅在审核与检查满足条件后允许合入 `main`。

不得把“提交 PR”推断为“合并 PR”，也不得把一次授权沿用到无关任务。

## 1. 预检

1. 找到仓库根目录，读取 `AGENTS.md`、`PROJECT_RULES.md`、`CONTRIBUTING.md` 和 `.github/pull_request_template.md`。
2. 检查 `git status --short --branch`、当前分支、远端 URL 和提交历史。
3. 根据 URL 和权限识别团队主仓库与可写远端：同仓库协作通常都为 `origin`；Fork 协作通常以 `upstream` 为团队仓库、`origin` 为个人 Fork。
4. 需要 GitHub 写操作时，先验证 `gh auth status`；若环境提供等价 GitHub 连接器，也可使用它。
5. 保留无关改动。若现有修改混合多个任务、来源不明或包含可疑文件，停止并说明。

## 2. 接管本地修改

- 工作区干净且开始新任务：先 fetch/prune，用 `--ff-only` 更新本地 `main`，再建立短期任务分支。
- 已在 `main` 上产生未提交修改：先从当前位置建立任务分支保护修改，再检查、提交并同步团队 `main`；禁止直接提交或推送 `main`。
- 已在任务分支：继续使用该分支；先检查它是否已有 PR，有则更新原 PR。
- 分支名按主要改动使用 `client/`、`server/`、`web/`、`db/` 或 `contract/` 前缀；一个任务只用一个短期分支和一个 PR。

不要自动 stash、重写历史或把无关修改搬入当前分支。

## 3. 检查并提交

1. 从实际 diff 判断变更目的、主要模块和跨模块影响，不只依赖用户描述。
2. 按仓库规则补齐必要的契约、fixture、迁移或文档；不要顺手修改其他成员模块。
3. 运行受影响模块已有的最小构建/测试，并执行 `git diff --check`。无法执行的检查必须记录原因和风险。
4. 只暂存本任务的明确文件，检查 staged diff；禁止默认 `git add .`。
5. 使用仓库格式创建能说明结果的提交，例如 `feat(client): add login page`。一个提交只表达一个逻辑变化。
6. 发布前 fetch/prune，将团队 `main` merge 到任务分支并重新测试；不对已发布分支 rebase，不强推。

## 4. 创建或更新 PR

1. push 前再次核对目标仓库、当前分支和 staged/unstaged 状态，绝不 push `main`。
2. 先查询当前分支是否已有开放 PR；有则继续更新，没有才创建。
3. PR 以团队 `main` 为 base，以当前任务分支为 head。
4. 按现有 PR 模板根据真实 diff 写明：变更内容、跨模块影响、验证结果、未执行检查及原因。
5. push 后核对远端 HEAD 与本地提交一致，并返回 PR URL、提交摘要和测试结果。

## 5. 邀请审核

优先使用仓库 `CODEOWNERS` 或已确认的团队账号映射，不根据姓名猜 GitHub 用户名。没有可靠账号时，只询问缺少的 reviewer，然后继续流程。

按改动路径邀请：

- `client/`、`server/`、`web/` 的模块内改动：至少一名非作者成员。
- `database/`：数据库负责人和服务端负责人。
- `shared/protocol/` 或 `contracts/`：共享协议、客户端和服务端负责人。
- Dashboard JSON 语义：Web 和服务端负责人。

作者不能批准自己的 PR。新增提交后继续使用同一 PR，并在需要时重新请求审核。

## 6. 处理 Review 与合并

- 读取未解决评论、Review 状态和 CI。明确且属于原任务的问题可直接修复、测试、commit、push、回复。
- 涉及需求变化、架构取舍、跨模块语义或无法验证的建议时，停止并交给用户决定。
- 只有用户明确要求合并，并且至少一名其他成员已批准、必需检查通过、无冲突、PR 非 Draft、待合并 HEAD 未变化时，才使用仓库约定的普通 merge。
- 若受保护 `main` 已强制 Review/检查，可在用户允许合并后启用 auto-merge；禁止管理员绕过规则。
- 合并确认后 fetch/prune，`--ff-only` 更新本地 `main`，再安全删除已合并的任务分支。

## 停止条件

遇到冲突含义不明、`main` 分叉、测试失败、认证/权限失败、远端被意外更新，或发现密钥、构建产物、本地数据库和无关文件时停止并报告现场。

禁止 `git reset --hard`、force push、自动丢弃修改、批量选择冲突一方、跳过 hooks、删除 `.git/` 或使用 `sudo git`。
