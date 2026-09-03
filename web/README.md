# Web ECharts 大屏

Web 负责人可以只依赖 [`contracts/examples/dashboard.sample.json`](../contracts/examples/dashboard.sample.json) 开发，不需要等待 Qt 服务端，也不能直接打开 SQLite。

当前交付是静态 ECharts 页面。最终演示时读取服务端生成的 `dashboard.json`；7 日视图取 30 日数组的最后 7 项。真实 HTTP API、WebSocket 和前端构建框架都不是当前前置条件。

## 本地预览

页面入口是 [`index.html`](index.html)。由于浏览器不允许 `file://` 页面直接读取 JSON，请在仓库根目录启动一个静态文件服务：

```bash
python3 -m http.server 8000
```

然后访问 <http://127.0.0.1:8000/web/>。页面会优先读取 `web/dashboard.json`；文件尚未由服务端生成时，自动回退到契约样例 `contracts/examples/dashboard.sample.json`。

也可以通过查询参数指定快照位置：

```text
http://127.0.0.1:8000/web/?data=/path/to/dashboard.json
```

ECharts `6.1.0` 已固定在 `vendor/echarts.min.js`，页面不依赖外网即可展示图表；对应 Apache 2.0 许可证保存在 `vendor/echarts.LICENSE.txt`。

## 实时变化演示

大屏每 2 秒检查一次快照，只在 `generatedAt` 或核心数据变化时更新图表。打开两个终端，在仓库根目录分别运行：

```bash
# 终端 1：启动静态页面
python3 -m http.server 8000

# 终端 2：每 2 秒生成一份新快照
python3 web/simulate_dashboard.py
```

访问 <http://127.0.0.1:8000/web/> 后，可以看到营收、电桩状态、可用率和负荷预测持续变化。模拟器用临时文件加原子替换的方式写入 `web/dashboard.json`，页面不会读到半写入的 JSON；该运行时文件已由仓库根 `.gitignore` 忽略。

按 `Ctrl+C` 停止模拟器。自定义刷新间隔或限定生成次数：

```bash
python3 web/simulate_dashboard.py --interval 1 --steps 30
```

## 当前页面使用的 JSON

- `summary`：今日、本月、累计营收，充电站和充电桩总数；
- `pileStates`：闲置、在用、异常/离线数量及派生占比；
- `revenuePoints`：近 30 个中国业务日营收，页面可切换最后 7 日；
- `predictions[0]`：代表站点的负荷、可用电桩及拥堵等级预测；
- `generatedAt`、`schemaVersion`：快照时间和版本。

参考页面中的用户充电行为分级、用户画像、平台偏好、24 小时历史充电量、区域成本收益、站点效率排行、工作日/周末对比和用户舆情暂未实现，因为 V1 Dashboard JSON 没有相应字段。
