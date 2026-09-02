# Ubuntu 与 GitHub 协作教程

这份教程面向第一次参加多人 GitHub 开发的成员。照着“每次开发的固定流程”操作即可；遇到异常时先执行 `git status`，不要用强制命令碰运气。

本项目采用同仓库协作：所有成员由仓库所有者邀请为 Collaborator，各自在本机克隆仓库，从最新 `main` 创建短期分支，通过 Pull Request（PR）评审后合并。

## 1. 先认识五个概念

| 名词 | 含义 |
| --- | --- |
| GitHub 账号 | 网站账号，决定你能访问哪些远端仓库 |
| Git 提交身份 | 本机的 `user.name` 和 `user.email`，写入每个 commit 的作者信息 |
| 认证 | SSH key 或 GitHub CLI 凭据，用来证明执行 pull/push 的账号 |
| 本地仓库 | 自己电脑上的项目副本，修改和 commit 先发生在这里 |
| `origin` | 本地对 GitHub 远端仓库的默认简称 |

配置 `user.name` 不等于登录 GitHub；能克隆公共仓库也不等于有推送权限；`commit` 也不等于已经上传，只有 `push` 后队友才看得到。

## 2. 仓库所有者先配置一次

### 2.1 邀请每个成员

不要多人共用同一个 GitHub 账号。先收集每个人自己的 GitHub 用户名，在仓库页面依次进入：

```text
Settings → Collaborators → Add people
```

每位成员必须接受邮件或 GitHub 通知中的邀请，之后才有推送权限。GitHub 对个人账号仓库的官方邀请步骤见 [Inviting collaborators](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/repository-access-and-collaboration/inviting-collaborators-to-a-personal-repository)。

### 2.2 保护 `main`

本仓库是公开仓库，可以在 `Settings → Rules → Rulesets` 或 `Settings → Branches` 中为 `main` 建立保护规则，建议启用：

- 合并前必须有 Pull Request；
- 至少一名其他成员批准；
- 合并前解决全部 review 对话；
- 如果界面提供 bypass 设置，不让仓库所有者把日常开发作为绕过理由；
- 禁止 force push；
- 禁止删除 `main`。

当前还没有 CI 检查可要求，启用 status checks 没有实际收益；等 CI 建立并稳定运行后再启用。具体设置见 [Managing a branch protection rule](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/managing-a-branch-protection-rule)，规则含义和套餐范围见 [Available rules for rulesets](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-rulesets/available-rules-for-rulesets)。

在 `Settings → General → Pull Requests` 中建议只保留 `Allow merge commits`，并开启合并后自动删除远端分支。初学阶段统一使用 GitHub 页面上的普通 `Merge pull request`，不要在同一团队中随意混用 rebase、squash 和强推。

## 3. 每位成员在 Ubuntu 上安装工具

以下命令适用于项目要求的 Ubuntu 22.04 或更新版本：

```bash
sudo apt update
sudo apt install -y git openssh-client ca-certificates

git --version
ssh -V
```

Git 官方推荐 Debian/Ubuntu 使用包管理器安装，见 [Installing Git](https://git-scm.com/install/linux)。本教程的默认认证方式是 SSH，因此 `openssh-client` 是必需工具。

### 可选：安装 GitHub CLI

`gh` 可以在终端中登录 GitHub、创建 PR 和查看 PR 状态，但不是本项目开发的前置条件。下面是 GitHub CLI 官方维护的 Debian/Ubuntu 安装方式（核对日期：2026-09-02）：

```bash
(type -p wget >/dev/null || (sudo apt update && sudo apt install wget -y)) \
  && sudo mkdir -p -m 755 /etc/apt/keyrings \
  && out=$(mktemp) \
  && wget -nv -O "$out" https://cli.github.com/packages/githubcli-archive-keyring.gpg \
  && sudo tee /etc/apt/keyrings/githubcli-archive-keyring.gpg < "$out" > /dev/null \
  && sudo chmod go+r /etc/apt/keyrings/githubcli-archive-keyring.gpg \
  && sudo mkdir -p -m 755 /etc/apt/sources.list.d \
  && echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
     | sudo tee /etc/apt/sources.list.d/github-cli.list > /dev/null \
  && sudo apt update \
  && sudo apt install gh -y
```

以后执行时先与 [GitHub CLI 官方 Linux 安装页](https://github.com/cli/cli/blob/trunk/docs/install_linux.md) 对照，因为软件源安装命令可能更新。不要使用 Snap，也不要只依赖可能过旧的 Ubuntu/Debian 社区包。

安装完成后检查：

```bash
gh --version
```

本项目目前不需要 Git LFS。以后只有在团队决定提交大型模型、视频或数据集时，才通过单独 PR 启用；不要由个人先执行 `git lfs track`。

## 4. 创建并保护自己的 GitHub 账号

每个人应完成以下事项：

1. 创建自己的 GitHub 账号；
2. 验证邮箱；
3. 启用双因素认证（2FA）；
4. 安全保存 2FA 恢复码；
5. 不向队友发送密码、Token、SSH 私钥或恢复码。

GitHub 推荐使用 TOTP 等方式启用 2FA，并要求恢复码保存在安全位置，见 [Configuring two-factor authentication](https://docs.github.com/en/authentication/securing-your-account-with-two-factor-authentication-2fa/configuring-two-factor-authentication)。

## 5. 配置 Git 提交身份

在自己的 Ubuntu 终端执行一次：

```bash
git config --global user.name "你的姓名或常用昵称"
git config --global user.email "GitHub 中已验证的邮箱"
git config --global init.defaultBranch main
```

检查结果：

```bash
git config --global --get user.name
git config --global --get user.email
```

`user.name` 不要求等于 GitHub 用户名。提交邮箱应当是 GitHub 账号中已验证的邮箱，这样提交才能正确关联到个人贡献。官方说明见 [Setting your username in Git](https://docs.github.com/en/get-started/git-basics/setting-your-username-in-git) 和 [Setting your commit email address](https://docs.github.com/en/account-and-profile/how-tos/email-preferences/setting-your-commit-email-address)。

如果不想公开真实邮箱，在 GitHub 的 `Settings → Emails` 中开启邮箱隐私，然后复制页面实际显示的 `noreply` 邮箱并配置到 Git。不要自己猜地址格式。

克隆本项目后，第 7 节还会配置三个只对本仓库生效的安全偏好，不改变成员电脑上的其他 Git 项目。

## 6. 配置推送认证：推荐 SSH

提交身份和推送认证是两件事。下面为每台用于开发的电脑单独配置 SSH key。

### 6.1 先检查是否已有 key

```bash
ls -al ~/.ssh
```

如果已经存在 `id_ed25519` 和 `id_ed25519.pub`，不要覆盖。确认它是不是你自己的 key；不确定时请找熟悉 Git 的成员协助。

如果没有，生成一对新 key：

```bash
ssh-keygen -t ed25519 -C "你的 GitHub 已验证邮箱或 noreply 邮箱"
```

提示保存位置时按 Enter 使用默认路径，提示 passphrase 时建议设置一个自己能保存好的口令。GitHub 官方步骤见 [Generating a new SSH key](https://docs.github.com/en/authentication/connecting-to-github-with-ssh/generating-a-new-ssh-key-and-adding-it-to-the-ssh-agent?platform=linux)。

### 6.2 加载 key

```bash
eval "$(ssh-agent -s)"
ssh-add ~/.ssh/id_ed25519
```

重新登录系统后如果 push 报 key 未加载，可以再次执行这两条命令。

### 6.3 只上传公钥

显示公钥：

```bash
cat ~/.ssh/id_ed25519.pub
```

复制整行内容，在 GitHub 中进入：

```text
个人头像 → Settings → SSH and GPG keys → New SSH key
```

类型选 `Authentication Key`，粘贴后保存。只允许复制以 `.pub` 结尾的公钥；绝不能展示、上传或发送 `~/.ssh/id_ed25519` 私钥。官方步骤见 [Adding a new SSH key](https://docs.github.com/en/authentication/connecting-to-github-with-ssh/adding-a-new-ssh-key-to-your-github-account)。

### 6.4 测试连接

```bash
ssh -T git@github.com
```

第一次连接会要求确认 GitHub 主机指纹。先与 [GitHub 官方 SSH fingerprints](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/githubs-ssh-key-fingerprints) 核对，匹配后再输入 `yes`。成功信息应包含你自己的 GitHub 用户名；GitHub 不提供 shell，所以这条测试成功时仍可能返回退出码 1，这是正常情况。

如果安装了可选的 `gh`，SSH 能 push 也不代表 `gh` 已获得 GitHub API 权限。还要登录一次，之后才能用它创建 PR：

```bash
gh auth login --hostname github.com --git-protocol ssh --web
gh auth status
```

### 6.5 如果必须使用 HTTPS

网络阻止 SSH 时，可以安装 `gh` 后使用浏览器授权：

```bash
gh auth login --hostname github.com --git-protocol https --web
gh auth setup-git
gh auth status
```

必须在浏览器中登录自己的账号。GitHub 账号密码不能再作为命令行 `git push` 的密码；也不要把 Personal Access Token 写进远端 URL、代码、截图或群聊。HTTPS 凭据的官方说明见 [Caching your GitHub credentials](https://docs.github.com/en/get-started/git-basics/caching-your-github-credentials-in-git)。

一个本地仓库只选 SSH 或 HTTPS 其中一种，不需要交替使用。

## 7. 克隆本项目

成员接受仓库邀请后，根据第 6 节已经配置的认证方式二选一，不要两条都执行。

SSH：

```bash
mkdir -p ~/projects
cd ~/projects
git clone git@github.com:panzhixin0650-droid/BIT_2026_CS.git
```

HTTPS 与 `gh`：

```bash
mkdir -p ~/projects
cd ~/projects
git clone https://github.com/panzhixin0650-droid/BIT_2026_CS.git
```

然后进入项目并检查：

```bash
cd BIT_2026_CS

git remote -v
git branch --show-current
git status
```

预期当前分支是 `main`，工作区没有修改，远端名为 `origin`。克隆会获得仓库完整历史，官方说明见 [Cloning a repository](https://docs.github.com/en/repositories/creating-and-managing-repositories/cloning-a-repository)。

如果已经通过 HTTPS 克隆，希望改为 SSH：

```bash
git remote set-url origin git@github.com:panzhixin0650-droid/BIT_2026_CS.git
git remote -v
```

为本项目设置安全偏好：

```bash
git config fetch.prune true
git config pull.ff only
git config core.autocrlf input
```

`pull.ff only` 会在本地与远端已经分叉时安全停止，而不是自动制造一次不清楚的合并；此时按照后文的同步或排错步骤处理。

每位成员使用自己电脑上的独立克隆，不要多人共用一个工作目录，也不要使用 `sudo git clone`、`sudo git commit` 或 `sudo git push`。

## 8. 每次开发都照着做

完整循环只有一条：

```text
更新 main → 新建短期分支 → 修改并检查 → commit → push 分支 → PR → 他人 Review → 合并 → 清理分支
```

### 8.1 从最新 `main` 建分支

先检查当前分支和工作区：

```bash
git status --short
git branch --show-current
```

只有 `git status --short` 没有任何输出时，才继续：

```bash
git switch main
git pull --ff-only origin main
git switch -c client/login-page
git branch --show-current
```

如果 `git status --short` 有输出，先弄清楚这些修改属于什么任务，不要切换分支、pull 或删除。

分支名只用小写英文、数字、斜杠和连字符。项目约定：

| 前缀 | 用途 | 示例 |
| --- | --- | --- |
| `client/` | Qt 用户端 | `client/login-page` |
| `server/` | 服务端或管理员端 | `server/order-stop` |
| `web/` | ECharts 页面 | `web/revenue-chart` |
| `db/` | 迁移或种子 | `db/initial-demo` |
| `contract/` | 接口、DTO、文档或跨模块规则 | `contract/order-error` |

一项小功能对应一个短期分支和一个 PR。不要建立长期 `client-dev`、`server-dev` 分支，也不要在同一分支混入不相关任务。

### 8.2 开发中检查改动

随时使用：

```bash
git status --short
git diff
```

只暂存这次确实要提交的文件：

```bash
git add client/具体文件
git diff --cached
git diff --cached --check
```

不要把 `git add .` 当默认操作。先看 `git diff --cached`，能及时发现误加入的密钥、运行时数据库、构建产物或其他人的文件。

### 8.3 提交并推送

```bash
git commit -m "feat(client): add login page"
git status
git push -u origin client/login-page
```

第一次 push 使用 `-u` 建立跟踪关系，之后在同一分支只需：

```bash
git push
```

推荐提交格式是 `<类型>(<范围>): <简短动作>`：

```text
feat(client): add login page
fix(server): reject invalid order state
docs(contract): clarify stop response
chore(db): add initial migration notes
```

一次 commit 只表达一个可解释的变化。不要提交 `.env`、Token、私钥、`build/`、Qt Creator 用户配置、运行时 `.db` 或自动生成的 `dashboard.json`。仓库的 `.gitignore` 只能挡住部分已知文件，绝不能依赖它发现粘贴在源码或文档里的 Token、密码和私钥。

## 9. 创建和评审 Pull Request

push 后打开 GitHub 仓库页面，点击 `Compare & pull request`，确认：

```text
base: main
compare: 你的短期分支
```

按现有 PR 模板写明：

- 做了什么以及属于哪个模块；
- 是否改变接口、fixture 或数据库；
- 如何验证；
- 哪些检查没有执行及原因；
- 希望哪位成员 Review。

功能尚未完成但希望队友提前看到时，可以先开 Draft PR。安装 `gh` 后也可执行：

```bash
gh pr create --web --base main --head client/login-page
```

Reviewer 在 `Files changed` 中查看改动，可以选择普通评论、`Approve` 或 `Request changes`。作者修改后继续在原分支 commit 并 push，原 PR 会自动更新，不要再建第二个 PR。GitHub 的标准流程见 [Pull request quickstart](https://docs.github.com/en/pull-requests/get-started/pull-request-quickstart) 和 [Giving reviews](https://docs.github.com/en/pull-requests/concepts/giving-reviews)。

项目评审规则：

- 作者不能用自己的批准代替他人 Review；
- 改 `contracts/` 时，客户端和服务端负责人都要确认影响；
- 改 `database/` 时，服务端负责人必须确认；
- 改 Dashboard JSON 时，服务端和 Web 负责人都要确认；
- 不直接执行 `git push origin main`。

## 10. 在功能分支同步最新 `main`

PR 开发期间其他成员可能已经合并代码。先确认自己仍在功能分支且工作区干净，然后：

```bash
git status --short
git branch --show-current
```

必须确认 `git status --short` 没有输出，而且 `git branch --show-current` 是本次功能分支、不是 `main`，然后才运行：

```bash
git fetch origin
git merge --no-edit origin/main
```

没有冲突就运行：

```bash
git push
```

这个新手流程不会重写已经发布的历史。不要在功能分支执行含义不清楚的裸 `git pull`，也不要自行使用 `git rebase`、`git push --force` 或 `git push --force-with-lease`。Git 对 pull/merge 行为的说明见 [git pull](https://git-scm.com/docs/git-pull) 和 [git merge](https://git-scm.com/docs/git-merge)。

## 11. 安全解决冲突

冲突不是代码丢失，而是 Git 无法替团队决定哪个结果正确。首先执行：

```bash
git status
```

打开列出的文件，查找 Git 插入的三种冲突标记：七个小于号 `<<<<<<<`、七个等号 `=======` 和七个大于号 `>>>>>>>`。

与相关负责人确认最终内容，删除标记，再逐个暂存：

```bash
git diff --check
git add 路径/冲突文件
git status
git commit -m "merge: sync main into client/login-page"
git push
```

如果无法判断，撤销本次尚未完成的 merge：

```bash
git merge --abort
```

然后请对应目录负责人一起处理。`contracts/`、数据库设计、`shared/protocol/` 以及 `.doc`、PNG 等二进制文件发生冲突时尤其不能随便选择一边。GitHub 的冲突标记和处理步骤见 [Resolving a merge conflict using the command line](https://docs.github.com/en/pull-requests/how-tos/merge-and-close-pull-requests/resolving-a-merge-conflict-using-the-command-line)。

下面这些命令不能当成新手“解决冲突”的办法：

```text
git reset --hard
git push --force
git checkout --ours .
git checkout --theirs .
删除 .git/
复制整个旧目录覆盖当前仓库
```

## 12. PR 合并后的清理

确认 GitHub 页面显示 PR 已合并后，先检查工作区：

```bash
git status --short
```

只有没有输出时才继续：

```bash
git switch main
git pull --ff-only origin main
git fetch --prune
git branch -d client/login-page
```

远端功能分支可以由 GitHub 自动删除，或在 PR 页面点击 `Delete branch`。删除已合并分支不会删除已经进入 `main` 的代码和 PR 讨论记录。

如果团队没有按约定使用普通 merge，而是使用了 squash 或 rebase，`git branch -d` 可能提示分支尚未完全合并。这是安全保护；不要立即改用 `-D`，应先确认 PR、未推送提交和合并方式，再请负责人处理。以后若团队正式更换合并方式，必须同时更新本教程。

下一项任务必须重新从最新 `main` 创建分支，不继续复用已经合并的分支。

## 13. 常见错误速查

| 现象 | 常见原因 | 安全处理 |
| --- | --- | --- |
| `Author identity unknown` | 没配置提交姓名或邮箱 | 重新执行第 5 节的两条身份配置 |
| `Permission denied (publickey)` | key 未加载、未上传或登录了错误账号 | 启动 `ssh-agent`，添加 key，再运行 `ssh -T git@github.com` |
| `Repository not found` | URL 错、未接受邀请或当前账号无权限 | 从仓库页面复制 URL，检查邀请和 SSH 返回的用户名 |
| `non-fast-forward` | 被推送的远端分支比本地新 | 保存完整报错并检查当前分支；请负责人判断应合入 `origin/main` 还是同名远端分支，不要 force push |
| `protected branch update failed` | 正在直接推 `main` | 从 `main` 创建短期分支并走 PR |
| `src refspec ... does not match any` | 分支无 commit 或分支名写错 | 检查 `git status` 和 `git branch --show-current` |
| 切分支时提示修改会被覆盖 | 当前有未提交工作 | 留在当前分支检查并提交；不要加 `-f` |
| Git 反复要求密码 | HTTPS 认证未正确配置 | 改用 SSH，或用 `gh auth login`；不要输入账号密码 |

### 不小心在 `main` 上开始修改

如果还没有 commit，直接从当前位置创建正确分支，未提交修改会随工作区保留：

```bash
git switch -c client/正确的分支名
```

如果已经在本地 `main` commit 但尚未 push，不要 push，也不要 reset；先从当前位置创建正确分支，然后联系负责人检查本地 `main` 的恢复方式。

任何异常都先保存 `git status` 和完整报错。不要删除不认识的文件，不要用 `sudo` 或强制参数掩盖权限、分支或冲突问题。

## 14. 日常命令卡

新任务：

```bash
git status --short
git branch --show-current
```

确认没有输出后：

```bash
git switch main
git pull --ff-only origin main
git switch -c client/功能名
```

提交：

以下路径和分支名都是示例，必须替换成自己本次任务的真实值。

```bash
git status --short
git diff
git add 具体文件
git diff --cached
git diff --cached --check
git commit -m "feat(client): describe change"
git push -u origin client/功能名
```

合并 `main` 到自己的功能分支：

```bash
git status --short
git branch --show-current
```

确认工作区为空、当前是本次功能分支且不是 `main` 后：

```bash
git fetch origin
git merge --no-edit origin/main
git push
```

PR 合并后：

```bash
git status --short
```

确认没有输出后：

```bash
git switch main
git pull --ff-only origin main
git fetch --prune
git branch -d client/功能名
```

## 15. 本项目的最小纪律

1. 不直接改或推送 `main`；
2. 一项功能一个短期分支和一个 PR；
3. push 前检查 `git diff --cached`；
4. 至少一名其他成员 Review；
5. 跨模块字段先改契约和 fixture；
6. 不提交密钥、运行数据和构建产物；
7. 不用 force、hard reset 或整目录覆盖解决问题；
8. 合并后删除功能分支，下一项任务重新从 `main` 开始。

更精简的项目约定见仓库根目录的 [CONTRIBUTING.md](../../CONTRIBUTING.md)，模块如何并行见 [模块并行开发指南](parallel-development.md)。
