#include "ui/scan_page.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace charging::client {

ScanPage::ScanPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("scanPage"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 24, 20, 20);
    layout->setSpacing(14);

    auto *heading = new QLabel(QStringLiteral("扫一扫"), this);
    QFont headingFont = heading->font();
    headingFont.setPointSize(20);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    auto *description = new QLabel(
        QStringLiteral("扫描充电桩二维码后开始充电。开始前会先检查当前订单和电桩状态。"),
        this);
    description->setWordWrap(true);
    description->setStyleSheet(QStringLiteral("color: #667085;"));

    auto *scannerCard = new QFrame(this);
    scannerCard->setFrameShape(QFrame::StyledPanel);
    scannerCard->setStyleSheet(QStringLiteral(
        "QFrame { background: white; border: 1px solid #e4e7ec; border-radius: 12px; } "
        "QLabel { border: none; }"));
    auto *scannerLayout = new QVBoxLayout(scannerCard);
    scannerLayout->setContentsMargins(18, 18, 18, 18);
    scannerLayout->setSpacing(12);
    auto *cameraTitle = new QLabel(QStringLiteral("开发环境模拟扫码"), scannerCard);
    QFont cameraTitleFont = cameraTitle->font();
    cameraTitleFont.setBold(true);
    cameraTitle->setFont(cameraTitleFont);
    auto *cameraHint = new QLabel(
        QStringLiteral("Ubuntu 虚拟机可能没有摄像头直通。当前可输入二维码中的充电桩编号；"
                       "真实摄像头后续通过独立扫码适配器接入。"),
        scannerCard);
    cameraHint->setObjectName(QStringLiteral("scanAdapterHint"));
    cameraHint->setWordWrap(true);
    cameraHint->setStyleSheet(QStringLiteral("color: #667085;"));
    pileCodeInput_ = new QLineEdit(scannerCard);
    pileCodeInput_->setObjectName(QStringLiteral("scanPileCodeInput"));
    pileCodeInput_->setPlaceholderText(QStringLiteral("输入充电桩编号，如 PILE-A-01"));
    pileCodeInput_->setMaxLength(64);

    auto *quickRow = new QHBoxLayout();
    for (const QString &pileCode : {QStringLiteral("PILE-A-01"),
                                    QStringLiteral("PILE-B-02")}) {
        auto *button = new QPushButton(pileCode, scannerCard);
        button->setProperty("pileCode", pileCode);
        connect(button, &QPushButton::clicked, this, [this, pileCode]() {
            pileCodeInput_->setText(pileCode);
        });
        quickRow->addWidget(button);
    }

    startButton_ = new QPushButton(QStringLiteral("识别并开始充电"), scannerCard);
    startButton_->setObjectName(QStringLiteral("scanStartButton"));
    messageLabel_ = new QLabel(scannerCard);
    messageLabel_->setObjectName(QStringLiteral("scanMessage"));
    messageLabel_->setWordWrap(true);
    messageLabel_->hide();

    scannerLayout->addWidget(cameraTitle);
    scannerLayout->addWidget(cameraHint);
    scannerLayout->addWidget(pileCodeInput_);
    scannerLayout->addLayout(quickRow);
    scannerLayout->addWidget(startButton_);
    scannerLayout->addWidget(messageLabel_);

    layout->addWidget(heading);
    layout->addWidget(description);
    layout->addWidget(scannerCard);
    layout->addStretch();

    connect(startButton_, &QPushButton::clicked, this, [this]() {
        emit scanRequested(pileCodeInput_->text());
    });
    connect(pileCodeInput_, &QLineEdit::returnPressed, this, [this]() {
        emit scanRequested(pileCodeInput_->text());
    });
}

void ScanPage::preparePileCode(const QString &pileCode)
{
    pileCodeInput_->setText(pileCode.trimmed());
    pileCodeInput_->setFocus();
    showMessage(QStringLiteral("已填入预约充电桩，请到桩后确认开始充电"));
}

void ScanPage::setLoading(bool loading)
{
    pileCodeInput_->setDisabled(loading);
    startButton_->setDisabled(loading);
    if (loading) {
        showMessage(QStringLiteral("正在检查充电条件…"));
    }
}

void ScanPage::showMessage(const QString &message, bool error)
{
    messageLabel_->setText(message);
    messageLabel_->setStyleSheet(error ? QStringLiteral("color: #c62828;")
                                       : QStringLiteral("color: #1677ff;"));
    messageLabel_->setVisible(!message.isEmpty());
}

void ScanPage::reset()
{
    setLoading(false);
    pileCodeInput_->clear();
    messageLabel_->hide();
}

}  // namespace charging::client
