// 计价规则说明按钮的声明
#pragma once

#include <QPointer>
#include <QToolButton>

class QDialog;

namespace charging::client {

// Shared, on-demand explanation beside a reference or locked unit price.
// 可复用的问号按钮，按需展示价格说明
class PricingInfoButton final : public QToolButton {
    Q_OBJECT
public:
    // 设置规则文本，空文本则隐藏按钮
    explicit PricingInfoButton(QWidget *parent = nullptr);
    void setRules(const QString &rules);

protected:
    void hideEvent(QHideEvent *event) override;

private:
    void showRules();
    void closeRules();
    QString rules_;
    // 用弱指针跟踪弹窗，避免重复打开
    QPointer<QDialog> dialog_;
};

}  // namespace charging::client
