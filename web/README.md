# Web ECharts 大屏

Web 负责人可以只依赖 [`contracts/examples/dashboard.sample.json`](../contracts/examples/dashboard.sample.json) 开发，不需要等待 Qt 服务端，也不能直接打开 SQLite。

当前交付是静态 ECharts 页面。最终演示时读取服务端生成的 `dashboard.json`；7 日视图取 30 日数组的最后 7 项。真实 HTTP API、WebSocket 和前端构建框架都不是当前前置条件。
