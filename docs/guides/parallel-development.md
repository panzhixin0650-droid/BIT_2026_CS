# 模块并行开发指南

## 结论

当前仓库支持客户端、服务端、Web 和数据库从第一天并行开发。这里的“独立”是指每个模块都有稳定输入和开发期替代物，不需要等另一个模块先写完；它不代表各模块可以自行改变协议，最后再一次性拼接。

当前仓库仍是契约和目录骨架，尚未提交应用源码或构建文件。因此准确说法是“已经具备独立开始开发的条件”，不是“现在已经有四个程序可以分别编译”。各负责人开始实现时，应在自己的应用目录中建立独立构建入口。

团队只需要共同维护五类边界：

- `contracts/` 中的接口、DTO、状态和错误码；
- `contracts/examples/` 中可重复使用的请求、响应和 Dashboard fixture；
- `shared/protocol/` 中对当前契约的 C++ 实现；
- [当前数据库设计](../design/demo-database-design.md)以及 `database/migrations/` 中按编号演进的结构；
- `database/seeds/` 中跨模块共用的演示账号、编号和状态场景。

模块内部的页面类、Service 类、文件数量和实现方式不需要全员统一。

## 各模块为什么能独立开工

| 模块 | 第一天的输入 | 开发期替代物 | 第一次真实联调才需要 |
| --- | --- | --- | --- |
| `client/` | [V1 接口契约](../../contracts/overall-interface-v1.md) | `MockChargingApi` | `shared/protocol/`、`TcpChargingApi` 和运行中的服务端 |
| `server/` | V1 契约、[五表设计](../design/demo-database-design.md) | Repository 测试替身、`MockPile`、`MockPredictionProvider` | `shared/protocol/`、正式迁移、种子和客户端 TCP 联调 |
| `web/` | [`dashboard.sample.json`](../../contracts/examples/dashboard.sample.json) | 现有 30 日静态数据 | 服务端生成的 `dashboard.json` |
| `database/` | 五表字段、状态和事务约定 | 不需要 UI 或服务端代码 | Repository 查询和事务联调 |
| `shared/protocol/` | 长度帧、消息信封、DTO 和错误码 | 契约中的 JSON 示例 | 同时接入客户端和服务端构建 |

### 客户端

页面和 Controller 只能依赖 `IChargingApi`。开发期由 `MockChargingApi` 返回与真实 TCP API 相同的 DTO、错误码和完成语义，因此客户端不需要 SQLite，也不需要包含服务端头文件。

客户端负责人可以立即做页面骨架、登录、站点列表、订单流程和错误提示。切换联调时只替换 API 适配器，不应重写页面业务。

### 服务端

服务端可以先按契约完成 TCP 路由、ApplicationService 边界、管理员页面、Mock 桩和 Mock 预测。数据库迁移合并前，业务层可以使用服务端内部的 Repository 测试替身；这个替身属于服务端实现，不进入跨模块契约。

服务端不需要等待客户端 UI。每个消息处理器可以直接使用契约 fixture，或严格按契约在服务端测试中构造输入；需要跨模块复用的场景必须沉淀到 `contracts/examples/`，不能形成第二套事实源。

ApplicationService、管理员 UI 和 Mock 可以立即开发。真实 TCP 路由中的公共帧、DTO 和错误码要接入 `shared/protocol/`，不能由客户端和服务端分别实现两份再合并。

### Web

Web 是当前依赖最少的模块。它只读取 Dashboard JSON，不连接 TCP，也不直接打开 SQLite。现有 fixture 已包含汇总、桩状态、连续 30 日营收和预测样例，因此 Web 负责人不需要安装 Qt 就能完成页面。

### 数据库

数据库负责人可以直接根据五表设计编写 `001_initial_demo.sql` 和演示种子，不需要等待页面。服务端只通过 Repository 使用数据库；客户端和 Web 都不接触 SQL。

种子应保留契约已经使用的固定联调标识，例如 `13800000001`、`PILE-A-01` 和演示管理员账号，避免每个模块创建一套不同样例。

## 必须指定负责人之处

建议每个应用目录只有一名主要负责人，同时给共享边界指定维护者：

| 范围 | 建议维护方式 |
| --- | --- |
| `client/`、`server/`、`web/` | 各自由对应模块负责人维护 |
| `database/` | 数据库负责人维护；若只有两人，可由服务端负责人兼任 |
| `shared/protocol/` | 指定一名主维护者；客户端和服务端改动都需要对方复核 |
| `contracts/` | 不按“谁先改谁说了算”；受影响模块至少各有一人确认 |

不要建立长期的 `client`、`server` 或 `database` 总分支。目录负责人不是分支所有者；所有人仍从最新 `main` 创建短期功能分支并尽快通过 Pull Request 合并。

## 第一天要共同确认的最小清单

开始写代码前开一次短会，只确认下面这些事项：

1. 每个应用目录及数据库由谁主要负责；
2. 谁维护 `contracts/` 和 `shared/protocol/`；
3. 参考机按[开发环境基线](development-environment.md)使用 Ubuntu 22.04.3、Linux 6.8.0-138、Qt Framework 6.2.4、Qt Creator 6.0.2、GCC 11.4.0 和 C++17；个人环境可不同，但首个代码 PR 记录实际版本并在参考机复验；
4. 接口字段只以 V1 契约为准，不从某个模块的临时代码反推；
5. 当前契约和 fixture 中已出现的固定用户、桩和订单场景不由个人随意改名；
6. 第一次 TCP 联调前统一主机和端口；第一次 Web/Exporter 联调前统一 Dashboard 导出目录、静态服务根目录、相对 URL 和刷新方式。这些都不阻塞第一天开发。

当前登录和 Dashboard fixture 可以直接使用。站点、订单主流程以及典型失败响应的共享 fixture 仍应在对应功能大规模开发前补齐，避免客户端 Mock 与服务端各自猜字段。

## 建议的五个集成检查点

| 检查点 | 什么时候做 | 通过条件 |
| --- | --- | --- |
| G0 基线确认 | 各自开分支前 | 已分配目录和共享边界负责人，全员读过 V1 契约 |
| G1 数据形状确认 | 开发站点与订单功能前 | 核心成功、失败 fixture 可同时供客户端 Mock 和服务端测试使用 |
| G2 数据库接入 | 首个迁移完成后 | 满足数据库设计 §7 与 §8.2；活动订单唯一约束生效，至少一条写事务主路径通过 |
| G3 TCP 冒烟 | 客户端切换真实 API 时 | 依次打通 `system.ping`、`auth.user.login`、`station.list` |
| G4 Demo 验收 | 合并主流程前 | 完成 V1 契约 §11 的用户链路、管理端/大屏和必测失败分支 |

不要等所有页面完成后才做第一次联调。G3 只需要三个简单消息，足以提前发现长度帧、鉴权、`requestId`、`type` 和错误信封不一致。

## 第一轮可并行领取的任务

- 客户端：页面骨架、`IChargingApi`、`MockChargingApi`、登录与站点页面；
- 服务端：ApplicationService 边界、管理员页面、Mock 桩和 Mock 预测，Repository 暂接测试替身；
- Web：基于现有 Dashboard fixture 完成图表和 7/30 日切换；
- 数据库：五表迁移、索引、约束和演示种子；
- 共享协议：实现长度帧、信封、错误码和 DTO，并补站点、订单及失败 fixture。

如果团队只有两人，推荐拆成：

- A：`client/`，并参与 `shared/protocol/` 复核；
- B：`server/`、`database/` 和 Dashboard 导出；
- Web 先由任意一人依据静态 fixture 独立完成。

如果团队有四至五人，可以把 Web、数据库和共享协议分别交给独立负责人。

## 跨模块变更规则

只改模块内部细节时，在自己的短期分支中完成即可。出现下列任一情况时，必须在同一个 Pull Request 中更新共享事实源，并邀请受影响模块复核：

- 新增或修改网络消息、字段、类型、单位、枚举或错误码；
- 改变订单状态含义或计费规则；
- 改变 Dashboard JSON 的字段或日期语义；
- 改变表字段、约束或事务边界；
- Mock 的可观察结果与真实适配器不再一致。

具体 GitHub 操作见 [Ubuntu 与 GitHub 协作教程](github-collaboration.md)。
