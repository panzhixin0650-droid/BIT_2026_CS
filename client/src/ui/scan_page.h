#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

namespace charging::client {

class ScanPage final : public QWidget {
    Q_OBJECT

public:
    explicit ScanPage(QWidget *parent = nullptr);

    void preparePileCode(const QString &pileCode);
    void setLoading(bool loading);
    void showMessage(const QString &message, bool error = false);
    void reset();

signals:
    void scanRequested(const QString &pileCode);

private:
    QLineEdit *pileCodeInput_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QLabel *messageLabel_ = nullptr;
};

}  // namespace charging::client
