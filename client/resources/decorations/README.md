# 手绘装饰素材

素材来源为用户提供的绿色手绘图案附件。原始图片保持不动；作者文字和背景散点不作为装饰。
透明 PNG 必须保留图案内部的白色填充、高光及原有颜色，不使用全局白色透明替换。

素材由用户授权的本地 Pillow 脚本 `extract.py` 从原图像素提取，未使用生成式重绘结果。
脚本按明确裁切框选择图案，以边界连通区域识别外部白色背景，保留封闭轮廓内的白色；
移除不相连的散点，并仅在外轮廓窄边缘消除白色背景混色。最终每边留 8 px 透明边距。
原附件尺寸为 1320×1632，不缩放或覆盖原图。复现命令：

```bash
python3 client/resources/decorations/extract.py /path/to/original.png client/resources/decorations
```

`cutouts-preview.png` 是深绿背景验收图，用于检查内部白色与边缘，不作为页面资源。

资源名称：`clover`、`avocado`、`bow`、`leaves`、`rabbit`、`drink`、`frog`、`plant`。
本次页面仅使用三种：登录标题旁的四叶草、助理欢迎卡的兔子、“我的”标题旁的盆栽。
其余作为独立素材保留，不自动铺到其他页面。

`DecorativeHeading` 给文字与装饰分配独立布局空间。装饰最大为 104 / 92 / 66 px，
随容器宽度缩小，容器小于 260 px 时隐藏；不接收鼠标事件，不进入键盘焦点顺序，
可访问名称和描述均为空。图片不替代业务图标、头像或操作按钮。

验证与截图：

```bash
cmake --build build/client --target charging_client_decoration_tests
QT_QPA_PLATFORM=offscreen BIT_DECOR_SCREENSHOTS=build/client/decor-screenshots \
  ./build/client/charging_client_decoration_tests
```

测试检查透明通道、外围透明边距、兔子和青蛙的白色填充，以及 360×640、480×860、
1100×800 窗口中的文字与装饰无重叠、无横向滚动。输出为实际 Qt 页面截图，使用
隔离的 Mock 业务数据，不读取本机地图或 AI Key。Qt 窄屏截图用于响应式布局验收，
不代表本项目已构建原生 Android / iOS 应用。

助理首次进入及新建对话时从欢迎卡顶部显示；有消息后继续使用原有的跟随回复滚动。
