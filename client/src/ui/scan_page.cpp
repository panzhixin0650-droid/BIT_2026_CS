#include "ui/scan_page.h"
#include "ui/charging_art.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace charging::client {

ScanPage::ScanPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("scanPage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(20, 24, 20, 20);
    layout->setSpacing(14);
    auto *eyebrow = new QLabel(QStringLiteral("PLUG IN. CHARGE UP."), content);
    eyebrow->setProperty("role", "eyebrow");
    layout->addWidget(eyebrow);

    auto *heading = new QLabel(QStringLiteral("扫一扫"), this);
    heading->setObjectName(QStringLiteral("scanHeading"));
    QFont headingFont = heading->font();
    headingFont.setPointSize(24);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    auto *description = new QLabel(
        QStringLiteral("连接电桩，开启你的补能时光。"),
        this);
    description->setWordWrap(true);
    description->setStyleSheet(QStringLiteral("color: #697969;"));

    auto *scannerCard = new QFrame(this);
    scannerCard->setFrameShape(QFrame::StyledPanel);
    scannerCard->setProperty("role", "card");
    auto *scannerLayout = new QVBoxLayout(scannerCard);
    scannerLayout->setContentsMargins(18, 18, 18, 18);
    scannerLayout->setSpacing(12);
    auto *cameraTitle = new QLabel(QStringLiteral("输入充电桩编号"), scannerCard);
    QFont cameraTitleFont = cameraTitle->font();
    cameraTitleFont.setBold(true);
    cameraTitle->setFont(cameraTitleFont);
    auto *cameraHint = new QLabel(
        QStringLiteral("当前使用模拟扫码。输入桩身二维码中的编号，或选择下方示例。"),
        scannerCard);
    cameraHint->setObjectName(QStringLiteral("scanAdapterHint"));
    cameraHint->setWordWrap(true);
    cameraHint->setStyleSheet(QStringLiteral("color: #697969;"));
    pileCodeInput_ = new QLineEdit(scannerCard);
    pileCodeInput_->setObjectName(QStringLiteral("scanPileCodeInput"));
    pileCodeInput_->setPlaceholderText(QStringLiteral("输入充电桩编号，如 PILE-A-01"));
    pileCodeInput_->setMaxLength(64);
    pileCodeInput_->setAccessibleName(QStringLiteral("充电桩编号"));

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
    layout->addWidget(new ChargingArt(ChargingArt::Scene::Scan, content));
    layout->addWidget(scannerCard);
    auto *steps = new QLabel(QStringLiteral("01  连接充电枪    →    02  确认桩号    →    03  开始充电"), content);
    steps->setWordWrap(true);
    steps->setAlignment(Qt::AlignCenter);
    steps->setProperty("role", "eyebrow");
    layout->addWidget(steps);
    layout->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll);

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
                                       : QStringLiteral("color: #245c45;"));
    messageLabel_->setVisible(!message.isEmpty());
}

void ScanPage::reset()
{
    setLoading(false);
    pileCodeInput_->clear();
    messageLabel_->hide();
}

}  // namespace charging::client
