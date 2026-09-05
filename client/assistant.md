# 充电小助手：第一版使用与维护

底部“客服助理”现在是原生 Qt Widgets 聊天页，不依赖 WebEngine。
按 [ADR 0005](../docs/decisions/0005-client-rag-assistant.md) 实现只读问答，
不增加服务端客服工单或数据库表。

## 开始使用

1. 按 [`README.md`](README.md) 构建客户端。
2. 将 [`config.example.json`](config.example.json) 的内容保存为
   `client/config.local.json`，在本机填入中转站 Key。`config.local.*` 已被 Git 忽略。
   本次配置使用 `https://rightapi.ai/codex/v1` 和用户指定的 `gpt-5.6-luna`。
3. 从仓库根目录启动，或明确指定配置路径：

```bash
./build/client/user-client --assistant-config client/config.local.json
```

Qt Creator → Projects → Run → Command line arguments 可使用绝对配置路径，
避免不同工作目录找不到文件；参数中只放路径，不放 Key。Linux 建议配置权限 `600`，
文件属于运行客户端的普通用户；不要用 root 运行 GUI。

未指定路径时依次检查当前目录下 `client/config.local.json`、`config.local.json`，
再检查 `QStandardPaths::AppConfigLocation/assistant.json`。
配置只在启动时读取，修改后重启；读取失败或缺少 URL/Key/模型时不会阻止登录，
聊天页退到明确标注的本地模式。单元测试构造默认窗口时不读取机器上的真实 Key。

登录后进入“客服助理”：选择快捷问题即可发送，也可以输入文字，Enter 发送，
Shift+Enter 换行。界面可停止生成、失败后重试、复制回答、展开参考知识或新建对话。
切换页签保留当前对话；新建对话、退出登录及重新登录时清空，并取消在途请求。

## AI 与本地模式

- **AI + 知识库**：本地检索最多三条知识，再将问题、最近最多四组成功问答及
  这些片段发给配置的中转站。进入页面不会自动发请求或消耗额度，点击问题才调用。
  “按需连接”只表示配置完整，不表示已经通过网络验证。
- **仅本地知识库**：只显示检索片段，清楚标记“本地知识摘录 · 未调用 AI”；
  不请求中转站。没有相关知识时会说明不足，即使选择 AI 也不会无依据联网作答。
- 网络失败不伪装成成功、不自动调用其他供应商，不自动重试收费请求；可手动重试
  或切换本地模式。中断、超时或达到输出上限的回答标记为未完成，不加入后续历史。

问题限制 1200 字符；内存会话最多 24 次提问，达到后新建对话；只给模型发送最近
四组成功问答，每组问题最多 1200 字符、回答最多 4000 字符。总响应限制 1 MiB，
回答限制 32000 字符。`timeoutMs` 默认 45000，允许 1000–120000；
`maxOutputTokens` 默认 2048，允许 128–8192（包含模型推理可能使用的输出预算）。

## 知识库与 RAG

知识来源文件为 [`resources/assistant_knowledge.json`](resources/assistant_knowledge.json)，
随 Qt 资源包编译，不依赖启动目录。首版是 **关键词与中文二元词检索型 RAG**，
不是向量语义检索，也不是模型训练；不需要单独购买 embedding 或部署向量数据库。

目前整理了 14 个条目，覆盖找站、预约/取消、计费、充值、地图、故障、扫码、停止充电、
订单、登录、资料、网络、预测和助理能力。每条包含：

| 字段 | 维护约定 |
| --- | --- |
| `id` | 稳定知识 ID，用于来源标记 |
| `title` / `content` | 标题与依据当前实现核对的内容，避免把扩展规划写成已上线 |
| `keywords` | 相关术语、同义词，用于检索 |
| `question` | 非空时可作为预置问题；当前前六条展示为欢迎页卡片 |
| `source` | 可追溯的仓库文档或代码路径，不包含真实 Key 或私人数据 |

检索会对简短追问结合上次成功问题，但不保证理解复杂指代。修改知识文件后重新构建；
新增/修改功能时同步更新条目与检索测试。后续可以替换检索实现为 embedding/vector RAG，
聊天和外部模型适配不需要接入订单写操作。

来源展开面板表示“本次检索参考”，不等于回答已逐句校验。对金额、实时状态和结果，
仍以业务页面和服务端为准。助理不得声称已经报修、转人工、充值、退款或改订单。

## 隐私与部署边界

Key 存在本机配置、仅用于认证头；不写源码、知识库、日志、TCP 或 Git fixture。
程序不会自动上传手机号、登录 token、余额、位置或订单记录；只发送用户输入的聊天。
对手机号及形似 `sk-...` 的内容做尽力脱敏，但无法覆盖所有隐私，请勿粘贴个人信息。
从本地切到 AI 模式后，最近成功的本地问答也可能作为上下文发送。

使用 HTTPS、不忽略证书校验、不跟随重定向转发 Key。使用 `store: false`，但这不是
对第三方中转站日志保留策略的保证。配置文件是明文，拥有客户端机器的人可以读取它；
**仅用于当前私有开发验证，对外分发前应改为后端代理、独立限额并轮换测试 Key**。
聊天目前仅存在进程内存，不提供跨会话历史、附件、语音、联网搜索或工具执行。

## 验证与排错

```bash
cmake --build build/client
ctest --test-dir build/client --output-on-failure
```

普通测试使用独立假网络，不读取本机密钥，不消耗额度。仅在明确要联网验证时，
指定本地配置路径执行一次通用预约问题（会消耗中转站额度）：

```bash
CHARGING_ASSISTANT_LIVE_CONFIG=/absolute/path/config.local.json \
  ./build/client/charging_client_assistant_tests liveConfiguredEndpoint
```

配置不完整：核对启动目录/路径；鉴权失败：核对 Key 与模型权限；400/404：核对
Responses 地址和模型 ID；429：核对额度或频率；超时：检查网络并酌情调整超时。
不要把包含 Key 的配置或请求头粘贴到问题单。

本次边界及示例见[本地契约](../contracts/client-assistant-local.md)。接口依据：
[Right Code Responses 说明](https://docs.right.codes/docs/rc_extension/curl)、
[OpenAI 流式事件](https://developers.openai.com/api/docs/guides/streaming-responses)。

### 本次验证记录（2026-09-05）

- Qt 6.2.4 / GCC 11.4.0：客户端构建成功，11 个客户端及共享协议 CTest 目标通过。
- Qt 6.4.2 / GCC 13.3.0：构建成功，同样 11 个 CTest 目标通过。
- GUI 测试以非 root 用户、offscreen 平台运行，覆盖 360×640、420×760、
  快捷问题、复制/来源、输入法、取消/重试、退出登录和会话隔离。
- 合入 `main` 的底部导航图标改动后重新通过上述构建和测试；五个标签均保留图标，
  客服标签仍打开 `SupportPage`，输入区按实际导航栏顶部检查，不依赖固定栏高。
- 用户指定的中转站与 `gpt-5.6-luna` 已通过独立接口和真实聊天页验证，
  包括流式文字及多轮“预约 → 是否可以取消”的追问。仅使用通用项目问题。
- JSON 示例、知识库结构、本次文档的本地链接和 `git diff --check` 通过。
  真实 Key 未进入待提交文件，专用临时 GUI 验证配置已清理。
- 宿主为 Ubuntu 24.04；以上验证不替代最终 Ubuntu 22.04.3 完整参考机验收。
  本次不涉及地图改动，未重复运行启用 WebEngine 的腾讯地图联网测试。
