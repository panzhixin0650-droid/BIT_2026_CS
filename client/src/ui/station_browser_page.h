#pragma once

#include "api/api_result.h"
#include "local/map_types.h"

#include <QList>
#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;
class QStackedWidget;
class QScrollArea;
class QVBoxLayout;

namespace charging::client {

class RouteMapView;
class StationMapView;
class StationPreviewCard;
class PricingInfoButton;

class StationBrowserPage final : public QWidget {
    Q_OBJECT

public:
    explicit StationBrowserPage(QWidget *parent = nullptr);

    [[nodiscard]] StationQuery stationQuery() const;
    [[nodiscard]] MapLocation currentLocation() const;
    void setUserId(qint64 userId);
    void showVisitHistory(const QList<protocol::OrderDto> &orders);
    void showVisitHistoryError();
    void setSheetPosition(int position);
    bool isMapFullscreen() const { return sheetHidden_; }
    void setBottomNavigationInset(int inset);
    void setGreeting(const QString &nickname, bool isNewUser);
    void setGreetingNickname(const QString &nickname);
    void setListLoading(bool loading);
    void setReservationBusy(bool busy);
    void showStations(const QList<protocol::StationDto> &stations);
    void showListError(const QString &message);
    void showListMessage(const QString &message, bool error = false);
    void showCurrentOrder(const std::optional<protocol::OrderDto> &order);
    [[nodiscard]] bool isShowingStationDetail() const;
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
    void preloadMap(const QUrl &scriptUrl);
    void configureHomeMap(const QUrl &scriptUrl);
    void prepareHomeMap(const QSize &availableSize);
    void setRouteBusy(bool busy);
    void showRouteMessage(const QString &message, bool error = false);
    void showRouteResult(const RouteResult &result);
    void reset();

signals:
    void mapFullscreenChanged(bool fullscreen);
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
    void directChargingRequested(const QString &pileCode);
    void progressRequested(qint64 orderId);
    void stopRequested(qint64 orderId);
    void detailBackRequested();
    void navigationClosed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void openSearch();
    void openLocationSettings();
    void submitSearch();
    void applyDiscovery();
    void renderDiscovery();
    void renderSearch();
    void renderStationSections(QWidget *container, const QList<protocol::StationDto> &items, bool search);
    void setupMapHome();
    qint64 userId_ = 0;
    QStringList searchHistory_;
    QList<qint64> visitedStationIds_;
    bool visitHistoryFailed_ = false;
    QList<protocol::StationDto> catalog_;
    QString appliedKeyword_;
    QWidget *searchPage_ = nullptr;
    QWidget *searchContent_ = nullptr;
    QLabel *searchMessage_ = nullptr;
    QWidget *locationPage_ = nullptr;
    QWidget *locationReturnPage_ = nullptr;
    QWidget *overviewContent_ = nullptr;
    QWidget *discoveryList_ = nullptr;
    QScrollArea *overviewScroll_ = nullptr;
    QStackedWidget *sheetPages_ = nullptr;
    QPushButton *sheetHandle_ = nullptr;
    QPushButton *homeSearchButton_ = nullptr;
    int sheetPosition_ = 1;
    bool sheetHidden_ = false;
    int bottomNavigationInset_ = 0;
    bool sheetDragging_ = false;
    int dragStartY_ = 0;
    int dragStartHeight_ = 0;
    int dragHeight_ = 0;
    void layoutHomeOverlays();
    void previewStation(qint64 stationId);
    void clearPileCards();
    void updateDirectChargingButtons();
    void updateLocationSummary();
    void updateRouteControls();

    QStackedWidget *pages_ = nullptr;
    QWidget *listPage_ = nullptr;
    StationMapView *stationMap_ = nullptr;
    StationPreviewCard *stationPreview_ = nullptr;
    QWidget *homeOverlay_ = nullptr;
    QScrollArea *locationScroll_ = nullptr;
    QList<protocol::StationDto> stations_;
    QWidget *detailPage_ = nullptr;
    QWidget *navigationPage_ = nullptr;
    QWidget *navigationReturnPage_ = nullptr;
    QLabel *welcomeLabel_ = nullptr;
    QLabel *loginNoticeLabel_ = nullptr;
    QLabel *actionMessageLabel_ = nullptr;
    QWidget *currentOrderCard_ = nullptr;
    QPushButton *currentOrderToggle_ = nullptr;
    QWidget *currentOrderDetails_ = nullptr;
    QLabel *currentOrderSummaryLabel_ = nullptr;
    QLabel *currentOrderProgressLabel_ = nullptr;
    QPushButton *cancelOrderButton_ = nullptr;
    QPushButton *currentOrderNavigationButton_ = nullptr;
    QPushButton *reservationScanButton_ = nullptr;
    QPushButton *progressButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QComboBox *locationPresetCombo_ = nullptr;
    QLineEdit *locationAddressInput_ = nullptr;
    QPushButton *resolveLocationButton_ = nullptr;
    QLabel *locationSummaryLabel_ = nullptr;
    QLabel *locationCaption_ = nullptr;
    QLabel *stationCountLabel_ = nullptr;
    QWidget *locationContent_ = nullptr;
    QPushButton *locationEntry_ = nullptr;
    QLabel *locationMessageLabel_ = nullptr;
    QLineEdit *keywordInput_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
    QLabel *listMessageLabel_ = nullptr;
    QPushButton *backButton_ = nullptr;
    QLabel *detailMessageLabel_ = nullptr;
    QWidget *detailContent_ = nullptr;
    QLabel *detailNameLabel_ = nullptr;
    QLabel *detailMetaLabel_ = nullptr;
    QLabel *detailPriceLabel_ = nullptr;
    PricingInfoButton *detailPricingInfo_ = nullptr;
    QPushButton *detailNavigationButton_ = nullptr;
    QVBoxLayout *pileListLayout_ = nullptr;
    QLineEdit *routeStartInput_ = nullptr;
    QLabel *routeDestinationLabel_ = nullptr;
    QComboBox *routeModeCombo_ = nullptr;
    QPushButton *routePlanButton_ = nullptr;
    QLabel *routeMessageLabel_ = nullptr;
    QStackedWidget *routeDisplayStack_ = nullptr;
    QLabel *routeDisplayLabel_ = nullptr;
    RouteMapView *routeMapView_ = nullptr;
    QLabel *routeSummaryLabel_ = nullptr;
    QPlainTextEdit *routeDetails_ = nullptr;
    QPushButton *routeDetailsButton_ = nullptr;
    bool routeRequestBusy_ = false;
    bool mapLoading_ = false;
    QList<QPushButton *> reservationButtons_;
    QList<QPushButton *> directChargingButtons_;
    std::optional<protocol::OrderDto> currentOrder_;
    MapLocation currentLocation_{QStringLiteral("演示位置"), 123.42, 41.70};
    protocol::StationDto navigationStation_;
    bool reservationBusy_ = false;
};

}  // namespace charging::client
