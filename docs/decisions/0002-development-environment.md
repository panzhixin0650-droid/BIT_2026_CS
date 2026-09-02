# ADR-0002：冻结课程 Demo 的参考开发环境

- 状态：Accepted
- 日期：2026-09-02

## 背景

项目说明书要求 Ubuntu 22.04 及以上，并写有“Qt Creator 6.2 及以上”。但 Qt Creator 是 IDE，Qt 是应用框架；Qt Creator 没有 6.2 正式版本。团队现已给出实际使用环境，需要把它作为唯一参考组合记录下来，避免每个模块自行选择不兼容的工具链。

## 决策

1. 当前参考开发机记录为 Ubuntu 22.04.3 LTS x86_64、Linux 6.8.0-138、Qt Framework 6.2.4、Qt Creator 6.0.2 和 GCC/G++ 11.4.0；其中内核号是环境快照，不是源代码兼容门槛；
2. 使用 C++17、CMake 3.22.x 和 Ninja 1.10.x；
3. Qt Creator 与 Qt Framework 是不同软件；Creator 6.0.2 的 Kit 必须指向系统 Qt 6.2.4；
4. C++ 模块统一使用 CMake，不同时维护 qmake 和 CMake 两套工程；
5. QSQLITE 是唯一当前数据库驱动；管理图表需要时安装 Qt Charts，真实内嵌地图需要时安装 Qt WebEngine；
6. 静态 ECharts Web 不要求 Node.js/npm，Mock 预测不要求 Python ML/CUDA；
7. 成员可临时使用不同编辑器或机器，但不得引入参考环境不支持的接口，合并和演示前必须回到参考环境验证；
8. 若使用 Windows 主机且老师未明确豁免说明书中的 VMware 17，最终版本至少在 VMware Workstation 17 中完成一次冒烟和演示验证。

## 结果

- 客户端、服务端、Web 和数据库成员可以只安装自己需要的工具；
- CMake 文件开始出现后，各模块必须能独立配置和构建；
- 扩展文档中的 Docker、外部数据库、真实模型、真实桩和额外安全库不成为 Demo 前置依赖；
- 以后有意提高 Qt、编译器或操作系统兼容门槛时，需要新的 ADR，并同步修改环境指南和构建配置；普通内核安全更新只更新环境记录。

具体安装、Kit 配置和自检命令见[开发环境基线与安装指南](../guides/development-environment.md)。
