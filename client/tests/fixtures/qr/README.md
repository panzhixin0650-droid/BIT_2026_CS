# 二维码测试图片

`PILE-A-01.png` 和 `PILE-B-02.png` 的内容分别是文件名中的纯文本桩号。
可放入测试相册后从“扫一扫 → 从相册识别”选择，也可用手机显示或打印后交给摄像头扫描。
识别成功进入充电待启动页，由用户确认开始；是否能充电由服务端的桩状态和当前订单决定。

`invalid-url.png` 是失败用例，内容是 `https://example.invalid/PILE-A-01`；不得打开网址或开始充电。

图片由独立的 qrencode 4.1.1 生成，纠错等级 M、模块大小 8、白边 4：

```bash
qrencode -l M -s 8 -m 4 -o PILE-A-01.png PILE-A-01
qrencode -l M -s 8 -m 4 -o PILE-B-02.png PILE-B-02
qrencode -l M -s 8 -m 4 -o invalid-url.png https://example.invalid/PILE-A-01
```
