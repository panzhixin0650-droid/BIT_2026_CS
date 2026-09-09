#pragma once

#include <QWidget>
#include <QPointer>

class QDialog;
class QLabel;
class QLineEdit;
class QPushButton;

namespace charging::client {

// 扫码页声明：扫码或输入编号后交由外部发起充电
class ScanPage final : public QWidget {
    Q_OBJECT

public:
    explicit ScanPage(QWidget *parent = nullptr);

    // 提交与预填电桩编号的对外接口
    void submitPileCode(const QString &pileCode);
    void preparePileCode(const QString &pileCode);
    void prepareDirectPileCode(const QString &pileCode);
    void setLoading(bool loading);
    void showMessage(const QString &message, bool error = false);
    void reset();

// 上报取消、扫码结果与报修请求
signals:
    void cancelled();
    void scanRequested(const QString &pileCode);
    void repairRequested(const QString &pileCode);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // 扫码对话框的打开与关闭，配合页面显隐
    void openScanner(bool camera);
    void closeScanner();
    QPointer<QDialog> scannerDialog_;
    bool closingScanner_ = false;
    QPushButton *cameraButton_ = nullptr;
    QPushButton *imageButton_ = nullptr;
    QLineEdit *pileCodeInput_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QPushButton *repairButton_ = nullptr;
    QLabel *messageLabel_ = nullptr;
};

}  // namespace charging::client
