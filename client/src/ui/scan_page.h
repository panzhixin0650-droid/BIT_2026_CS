#pragma once

#include <QWidget>
#include <QPointer>

class QDialog;
class QLabel;
class QLineEdit;
class QPushButton;

namespace charging::client {

class ScanPage final : public QWidget {
    Q_OBJECT

public:
    explicit ScanPage(QWidget *parent = nullptr);

    void submitPileCode(const QString &pileCode);
    void preparePileCode(const QString &pileCode);
    void prepareDirectPileCode(const QString &pileCode);
    void setLoading(bool loading);
    void showMessage(const QString &message, bool error = false);
    void reset();

signals:
    void cancelled();
    void scanRequested(const QString &pileCode);
    void repairRequested(const QString &pileCode);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
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
