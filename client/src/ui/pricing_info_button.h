#pragma once

#include <QPointer>
#include <QToolButton>

class QDialog;

namespace charging::client {

// Shared, on-demand explanation beside a reference or locked unit price.
class PricingInfoButton final : public QToolButton {
    Q_OBJECT
public:
    explicit PricingInfoButton(QWidget *parent = nullptr);
    void setRules(const QString &rules);

protected:
    void hideEvent(QHideEvent *event) override;

private:
    void showRules();
    void closeRules();
    QString rules_;
    QPointer<QDialog> dialog_;
};

}  // namespace charging::client
