#pragma once

#include "api/api_result.h"

#include <QList>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace charging::client {

class StationBrowserPage final : public QWidget {
    Q_OBJECT

public:
    explicit StationBrowserPage(QWidget *parent = nullptr);

    [[nodiscard]] StationQuery stationQuery() const;
    void setGreeting(const QString &nickname, bool isNewUser);
    void setListLoading(bool loading);
    void setReservationBusy(bool busy);
    void showStations(const QList<protocol::StationDto> &stations);
    void showListError(const QString &message);
    void showListMessage(const QString &message, bool error = false);
    void showCurrentOrder(const std::optional<protocol::OrderDto> &order);
    void showListPage();
    void showDetailLoading();
    void showStationDetail(const StationDetailPayload &detail);
    void showDetailError(const QString &message);
    void showDetailMessage(const QString &message, bool error = false);
    void reset();

signals:
    void refreshRequested();
    void stationSelected(qint64 stationId);
    void reservationRequested(const QString &pileCode);
    void cancellationRequested(qint64 orderId);
    void detailBackRequested();

private:
    void clearStationCards();
    void clearPileCards();
    void updateLocationSummary();

    QStackedWidget *pages_ = nullptr;
    QWidget *listPage_ = nullptr;
    QWidget *detailPage_ = nullptr;
    QLabel *welcomeLabel_ = nullptr;
    QLabel *loginNoticeLabel_ = nullptr;
    QLabel *actionMessageLabel_ = nullptr;
    QWidget *currentOrderCard_ = nullptr;
    QLabel *currentOrderSummaryLabel_ = nullptr;
    QPushButton *cancelOrderButton_ = nullptr;
    QCheckBox *demoLocationCheck_ = nullptr;
    QLabel *locationSummaryLabel_ = nullptr;
    QLineEdit *regionInput_ = nullptr;
    QLineEdit *keywordInput_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
    QLabel *listMessageLabel_ = nullptr;
    QWidget *stationListContent_ = nullptr;
    QVBoxLayout *stationListLayout_ = nullptr;
    QPushButton *backButton_ = nullptr;
    QLabel *detailMessageLabel_ = nullptr;
    QWidget *detailContent_ = nullptr;
    QLabel *detailNameLabel_ = nullptr;
    QLabel *detailMetaLabel_ = nullptr;
    QLabel *detailPriceLabel_ = nullptr;
    QVBoxLayout *pileListLayout_ = nullptr;
    QList<QPushButton *> reservationButtons_;
    bool reservationBusy_ = false;
};

}  // namespace charging::client
