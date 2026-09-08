#include "ui/scan_page.h"
#ifdef CHARGING_CLIENT_HAS_SCANNER
#include "ui/qr_scan_dialog.h"
#endif
#include <QDialog>
#include <QHideEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTimer>
#include <QRegularExpression>
namespace charging::client {
ScanPage::ScanPage(QWidget *parent):QWidget(parent){
 setObjectName("scanPage");auto *v=new QVBoxLayout(this);v->setContentsMargins(20,24,20,24);v->setSpacing(16);
 auto *title=new QLabel(QStringLiteral("扫一扫"),this);title->setObjectName("scanHeading");auto titleFont=title->font();titleFont.setPointSize(24);titleFont.setBold(true);title->setFont(titleFont);v->addWidget(title);
 auto *hint=new QLabel(QStringLiteral("扫描桩身二维码，或输入电桩编号，前往充电页确认开始。"),this);hint->setObjectName("scanAdapterHint");hint->setWordWrap(true);v->addWidget(hint);v->addStretch();
 cameraButton_=new QPushButton(QStringLiteral("打开摄像头扫码"),this);cameraButton_->setObjectName("scanCameraButton");v->addWidget(cameraButton_);
 imageButton_=new QPushButton(QStringLiteral("识别二维码图片"),this);imageButton_->setObjectName("scanImageButton");v->addWidget(imageButton_);
 pileCodeInput_=new QLineEdit(this);pileCodeInput_->setObjectName("scanPileCodeInput");pileCodeInput_->setPlaceholderText(QStringLiteral("输入电桩编号"));pileCodeInput_->setMaxLength(64);v->addWidget(pileCodeInput_);
 startButton_=new QPushButton(QStringLiteral("确认电桩编号"),this);startButton_->setObjectName("scanStartButton");startButton_->setProperty("role","primary");v->addWidget(startButton_);
 messageLabel_=new QLabel(this);messageLabel_->setObjectName("scanMessage");messageLabel_->setWordWrap(true);v->addWidget(messageLabel_);v->addStretch();
 connect(startButton_,&QPushButton::clicked,this,[this]{submitPileCode(pileCodeInput_->text());});
 connect(pileCodeInput_,&QLineEdit::returnPressed,this,[this]{submitPileCode(pileCodeInput_->text());});
 connect(cameraButton_,&QPushButton::clicked,this,[this]{openScanner(true);});
 connect(imageButton_,&QPushButton::clicked,this,[this]{openScanner(false);});
#ifndef CHARGING_CLIENT_HAS_SCANNER
 cameraButton_->hide();imageButton_->hide();hint->setText(QStringLiteral("此构建可输入电桩编号；启用摄像头构建后可直接扫码。"));
#endif
}
void ScanPage::submitPileCode(const QString &input){
 const QString code=input.trimmed();static const QRegularExpression valid("^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$");
 if(!valid.match(code).hasMatch()){showMessage(QStringLiteral("请输入有效的电桩编号"),true);return;}
 pileCodeInput_->setText(code);closeScanner();emit scanRequested(code);
}
void ScanPage::preparePileCode(const QString &code){pileCodeInput_->setText(code.trimmed());}
void ScanPage::prepareDirectPileCode(const QString &code){preparePileCode(code);}
void ScanPage::setLoading(bool loading){if(loading)closeScanner();cameraButton_->setDisabled(loading);imageButton_->setDisabled(loading);pileCodeInput_->setDisabled(loading);startButton_->setDisabled(loading);}
void ScanPage::showMessage(const QString &message,bool error){messageLabel_->setText(message);messageLabel_->setStyleSheet(error?"color:#c62828;":"color:#245c45;");}
void ScanPage::reset(){closeScanner();setLoading(false);pileCodeInput_->clear();messageLabel_->clear();}
void ScanPage::openScanner(bool camera){
#ifdef CHARGING_CLIENT_HAS_SCANNER
 if(scannerDialog_||!isVisible()||!startButton_->isEnabled())return;
 auto *dialog=new QrScanDialog(camera?QrScanDialog::Source::Camera:QrScanDialog::Source::Image,this,true);
 scannerDialog_=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);
 connect(dialog,&QrScanDialog::pileCodeDecoded,this,[this](const QString&code){
     QTimer::singleShot(0,this,[this,code]{if(isVisible())submitPileCode(code);});
 });
 connect(dialog,&QDialog::finished,this,[this,camera](int result){scannerDialog_.clear();if(camera&&result==QDialog::Rejected&&!closingScanner_&&isVisible())emit cancelled();});
 dialog->show();dialog->raise();if(!camera)QTimer::singleShot(0,dialog,&QrScanDialog::chooseImage);
#else
 Q_UNUSED(camera);
#endif
}
void ScanPage::closeScanner(){closingScanner_=true;if(scannerDialog_)scannerDialog_->reject();scannerDialog_.clear();closingScanner_=false;}
void ScanPage::showEvent(QShowEvent *event){QWidget::showEvent(event);QTimer::singleShot(0,this,[this]{if(isVisible())openScanner(true);});}
void ScanPage::hideEvent(QHideEvent *event){closeScanner();QWidget::hideEvent(event);}
void ScanPage::resizeEvent(QResizeEvent *event){QWidget::resizeEvent(event);if(scannerDialog_)scannerDialog_->setGeometry(rect());}
}
