#pragma once

#include "api/api_result.h"
#include "local/map_types.h"

#include <QList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class QWebEngineView;

namespace charging::client {

class StationBrowserPage final : public QWidget {
    Q_OBJECT

public:
    explicit StationBrowserPage(QWidget *parent = nullptr);

    [[nodiscard]] StationQuery stationQuery() const;
    [[nodiscard]] MapLocation currentLocation() const;
    void setGreeting(const QString &nickname, bool isNewUser);
    void setGreetingNickname(const QString &nickname);
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
    void setLocationBusy(bool busy);
    void setResolvedLocation(const MapLocation &location);
    void showLocationMessage(const QString &message, bool error = false);
    void showNavigation(const protocol::StationDto &station,
                        const MapLocation &start);
    void setRouteBusy(bool busy);
    void showRouteMessage(const QString &message, bool error = false);
    void showRouteResult(const RouteResult &result);
    void reset();

signals:
    void refreshRequested();
    void locationResolutionRequested(const QString &address);
    void stationSelected(qint64 stationId);
    void navigationRequested(const charging::protocol::StationDto &station);
    void currentOrderNavigationRequested(qint64 stationId);
    void routeRequested(const QString &startAddress,
                        charging::client::RouteMode mode);
    void reservationRequested(const QString &pileCode);
    void cancellationRequested(qint64 orderId);
    void reservationScanRequested(const QString &pileCode);
    void progressRequested(qint64 orderId);
    void stopRequested(qint64 orderId);
    void detailBackRequested();

private:
    void clearStationCards();
    void clearPileCards();
    void updateLocationSummary();

    QStackedWidget *pages_ = nullptr;
    QWidget *listPage_ = nullptr;
    QWidget *detailPage_ = nullptr;
    QWidget *navigationPage_ = nullptr;
    QWidget *navigationReturnPage_ = nullptr;
    QLabel *welcomeLabel_ = nullptr;
    QLabel *loginNoticeLabel_ = nullptr;
    QLabel *actionMessageLabel_ = nullptr;
    QWidget *currentOrderCard_ = nullptr;
    QLabel *currentOrderSummaryLabel_ = nullptr;
    QLabel *currentOrderProgressLabel_ = nullptr;
    QPushButton *cancelOrderButton_ = nullptr;
    QPushButton *currentOrderNavigationButton_ = nullptr;
    QPushButton *reservationScanButton_ = nullptr;
    QPushButton *progressButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QCheckBox *demoLocationCheck_ = nullptr;
    QComboBox *locationPresetCombo_ = nullptr;
    QLineEdit *locationAddressInput_ = nullptr;
    QPushButton *resolveLocationButton_ = nullptr;
    QLabel *locationSummaryLabel_ = nullptr;
    QLabel *locationMessageLabel_ = nullptr;
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
    QPushButton *detailNavigationButton_ = nullptr;
    QVBoxLayout *pileListLayout_ = nullptr;
    QLineEdit *routeStartInput_ = nullptr;
    QLabel *routeDestinationLabel_ = nullptr;
    QComboBox *routeModeCombo_ = nullptr;
    QPushButton *routePlanButton_ = nullptr;
    QLabel *routeMessageLabel_ = nullptr;
    QStackedWidget *routeDisplayStack_ = nullptr;
    QLabel *routeDisplayLabel_ = nullptr;
    QWebEngineView *routeWebView_ = nullptr;
    QList<QPushButton *> reservationButtons_;
    MapLocation currentLocation_{QStringLiteral("演示位置"), 123.42, 41.70};
    protocol::StationDto navigationStation_;
    bool reservationBusy_ = false;
};

}  // namespace charging::client
