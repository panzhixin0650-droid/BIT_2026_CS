# ADR-0011：真实离线底图与首页瓦片预加载

- 状态：Accepted（用户已授权实现，待合并）
- 日期：2026-09-08
- 授权：用户要求替换过于虚假的示意地图，先调用地图 API 获取默认地图，并进一步缩短腾讯首页加载。

## 决策与边界

开发阶段显式调用 OpenStreetMap Overpass API，获取覆盖沈阳演示站范围的道路、铁路、
水域、绿地与地名，打包为可审阅的离线资源。程序不在每次启动时调用该 API，Mock
仍然不联网、不依赖 Key 或 WebEngine。数据来源、日期、范围、请求和 SHA-256 随资源
记录，保留 © OpenStreetMap 署名及 ODbL 许可；不缓存或分发腾讯专有瓦片。
离线数据做显示简化及近似 GCJ-02 对齐，API/DTO 坐标不变；演示站、路线、预测及拥堵
信息仍来自现有适配器，不把真实底图当成真实设备、实时交通或可路由数据。

Qt 首页在登录期间预加载，离线几何/QImage 的首次准备允许使用一个有限任务后台线程，
只处理本地数据与图像，不创建 QWidget、不访问业务服务。线程结果通过 Qt 事件回传，
销毁窗口前回收；此决定不扩展业务运行时线程模型。

Tencent 首页在隐藏但已定尺寸的画布加载实际 SDK、实例和默认视野。首页与导航使用
同一个独立 WebEngine 磁盘 HTTP 缓存，遵循响应头，最大 64 MiB，不持久化 cookie。
预加载不调用 station.list、用户、订单、位置解析或路线接口；导航 SDK 预热排在首页
瓦片就绪之后。相同视野避免重复取景，首页自动取景不执行缓动动画。

加载中的首页可以显示明确标注来源的 OSM 离线预览。只有腾讯 `tilesloaded` 事件才能
移除预览；SDK 构造完成不等于瓦片完成。失败仍显示错误/重试，不切换 IMapService，
不把预览当成在线成功或成功 Mock 路线。该预览仅影响首页，不改变 ADR-0006 的路线
服务、恢复、计费及账号边界。没有实际腾讯 Key 的测试不能承诺真实网络首帧耗时。

## 参考

- [地图资源、许可及更新脚本](../../client/resources/map/NOTICE.md)
- [腾讯 Map 事件和 FitBoundsOptions](https://lbs.qq.com/webApi/javascriptGL/glDoc/docIndexMap)
- [Qt 6 缓存行为变化](https://doc.qt.io/qt-6/qtwebengine-changes-qt6.html#default-profile)
