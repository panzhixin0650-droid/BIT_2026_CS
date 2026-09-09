#pragma once

#include <QWidget>
#include <QString>
#include <QImage>

class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace charging::client {

// Local demo album. Selecting a picture never starts a business order.
// 演示相册页：仅本地选图，不触发业务请求
class PhotoAlbumPage final : public QWidget {
    Q_OBJECT
public:
    enum class Purpose { QrCode, Avatar };
    // 可指定目录与用途，头像或二维码
    explicit PhotoAlbumPage(QWidget *parent = nullptr, const QString &directory = {},
                            Purpose purpose = Purpose::QrCode);
    // 重新扫描目录刷新缩略图
    void reload();
// 选中结果以路径或图像对象向外发出
signals:
    void imageSelected(const QString &path);
    void imageSelectedImage(const QImage &image);
    void backRequested();
protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    // 网格排布、选择状态与预览的内部逻辑
    void layoutGrid();
    void updateSelection();
    void showPreview();
    void updatePreview();
    QString directory_;
    Purpose purpose_;
    QStackedWidget *pages_;
    QListWidget *photos_;
    // 数量、选择状态、空提示与预览等控件
    QLabel *count_, *selection_, *empty_, *preview_;
    QPushButton *confirm_, *previewButton_, *back_;
};

} // namespace charging::client
