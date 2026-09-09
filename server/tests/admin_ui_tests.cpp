#include "admin_ui/admin_facade.h"
#include "admin_ui/admin_window.h"
#include "admin_ui/admin_analytics.h"
#include "admin_ui/admin_time_format.h"
#include "admin_ui/animated_metric_label.h"
#include "admin_ui/analysis_bar_chart.h"
#include "admin_ui/pile_status_chart.h"
#include "admin_ui/revenue_chart.h"
#include <QScrollArea>
#include <QScrollBar>
#include <QJsonArray>
#include "admin_ui/support_tickets_page.h"
#include "application/application_service.h"
#include "application/session_store.h"
#include "adapters/mock_pile.h"
#include "adapters/mock_prediction_provider.h"
#include "persistence/in_memory_repository.h"

#include <QDir>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QDateEdit>
#include <QMessageBox>
#include <QMenu>
#include <QDialogButtonBox>
#include <QTreeWidget>
#include <QUuid>
#include <QCheckBox>
#include <QRadioButton>
#include <QDateTimeEdit>
#include <functional>
#include <algorithm>
#include <QTimer>
#include <QPushButton>
#include <QScreen>
#include <QTableWidget>
#include <QToolButton>
#include <QtTest>

using namespace charging::server;

namespace {

struct LoginFixture {
    InMemoryRepository repository;
    SessionStore sessions;
    MockPile pileGateway;
    MockPredictionProvider predictions;
    ApplicationService service{&repository, &sessions, &pileGateway, &predictions};
    AdminFacade facade{&service};
    AdminWindow window{&facade, false, 0, false};
    QWidget *page = window.findChild<QWidget *>(QStringLiteral("loginPage"));
    QList<QLineEdit *> inputs = page->findChildren<QLineEdit *>();
    QLineEdit *username = inputs.at(0);
    QLineEdit *password = inputs.at(1);
    QLabel *error = page->findChild<QLabel *>(QStringLiteral("loginError"));
    QPushButton *submit = page->findChild<QPushButton *>(QStringLiteral("loginSubmit"));
};

// Read the pixels already presented by Qt. QWidget::grab()/render() forces
// a new paint and can hide corruption caused by an incremental update.
QImage screenImage(QWidget &window, const QString &name)
{
    QTest::qWait(100);
    const QPixmap snapshot = window.screen()->grabWindow(window.winId());
    const QString artifactDirectory = qEnvironmentVariable("CHARGING_UI_ARTIFACT_DIR");
    if (!artifactDirectory.isEmpty() && !snapshot.isNull()) {
        snapshot.save(QDir(artifactDirectory).filePath(name + QStringLiteral(".png")));
    }
    return snapshot.toImage().scaled(window.size(), Qt::IgnoreAspectRatio,
                                    Qt::FastTransformation);
}

QString unexpectedColor(const QImage &image, const QRect &area, const QColor &expected)
{
    if (area.isEmpty() || !image.rect().contains(area)) {
        return QStringLiteral("Invalid sample area");
    }
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            const QColor actual = image.pixelColor(x, y);
            if (actual != expected) {
                return QStringLiteral("Pixel (%1, %2): expected %3, got %4")
                    .arg(x).arg(y).arg(expected.name(), actual.name());
            }
        }
    }
    return {};
}

// Sample the right side of each row, away from label text and rounded edges.
QRect formGapArea(const QWidget *above, const QWidget *below, const QWidget &window)
{
    const QPoint top = above->mapTo(&window, QPoint());
    const QPoint bottom = below->mapTo(&window, QPoint());
    return QRect(top.x() + above->width() - 70, top.y() + above->height() + 2, 40,
                 bottom.y() - top.y() - above->height() - 4);
}

QRect errorArea(const LoginFixture &fixture)
{
    const QPoint error = fixture.error->mapTo(&fixture.window, QPoint());
    return QRect(error.x() + fixture.error->width() - 70, error.y() + 8,
                 40, fixture.error->height() - 16);
}

QString unexpectedSurfacePixels(const LoginFixture &fixture, const QImage &image,
                                const QColor &errorColor)
{
    // The warning's own fill can look correct while the gaps above and below
    // it still reveal the artwork. Check the entire reserved row as well.
    const QList<QRect> gaps{
        formGapArea(fixture.password, fixture.error, fixture.window),
        formGapArea(fixture.error, fixture.submit, fixture.window),
        formGapArea(fixture.username, fixture.password, fixture.window),
    };
    for (const QRect &area : gaps) {
        const QString mismatch = unexpectedColor(image, area, QColor("#ffffff"));
        if (!mismatch.isEmpty()) return mismatch;
    }
    return unexpectedColor(image, errorArea(fixture), errorColor);
}

} // namespace

class AdminUiTests final : public QObject {
    Q_OBJECT

private slots:
    void chartsActivateActualDataWithMouseAndKeyboard();
    void occupancyBandsAndAnalysisDrillThroughMatchSourceData();
    void ordersSortNewestFirstAndUseSingleTimeFilter();
    void metricAnimationKeepsAuthoritativeValuesAndUnits();
    void timestampsUseBeijingTimeAcrossDateBoundaries();
    void loginFailurePreservesInputAndAllowsRetry();
    void passwordVisibilityAndEmptyLogin();
    void analyticsUsesPaidDatesAndKeepsReceivablesSeparate();
    void analysisPagesAndSidebarRemainUsable();
    void invalidRevenueRangeClearsStaleAnalysisAndCanRecover();
    void loginSurfaceSurvivesPartialRepaints_data();
    void loginSurfaceSurvivesPartialRepaints();
    void systemAdminCanOpenAdminManagementPage();
    void adminDetailsAndAccountFormsFollowManagementFlow();
    void navigationRestoresViewsAndRefreshIsLocal();
    void chartAnimationsFollowNavigationAndRefreshRules();
    void repairTicketsLocatePilesAndExpandedStationsWithHistory();
    void ordersLocateAssociatedAssetsAndRestoreSource();
    void filtersIncludeNewRegionsAndStayWithinScreen();
    void userAdminOnlySeesAuthorizedPages();
    void supportTicketPageSavesAndLogoutClears();
    void supportTicketPageSavesAndLogoutClears_data() {
        QTest::addColumn<bool>("repair");
        QTest::newRow("support") << false;
        QTest::newRow("repair") << true;
    }
    void revokedTicketAccessClearsCachedPage();
};

void AdminUiTests::chartsActivateActualDataWithMouseAndKeyboard()
{
    RevenueChart line;
    line.resize(480,320); line.setPoints({{"2026-09-01",100},{"2026-09-02",0},{"2026-09-03",200}});
    line.show(); QVERIFY(QTest::qWaitForWindowExposed(&line));
    QSignalSpy dates(&line,&RevenueChart::dateClicked);
    QTest::mouseClick(&line,Qt::LeftButton,Qt::NoModifier,QPoint(60,100));
    QCOMPARE(dates.size(),1); QCOMPARE(dates.takeFirst().first().toString(),QStringLiteral("2026-09-01"));
    line.setPoints({{"2026-09-01",100},{"2026-09-02",0},{"2026-09-03",200}});
    QTest::keyClick(&line,Qt::Key_Right); QTest::keyClick(&line,Qt::Key_Right); QTest::keyClick(&line,Qt::Key_Return);
    QCOMPARE(dates.takeFirst().first().toString(),QStringLiteral("2026-09-02"));
    line.setPoints({}); QTest::mouseClick(&line,Qt::LeftButton,Qt::NoModifier,QPoint(60,100)); QVERIFY(dates.isEmpty());
    AnalysisBarChart bars;
    const QList<AnalysisBar> data{{"重名站",5,"5 单",Qt::blue,"101"},{"重名站",0,"0 单",Qt::green,"202"}};
    bars.resize(480,320); bars.setBars(data); bars.show(); QVERIFY(QTest::qWaitForWindowExposed(&bars));
    QSignalSpy keys(&bars,&AnalysisBarChart::barClicked);
    QTest::mouseClick(&bars,Qt::LeftButton,Qt::NoModifier,QPoint(30,20));
    QCOMPARE(keys.takeFirst().first().toString(),QStringLiteral("101"));
    bars.setBars(data); QTest::keyClick(&bars,Qt::Key_Down); QTest::keyClick(&bars,Qt::Key_Down); QTest::keyClick(&bars,Qt::Key_Space);
    QCOMPARE(keys.takeFirst().first().toString(),QStringLiteral("202"));
    PileStatusChart pie;
    pie.setSlices({{"HIGH","高占用",2,Qt::red},{"LOW","低占用",0,Qt::green}});
    pie.resize(480,320); pie.show(); QVERIFY(QTest::qWaitForWindowExposed(&pie));
    QSignalSpy slices(&pie,&PileStatusChart::statusClicked);
    QTest::keyClick(&pie,Qt::Key_Down); QTest::keyClick(&pie,Qt::Key_Down); QTest::keyClick(&pie,Qt::Key_Return);
    QCOMPARE(slices.takeFirst().first().toString(),QStringLiteral("LOW"));
}

void AdminUiTests::occupancyBandsAndAnalysisDrillThroughMatchSourceData()
{
    QCOMPARE(stationOccupancyBand(0,0),-1);
    for (const auto &example : {QPair<int,int>{100,0},{99,1},{70,1},{69,2},{40,2},{39,3},{20,3},{19,4},{0,4}})
        QCOMPARE(stationOccupancyBand(example.first,100),example.second);
    LoginFixture fixture;
    fixture.window.resize(1380,860); fixture.window.show();
    fixture.username->setText("admin"); fixture.password->setText("123456"); fixture.submit->click();
    auto *nav=fixture.window.findChild<QListWidget *>("navigation");
    auto *back=fixture.window.findChild<QToolButton *>("adminPageBack");
    auto *orders=fixture.window.findChild<QTableWidget *>("ordersTable");
    auto *piles=fixture.window.findChild<QTableWidget *>("pilesTable");
    auto *stations=fixture.window.findChild<QTreeWidget *>();
    const auto metric = [&](const QString &key) -> QWidget * {
        for (auto *frame : fixture.window.findChildren<QFrame *>())
            if (frame->property("actionKey").toString()==key) return frame;
        return nullptr;
    };
    const auto chooseMetric = [&](const QString &key,const QString &actionText) {
        auto *card=metric(key); QVERIFY(card);
        bool chosen=false;
        QTimer::singleShot(30,&fixture.window,[&] {
            auto *menu=fixture.window.findChild<QMenu *>("metricActionMenu");
            QVERIFY(menu); QTimer::singleShot(2000,menu,&QMenu::close);
            for (auto *action : menu->actions()) if (action->text()==actionText) { chosen=true; action->trigger(); menu->close(); break; }
        });
        QTest::mouseClick(card,Qt::LeftButton); QVERIFY(chosen);
    };
    chooseMetric("operatingStations",QStringLiteral("运营中站点"));
    QCOMPARE(nav->currentRow(),2); back->click();
    chooseMetric("occupiedPiles",QStringLiteral("充电中电桩"));
    QCOMPARE(nav->currentRow(),3); QCOMPARE(piles->rowCount(),1); back->click();
    chooseMetric("abnormalPiles",QStringLiteral("全部异常电桩"));
    QCOMPARE(nav->currentRow(),3); QCOMPARE(piles->rowCount(),2); back->click();
    auto *available=metric("availablePiles"); QVERIFY(available);
    QTest::mouseClick(available,Qt::LeftButton);
    QCOMPARE(nav->currentRow(),3);
    QVERIFY(fixture.window.findChild<QPushButton *>("pileActiveStationScope")->isVisible());
    for (int row=0;row<piles->rowCount();++row) QCOMPARE(piles->item(row,4)->data(Qt::UserRole).toString(),QStringLiteral("IDLE"));
    back->click(); QCOMPARE(nav->currentRow(),0);
    auto *occupancy=fixture.window.findChild<PileStatusChart *>("stationOccupancyChart"); QVERIFY(occupancy);
    QCOMPARE(occupancy->accessibleDescription().count(QStringLiteral("：")),5);
    occupancy->statusClicked("2"); // The seeded station with 1 / 2 occupied piles.
    QCOMPARE(nav->currentRow(),2); QCOMPARE(stations->topLevelItemCount(),1);
    QCOMPARE(stations->topLevelItem(0)->data(0,Qt::UserRole).toLongLong(),1);
    back->click();
    auto *ops=fixture.window.findChild<QScrollArea *>("operationsAnalysisScroll");
    AnalysisBarChart *faults=nullptr;
    for (auto *chart : ops->findChildren<AnalysisBarChart *>()) faults=chart;
    QVERIFY(faults && !faults->bars().isEmpty());
    const auto faultStation=faults->bars().first().key.toLongLong();
    faults->barClicked(faults->bars().first().key);
    QCOMPARE(nav->currentRow(),3);
    for (int row=0;row<piles->rowCount();++row) {
        const auto storedPiles=fixture.repository.listPiles();
        const auto id=piles->item(row,0)->data(Qt::UserRole).toLongLong();
        const auto dto=std::find_if(storedPiles.cbegin(),storedPiles.cend(),[&](const auto &pile) { return pile.pileId==id; });
        QVERIFY(dto!=storedPiles.cend()); QCOMPARE(dto->stationId,faultStation);
        QVERIFY(dto->status==charging::protocol::PileStatus::Fault || dto->status==charging::protocol::PileStatus::Offline);
    }
    nav->setCurrentRow(1);
    chooseMetric("resources",QStringLiteral("查看充电桩"));
    QCOMPARE(nav->currentRow(),3); QCOMPARE(piles->rowCount(),6); back->click();
    auto *revenue=fixture.window.findChild<QScrollArea *>("revenueAnalysisScroll");
    auto *range=revenue->findChild<QComboBox *>(); range->setCurrentIndex(range->findData(30));
    QTest::qWait(20);
    const auto source=fixture.repository.listOrders();
    const QTimeZone zone("Asia/Shanghai");
    const QDate today=QDateTime::currentDateTimeUtc().toTimeZone(zone).date();
    const auto verifyRows = [&](const QDate &from,const QDate &to,qint64 stationId,const QString &mode,int period) {
        QSet<qint64> expected,actual;
        for (const auto &order : source) {
            if (order.status!=charging::protocol::OrderStatus::Completed || !order.paidAt) continue;
            const auto paid=QDateTime::fromString(*order.paidAt,Qt::ISODate).toTimeZone(zone).date();
            if ((from.isValid() && paid<from) || (to.isValid() && paid>to)) continue;
            if (stationId>0 && order.stationId!=stationId) continue;
            if (!mode.isEmpty() && charging::protocol::toString(order.mode)!=mode) continue;
            if (period>=0 && (!order.startedAt || QDateTime::fromString(*order.startedAt,Qt::ISODate).toTimeZone(zone).time().hour()/4!=period)) continue;
            expected.insert(order.orderId);
        }
        for (int row=0;row<orders->rowCount();++row) actual.insert(orders->item(row,0)->data(Qt::UserRole).toLongLong());
        QCOMPARE(actual,expected);
    };
    for (const QString &key : {"todayRevenue","monthRevenue","totalRevenue","rangeRevenue","rangeOrders","rangeEnergy","rangeAverage"}) {
        auto *card=metric(key); QVERIFY(card);
        card->setFocus(); QTest::keyClick(card,Qt::Key_Return);
        QCOMPARE(nav->currentRow(),5);
        if (key=="todayRevenue") verifyRows(today,today,0,{},-1);
        else if (key=="monthRevenue") verifyRows(QDate(today.year(),today.month(),1),QDate(today.year(),today.month(),today.daysInMonth()),0,{},-1);
        else if (key=="totalRevenue") verifyRows({},{},0,{},-1);
        else verifyRows(today.addDays(-29),today,0,{},-1);
        back->click(); QCOMPARE(nav->currentRow(),1); QCOMPARE(range->currentData().toInt(),30);
    }
    auto *line=revenue->findChildren<RevenueChart *>().first();
    line->dateClicked(today.toString(Qt::ISODate));
    QCOMPARE(nav->currentRow(),5); verifyRows(today,today,0,{},-1); back->click();
    auto *ranking=fixture.window.findChild<AnalysisBarChart *>("stationRevenueChart");
    QVERIFY(ranking && !ranking->bars().isEmpty());
    const auto stationId=ranking->bars().first().key.toLongLong();
    ranking->barClicked(ranking->bars().first().key);
    QCOMPARE(nav->currentRow(),5); verifyRows(today.addDays(-29),today,stationId,{},-1); back->click();
    revenue->findChild<PileStatusChart *>()->statusClicked("DIRECT");
    QCOMPARE(nav->currentRow(),5); verifyRows(today.addDays(-29),today,0,"DIRECT",-1); back->click();
    AnalysisBarChart *periods=nullptr;
    for (auto *chart : revenue->findChildren<AnalysisBarChart *>()) if (chart!=ranking) periods=chart;
    QVERIFY(periods); periods->barClicked("5");
    QCOMPARE(nav->currentRow(),5); verifyRows(today.addDays(-29),today,0,{},5);
    screenImage(fixture.window,"analysis-order-drill-through");
    fixture.window.findChild<QToolButton *>("adminPageRefresh")->click();
    QCOMPARE(orders->rowCount(),source.size());
}

void AdminUiTests::ordersSortNewestFirstAndUseSingleTimeFilter()
{
    LoginFixture fixture;
    const auto login = fixture.service.loginUser({{"phone", "13912345678"}});
    QVERIFY(login.ok());
    const auto userId = login.data.value("user").toObject().value("userId").toInteger();
    QVERIFY(userId > 0);
    const auto now = QDateTime::fromSecsSinceEpoch(QDateTime::currentSecsSinceEpoch(), Qt::UTC);
    const QList<QPair<QString,QDateTime>> samples{
        {"recent",now.addSecs(-3600)}, {"dayOld",now.addSecs(-86400-60)},
        {"week",now.addSecs(-7*86400+60)}, {"month",now.addSecs(-30*86400+60)},
        {"twoMonths",now.addSecs(-60*86400+60)}, {"threeMonths",now.addSecs(-90*86400+60)},
        {"older",now.addSecs(-90*86400-60)}, {"future",now.addSecs(3600)}
    };
    QVERIFY(fixture.repository.beginTransaction());
    for (const auto &sample : samples) {
        charging::protocol::OrderDto order;
        order.orderNo = "DATE-" + sample.first;
        order.createdAt = sample.second.addDays(-120).toString(Qt::ISODate);
        order.paidAt = sample.second.toString(Qt::ISODate);
        order.startedAt = sample.second.addSecs(-3600).toString(Qt::ISODate);
        order.userId = userId; order.stationId = 1; order.pileId = 1;
        order.mode = charging::protocol::OrderMode::Direct;
        order.status = charging::protocol::OrderStatus::Completed;
        QVERIFY(fixture.repository.createOrder(order).orderId > 0);
    }
    QVERIFY(fixture.repository.commitTransaction());
    fixture.window.show(); fixture.username->setText("admin"); fixture.password->setText("123456"); fixture.submit->click();
    auto *nav = fixture.window.findChild<QListWidget *>("navigation");
    nav->setCurrentRow(5);
    auto *table = fixture.window.findChild<QTableWidget *>("ordersTable");
    auto *search = fixture.window.findChild<QLineEdit *>("orderSearch");
    auto *filter = fixture.window.findChild<QPushButton *>("orderTimeFilter");
    QVERIFY(table && search && filter);
    const int allRows = table->rowCount();
    search->setText("DATE-");
    QCOMPARE(table->rowCount(),8);
    QCOMPARE(table->item(0,1)->text(),QStringLiteral("DATE-future"));
    for (int row=1; row<table->rowCount(); ++row)
        QVERIFY(table->item(row-1,8)->text() >= table->item(row,8)->text());
    const auto inPopup = [&](const std::function<void(QDialog *)> &operation) {
        bool entered = false;
        QTimer::singleShot(30,&fixture.window,[&] {
            auto *dialog = fixture.window.findChild<QDialog *>("orderTimeFilterPopup");
            QVERIFY(dialog); QTimer::singleShot(2500,dialog,&QDialog::reject);
            entered = true;
            operation(dialog);
        });
        filter->click();
        QVERIFY(entered);
    };
    const auto choose = [](QDialog *dialog, int hours) {
        const auto radios = dialog->findChildren<QRadioButton *>();
        QCOMPARE(radios.size(),6);
        for (auto *radio : radios)
            if (radio->property("rangeHours").toInt() == hours) radio->click();
        int checked = 0;
        for (auto *radio : radios) checked += radio->isChecked();
        QCOMPARE(checked,1);
    };
    for (const auto &rangeCount : {QPair<int,int>{24,1}, {7*24,3}, {30*24,4}, {60*24,5}, {90*24,6}}) {
        inPopup([&](QDialog *dialog) {
            choose(dialog,24);
            choose(dialog,rangeCount.first);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        QCOMPARE(table->rowCount(),rangeCount.second);
        QCOMPARE(table->item(0,1)->text(),QStringLiteral("DATE-recent"));
    }
    const QTimeZone zone("Asia/Shanghai");
    const auto setTime = [&](QDateTimeEdit *edit, const QDateTime &instant) {
        const auto local = instant.toTimeZone(zone);
        edit->setDate(local.date()); edit->setTime(local.time());
    };
    const auto startTime = samples[1].second;
    const auto endTime = samples[0].second;
    const auto tableTop = table->mapTo(&fixture.window,QPoint()).y();
    inPopup([&](QDialog *dialog) {
        choose(dialog,-1);
        auto *start = dialog->findChild<QDateTimeEdit *>("orderStartTime");
        auto *end = dialog->findChild<QDateTimeEdit *>("orderEndTime");
        auto *confirm = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        QVERIFY(start && end && start->isVisible() && end->isVisible());
        setTime(start,endTime); setTime(end,startTime);
        confirm->click();
        QVERIFY(dialog->isVisible());
        QVERIFY(dialog->findChild<QLabel *>("orderTimeError")->isVisible());
        setTime(start,startTime); setTime(end,endTime);
        QVERIFY(dialog->screen()->availableGeometry().contains(dialog->geometry()));
        screenImage(*dialog,"order-time-custom-popup");
        confirm->click();
    });
    QCOMPARE(table->rowCount(),2); // Both exact custom endpoints are included.
    QCOMPARE(table->mapTo(&fixture.window,QPoint()).y(),tableTop);
    table->selectRow(0);
    nav->setCurrentRow(7);
    fixture.window.findChild<QToolButton *>("adminPageBack")->click();
    QCOMPARE(table->rowCount(),2); QCOMPARE(table->currentRow(),0);
    inPopup([&](QDialog *dialog) {
        auto *start = dialog->findChild<QDateTimeEdit *>("orderStartTime");
        QCOMPARE(start->date(),startTime.toTimeZone(zone).date());
        QCOMPARE(start->time(),startTime.toTimeZone(zone).time());
        setTime(start,now.addDays(1));
        dialog->reject(); // Dismissed edits never replace the active filter.
    });
    QCOMPARE(table->rowCount(),2);
    screenImage(fixture.window,"orders-date-filter");
    inPopup([&](QDialog *dialog) {
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Reset)->click();
    });
    QCOMPARE(table->rowCount(),8);
    fixture.window.findChild<QToolButton *>("adminPageRefresh")->click();
    QVERIFY(search->text().isEmpty());
    QCOMPARE(table->rowCount(),allRows);
}

void AdminUiTests::metricAnimationKeepsAuthoritativeValuesAndUnits()
{
    AnimatedMetricLabel label(QStringLiteral("¥ 125.50"), nullptr);
    label.show();
    QVERIFY(QTest::qWaitForWindowExposed(&label));
    auto *intro = label.findChild<QVariantAnimation *>("metricIntroAnimation");
    QVERIFY(intro);
    const auto finalSize = label.sizeHint();
    label.playIntro();
    QCOMPARE(label.displayedText(), QStringLiteral("¥ 0.00"));
    intro->setCurrentTime(intro->duration() / 2);
    QCOMPARE(label.text(), QStringLiteral("¥ 125.50"));
    QCOMPARE(label.sizeHint(), finalSize);
    QVERIFY(QRegularExpression(QStringLiteral("^¥ [0-9]+\\.[0-9]{2}$")).match(label.displayedText()).hasMatch());
    const double intermediate = label.displayedText().mid(2).toDouble();
    QVERIFY(intermediate > 0 && intermediate < 125.50);
    label.hide();
    QCOMPARE(intro->state(), QAbstractAnimation::Stopped);
    QCOMPARE(label.displayedText(), label.text());
    label.show();
    QCOMPARE(intro->state(), QAbstractAnimation::Stopped);
    label.setText(QStringLiteral("3 / 9"));
    label.playIntro();
    QCOMPARE(label.displayedText(), QStringLiteral("0 / 0"));
    label.finishIntro();
    QCOMPARE(label.displayedText(), QStringLiteral("3 / 9"));
    label.setText(QStringLiteral("—"));
    label.playIntro();
    QCOMPARE(intro->state(), QAbstractAnimation::Stopped);
    QCOMPARE(label.displayedText(), QStringLiteral("—"));
}

void AdminUiTests::timestampsUseBeijingTimeAcrossDateBoundaries()
{
    QCOMPARE(adminTimeText("2026-09-08T18:30:45Z"), QStringLiteral("2026-09-09 02:30:45"));
    QCOMPARE(adminTimeText("2026-09-09T02:30:45+08:00"), QStringLiteral("2026-09-09 02:30:45"));
    QCOMPARE(adminTimeText("2026-09-08T18:30:45.123Z"), QStringLiteral("2026-09-09 02:30:45"));
    QCOMPARE(adminTimeText({}), QStringLiteral("—"));
    QCOMPARE(adminTimeText("invalid"), QStringLiteral("—"));
    QCOMPARE(adminTimeText({}, QStringLiteral("从未登录")), QStringLiteral("从未登录"));
}

void AdminUiTests::supportTicketPageSavesAndLogoutClears()
{
    QFETCH(bool, repair);
    LoginFixture fixture;
    const auto token = fixture.service.loginUser({{"phone", "13800000001"}}).data.value("token").toString();
    QJsonObject draft{
        {"submissionId", "b758e849-0cd0-4eb6-8aee-35c5c98fd553"},
        {"title", "<script>test</script>"}, {"summary", QStringLiteral("用户确认的订单页面异常")},
        {"sourceModel", "gpt-5.6-sol"}};
    if (repair) draft.insert("repair", QJsonObject{{"pileCode", "PILE-A-01"}, {"faultType", "screen"}});
    const auto result = fixture.service.createSupportTicket(token, draft);
    QVERIFY(result.ok());
    fixture.window.show();
    fixture.username->setText("admin"); fixture.password->setText("123456");
    fixture.submit->click();
    auto *navigation = fixture.window.findChild<QListWidget *>("navigation");
    QVERIFY(navigation);
    QCOMPARE(navigation->count(), 8);
    navigation->setCurrentRow(6);
    auto *page = fixture.window.findChild<QWidget *>("supportTicketsPage");
    QVERIFY(page && page->isVisible());
    auto *list = page->findChild<QListWidget *>("adminTicketList");
    QCOMPARE(list->count(), 1);
    if (repair) {
        QVERIFY(list->item(0)->text().contains(QStringLiteral("报修")));
        QVERIFY(page->findChild<QPlainTextEdit *>("adminTicketSummary")->toPlainText().contains("PILE-A-01"));
        QVERIFY(page->findChild<QPlainTextEdit *>("adminTicketSummary")->toPlainText().contains("screen"));
    }
    QVERIFY(page->findChild<QPlainTextEdit *>("adminTicketSummary")->toPlainText().contains("<script>"));
    auto *status = page->findChild<QComboBox *>("adminTicketStatus");
    status->setCurrentIndex(status->findData("RESOLVED"));
    page->findChild<QPushButton *>("adminTicketSave")->click();
    QVERIFY(page->findChild<QLabel *>("adminTicketNotice")->text().contains(QStringLiteral("失败")));
    page->findChild<QPlainTextEdit *>("adminTicketReply")->setPlainText(QStringLiteral("已核对，请刷新订单。"));
    page->findChild<QPushButton *>("adminTicketSave")->click();
    QVERIFY(page->findChild<QLabel *>("adminTicketNotice")->text().contains(QStringLiteral("已保存")));
    const auto stored = fixture.service.getSupportTicket(token, {{"ticketId", 1}});
    QCOMPARE(stored.data.value("ticket").toObject().value("status").toString(), QStringLiteral("RESOLVED"));
    if (repair) QVERIFY(list->item(0)->text().contains(QStringLiteral("报修")));
    const QString directory = qEnvironmentVariable("CHARGING_UI_ARTIFACT_DIR");
    if (!directory.isEmpty()) QVERIFY(fixture.window.grab().save(QDir(directory).filePath("admin-support-tickets.png")));
    for (auto *button : fixture.window.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("退出登录")) { button->click(); break; }
    }
    QCOMPARE(list->count(), 0);
    QVERIFY(!fixture.facade.listSupportTickets().ok());
}

void AdminUiTests::analyticsUsesPaidDatesAndKeepsReceivablesSeparate()
{
    using namespace charging::protocol;
    OrderDto order;
    order.orderId=1; order.orderNo="ANALYSIS-1"; order.createdAt="2026-09-07T00:00:00Z";
    order.userId=1; order.stationId=2; order.stationName=QStringLiteral("测试站");
    order.pileId=1; order.pileCode="PILE-A-01"; order.status=OrderStatus::Completed;
    order.mode=OrderMode::Direct; order.startedAt="2026-09-07T15:00:00Z";
    order.endedAt="2026-09-07T15:30:00Z"; order.paidAt="2026-09-07T16:00:00Z";
    order.unitPriceCentsPerKwh=120; order.energyWh=1000; order.amountCents=120;
    QJsonArray items{toJson(order)};
    order.orderId=2; order.paidAt="2026-09-07T15:59:59Z"; items.append(toJson(order));
    order.orderId=3; order.paidAt="2026-09-08T15:59:59Z"; order.mode=OrderMode::Reservation; items.append(toJson(order));
    order.orderId=4; order.paidAt="2026-09-08T16:00:00Z"; items.append(toJson(order));
    order.orderId=5; order.status=OrderStatus::PendingPayment; order.paidAt.reset();
    order.amountCents=777; items.append(toJson(order));
    const auto result=analyzeRevenue(items,QDate(2026,9,8),QDate(2026,9,8));
    QVERIFY(result.valid);
    QCOMPARE(result.receivedCents,240); QCOMPARE(result.paidOrders,2);
    QCOMPARE(result.energyWh,2000); QCOMPARE(result.pendingCents,777); QCOMPARE(result.previousCents,120);
    QCOMPARE(result.stationRevenue.value(2),240);
    QCOMPARE(result.modeRevenue.value("DIRECT"),120); QCOMPARE(result.modeRevenue.value("RESERVATION"),120);
    QCOMPARE(result.startPeriods[5],2); // UTC 15:00 is 23:00 in Shanghai.
    QCOMPARE(result.dailyOrders.size(),1);
    QCOMPARE(result.dailyOrders.value(QDate(2026,9,8)),2);
    const auto empty=analyzeRevenue({},QDate(2026,9,8),QDate(2026,9,10));
    QVERIFY(empty.valid); QCOMPARE(empty.dailyOrders.size(),3); QCOMPARE(empty.receivedCents,0);
    QVERIFY(!analyzeRevenue(QJsonArray{QJsonObject{}},QDate(2026,9,8),QDate(2026,9,8)).valid);
    QVERIFY(!analyzeRevenue({},QDate(2026,9,9),QDate(2026,9,8)).valid);
}

void AdminUiTests::analysisPagesAndSidebarRemainUsable()
{
    LoginFixture fixture;
    fixture.window.show(); fixture.username->setText("admin"); fixture.password->setText("123456"); fixture.submit->click();
    auto *clock = fixture.window.findChild<QLabel *>("operationsClock");
    QVERIFY(clock && clock->isVisible());
    const auto initialTime = clock->text();
    QTRY_VERIFY(clock->text() != initialTime);
    auto *navigation=fixture.window.findChild<QListWidget *>("navigation");
    QCOMPARE(navigation->count(),8);
    QCOMPARE(navigation->horizontalScrollBarPolicy(),Qt::ScrollBarAlwaysOff);
    auto *sidebar=fixture.window.findChild<QFrame *>("adminSidebar");
    QVERIFY(sidebar);
    for (const auto &size : {QSize(1080,700),QSize(1380,860),QSize(1500,950)}) {
        fixture.window.resize(size);
        QTest::qWait(50);
        QCOMPARE(fixture.window.size(),size);
        for (int page : {0,1,2,3,4,5,6,7}) {
            navigation->setCurrentRow(page); QTest::qWait(30);
            const auto image=screenImage(fixture.window,QStringLiteral("page-%1-%2").arg(page).arg(size.width()));
            QVERIFY(!navigation->horizontalScrollBar()->isVisible());
            if (!image.isNull()) {
                const auto position=navigation->mapTo(&fixture.window,QPoint(0,navigation->height()));
                const QRect gap(14,position.y()+1,sidebar->width()-28,6);
                const auto mismatch=unexpectedColor(image,gap,QColor("#102a56"));
                QVERIFY2(mismatch.isEmpty(),qPrintable(mismatch));
            }
        }
        for (const QString &name : {QStringLiteral("operationsAnalysisScroll"),QStringLiteral("revenueAnalysisScroll")}) {
            navigation->setCurrentRow(name.startsWith("operations") ? 0 : 1);
            auto *scroll=fixture.window.findChild<QScrollArea *>(name);
            QVERIFY(scroll); QVERIFY(scroll->isVisible());
            QVERIFY(scroll->widget()->width()<=scroll->viewport()->width());
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            screenImage(fixture.window,name+QString::number(size.width())+"-bottom");
            scroll->verticalScrollBar()->setValue(0);
        }
    }
    auto *statusChart = fixture.window.findChild<QScrollArea *>("operationsAnalysisScroll")->findChild<PileStatusChart *>();
    statusChart->statusClicked("RESERVED"); QCOMPARE(navigation->currentRow(),3);
    statusChart->statusClicked("CHARGING"); QCOMPARE(navigation->currentRow(),3);
    QCOMPARE(fixture.window.findChildren<RevenueChart *>().size(),3);
    QCOMPARE(fixture.window.findChildren<PileStatusChart *>().size(),4);
    QVERIFY(!fixture.window.findChild<QLabel *>("rangeReceivedRevenue")->text().contains(QStringLiteral("—")));
}

void AdminUiTests::invalidRevenueRangeClearsStaleAnalysisAndCanRecover()
{
    LoginFixture fixture;
    fixture.window.show();
    fixture.username->setText("admin"); fixture.password->setText("123456"); fixture.submit->click();
    fixture.window.findChild<QListWidget *>("navigation")->setCurrentRow(1);
    auto *page = fixture.window.findChild<QScrollArea *>("revenueAnalysisScroll");
    auto *range = page->findChild<QComboBox *>();
    QVERIFY(range);
    range->setCurrentIndex(range->findData(-1));
    screenImage(fixture.window, "revenue-custom-range");

    const auto dates = page->findChildren<QDateEdit *>();
    QCOMPARE(dates.size(), 2);
    dates[0]->setDate(QDate(2026,9,9));
    dates[1]->setDate(QDate(2026,9,8));
    auto *received = page->findChild<QLabel *>("rangeReceivedRevenue");
    QVERIFY(received->text() != QStringLiteral("—"));
    QTimer::singleShot(0, &fixture.window, [] {
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *message = qobject_cast<QMessageBox *>(widget)) message->accept();
    });
    page->findChild<QPushButton *>()->click(); // Apply the custom range.
    QCOMPARE(received->text(), QStringLiteral("—"));
    for (auto *chart : page->findChildren<RevenueChart *>()) QVERIFY(chart->points().isEmpty());
    for (auto *chart : page->findChildren<AnalysisBarChart *>()) QVERIFY(chart->bars().isEmpty());
    QVERIFY(page->findChild<QLabel *>("dashboardAnalysisSummary")->text().contains(QStringLiteral("暂不可用")));
    range->setCurrentIndex(range->findData(7));
    QVERIFY(received->text() != QStringLiteral("—"));
    for (auto *chart : page->findChildren<RevenueChart *>()) QCOMPARE(chart->points().size(), 7);
}

void AdminUiTests::passwordVisibilityAndEmptyLogin()
{
    LoginFixture fixture;
    fixture.window.show();
    fixture.submit->click();
    QCOMPARE(fixture.error->text(), QStringLiteral("请输入账号和密码"));
    auto *toggle = fixture.password->findChild<QToolButton *>("loginPasswordToggle");
    QVERIFY(toggle);
    fixture.password->setText("wrong-password");
    toggle->click();
    QCOMPARE(fixture.password->echoMode(), QLineEdit::Normal);
    QCOMPARE(toggle->accessibleName(), QStringLiteral("隐藏密码"));
    fixture.password->clear();
    QCOMPARE(fixture.password->echoMode(), QLineEdit::Password);
    QVERIFY(!toggle->isChecked());
    fixture.username->setText("admin");
    fixture.password->setText("wrong-password");
    toggle->click();
    fixture.submit->click();
    QCOMPARE(fixture.password->echoMode(), QLineEdit::Password);
    QVERIFY(!toggle->isChecked());
    auto *brand = fixture.page->findChild<QWidget *>("loginBrandPanel");
    auto *surface = fixture.page->findChild<QWidget *>("loginSurface");
    QVERIFY(brand && surface);
    QVERIFY(brand->geometry().right() < surface->geometry().left());
}

void AdminUiTests::loginFailurePreservesInputAndAllowsRetry()
{
    LoginFixture fixture;
    fixture.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
    QApplication::setActiveWindow(&fixture.window);
    const QRect passwordGeometry = fixture.password->geometry();
    const QRect buttonGeometry = fixture.submit->geometry();
    const QRect errorGeometry = fixture.error->geometry();

    QTest::mouseClick(fixture.username, Qt::LeftButton);
    QTest::keyClicks(fixture.username, "admin");
    QTest::keyClick(fixture.username, Qt::Key_Tab);
    QTRY_VERIFY(fixture.password->hasFocus());
    QTest::keyClicks(fixture.password, "wrong-password");
    QTest::mouseClick(fixture.submit, Qt::LeftButton);

    QVERIFY(fixture.page->isVisible());
    QCOMPARE(fixture.username->text(), QStringLiteral("admin"));
    QCOMPARE(fixture.password->text(), QStringLiteral("wrong-password"));
    QCOMPARE(fixture.error->text(), QStringLiteral("账号或密码错误，请重试"));
    QVERIFY(fixture.error->property("hasError").toBool());
    QCOMPARE(fixture.password->echoMode(), QLineEdit::Password);
    QVERIFY(fixture.password->hasFocus());
    QCOMPARE(fixture.password->selectedText(), fixture.password->text());
    QCOMPARE(fixture.password->geometry(), passwordGeometry);
    QCOMPARE(fixture.submit->geometry(), buttonGeometry);
    QCOMPARE(fixture.error->geometry(), errorGeometry);

    QTest::keyClicks(fixture.password, "123456");
    QTest::keyClick(fixture.password, Qt::Key_Return);
    QTRY_VERIFY(!fixture.page->isVisible());
    QVERIFY(fixture.error->text().isEmpty());
    QVERIFY(!fixture.error->property("hasError").toBool());
}

void AdminUiTests::loginSurfaceSurvivesPartialRepaints_data()
{
    QTest::addColumn<bool>("submitWrongPassword");
    QTest::newRow("password-focus") << false;
    QTest::newRow("wrong-password") << true;
}

void AdminUiTests::loginSurfaceSurvivesPartialRepaints()
{
    QFETCH(bool, submitWrongPassword);
    LoginFixture fixture;
    fixture.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
    QApplication::setActiveWindow(&fixture.window);
    if (screenImage(fixture.window, QStringLiteral("initial")).isNull()) {
        QSKIP("This platform cannot capture presented pixels; run under X11/Xvfb with QT_QPA_PLATFORM=xcb.");
    }

    QTest::mouseClick(fixture.username, Qt::LeftButton);
    QTest::keyClicks(fixture.username, "admin");
    QTest::mouseClick(fixture.password, Qt::LeftButton);
    QTRY_VERIFY(fixture.password->hasFocus());
    if (submitWrongPassword) {
        QTest::keyClicks(fixture.password, "wrong-password");
        QTest::mouseClick(fixture.submit, Qt::LeftButton);
        QCOMPARE(fixture.error->text(), QStringLiteral("账号或密码错误，请重试"));
    }
    const QString state = QString::fromLatin1(QTest::currentDataTag());
    const QColor errorColor(submitWrongPassword ? "#fff3f1" : "#ffffff");
    QImage image = screenImage(fixture.window, state);
    QString mismatch = unexpectedSurfacePixels(fixture, image, errorColor);
    QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));

    // Repeated focus/hover updates and resizing must not uncover either row.
    for (int i = 0; i < 3; ++i) {
        QTest::mouseClick(fixture.username, Qt::LeftButton);
        QTest::mouseMove(fixture.submit);
        QTest::mouseClick(fixture.password, Qt::LeftButton);
        fixture.window.resize(i % 2 == 0 ? QSize(1080, 700) : QSize(1500, 950));
        image = screenImage(fixture.window, QStringLiteral("%1-resize-%2").arg(state).arg(i));
        mismatch = unexpectedSurfacePixels(fixture, image, errorColor);
        QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));
    }
}

void AdminUiTests::systemAdminCanOpenAdminManagementPage()
{
    LoginFixture fixture;
    fixture.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
    fixture.username->setText(QStringLiteral("admin"));
    fixture.password->setText(QStringLiteral("123456"));
    QTest::mouseClick(fixture.submit, Qt::LeftButton);
    QTRY_VERIFY(!fixture.page->isVisible());

    auto *navigation = fixture.window.findChild<QListWidget *>(QStringLiteral("navigation"));
    auto *table = fixture.window.findChild<QTableWidget *>(QStringLiteral("adminsTable"));
    QVERIFY(navigation != nullptr);
    QVERIFY(table != nullptr);
    QCOMPARE(navigation->count(), 8);
    QVERIFY(!navigation->item(6)->isHidden());
    QVERIFY(!navigation->item(7)->isHidden());
    navigation->setCurrentRow(7);
    QTRY_VERIFY(table->isVisible());
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("admin"));
    if (!qEnvironmentVariableIsEmpty("CHARGING_UI_ARTIFACT_DIR")) screenImage(fixture.window, "admin-management");
    auto *tickets = fixture.window.findChild<QWidget *>(QStringLiteral("supportTicketsPage"));
    QVERIFY(tickets);
    navigation->setCurrentRow(6);
    QVERIFY(tickets->isVisible());
    QVERIFY(!table->isVisible());
    if (!qEnvironmentVariableIsEmpty("CHARGING_UI_ARTIFACT_DIR")) screenImage(fixture.window, "ticket-management");
    auto *refresh = fixture.window.findChild<QToolButton *>(QStringLiteral("adminPageRefresh"));
    QVERIFY(refresh);
    refresh->click();
    QVERIFY(tickets->isVisible());
    navigation->setCurrentRow(7);
    refresh->click();
    QVERIFY(table->isVisible());
    QCOMPARE(table->rowCount(), 1);
}

void AdminUiTests::adminDetailsAndAccountFormsFollowManagementFlow()
{
    LoginFixture fixture;
    fixture.window.show(); fixture.username->setText("admin"); fixture.password->setText("123456"); fixture.submit->click();
    fixture.window.findChild<QListWidget *>("navigation")->setCurrentRow(7);
    auto *table = fixture.window.findChild<QTableWidget *>("adminsTable");
    QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
    QTest::qWait(50);
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                     table->visualItemRect(table->item(0,1)).center());
    bool detailsOpened = false;
    QTimer::singleShot(30, &fixture.window, [&] {
        QDialog *dialog = nullptr;
        for (auto *candidate : fixture.window.findChildren<QDialog *>())
            if (candidate->isVisible()) dialog = candidate;
        QVERIFY(dialog);
        QTimer::singleShot(2000, dialog, &QDialog::reject);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("管理员详情"));
        QVERIFY(dialog->findChildren<QLineEdit *>().isEmpty());
        QString text;
        for (auto *label : dialog->findChildren<QLabel *>()) text += label->text();
        QVERIFY(text.contains("admin"));
        QVERIFY(text.contains(QStringLiteral("全部站点")));
        QVERIFY(!text.contains("123456"));
        detailsOpened = true;
        screenImage(*dialog, "admin-readonly-details");
        dialog->reject();
    });
    QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                      table->visualItemRect(table->item(0,1)).center());
    QVERIFY(detailsOpened);

    auto *create = fixture.window.findChild<QPushButton *>("createAdminButton");
    QVERIFY(create->property("primary").toBool());
    bool createChecked = false;
    QTimer::singleShot(30, &fixture.window, [&] {
        auto *dialog = fixture.window.findChild<QDialog *>("adminAccountDialog");
        QVERIFY(dialog);
        QTimer::singleShot(3000, dialog, &QDialog::reject);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("新增管理员"));
        dialog->findChild<QLineEdit *>("adminUsername")->setText("ui_navigation_admin");
        dialog->findChild<QLineEdit *>("adminDisplayName")->setText(QStringLiteral("测试管理员"));
        dialog->findChild<QLineEdit *>("adminInitialPassword")->setText("123456");
        dialog->findChild<QLineEdit *>("adminConfirmPassword")->setText("123456");
        auto *role = dialog->findChild<QComboBox *>("adminRoleInput");
        role->setCurrentIndex(role->findData("STATION_ADMIN"));
        auto *save = dialog->findChild<QDialogButtonBox *>("adminFormButtons")->button(QDialogButtonBox::Ok);
        QVERIFY(save->property("primary").toBool());
        // A station administrator cannot be created without a station scope.
        QTimer::singleShot(0, dialog, [] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) message->accept();
        });
        save->click();
        QVERIFY(dialog->isVisible());
        auto *scope = dialog->findChild<QListWidget *>("adminScopeList");
        QVERIFY(scope->isEnabled()); scope->item(0)->setSelected(true);
        screenImage(*dialog, "admin-create-form");
        createChecked = true;
        QTimer::singleShot(0, &fixture.window, [] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) message->accept();
        });
        save->click();
    });
    create->click();
    QVERIFY(createChecked);
    QCOMPARE(table->rowCount(), 2);
    int row = -1;
    for (int i = 0; i < table->rowCount(); ++i)
        if (table->item(i,1)->text() == "ui_navigation_admin") row = i;
    QVERIFY(row >= 0);
    const auto adminId = table->item(row,0)->data(Qt::UserRole).toLongLong();
    QCOMPARE(fixture.repository.findAdminById(adminId)->stationIds.size(), 1);
    bool menuChecked = false, editChecked = false;
    QTimer::singleShot(30, &fixture.window, [&] {
        auto *menu = fixture.window.findChild<QMenu *>("adminContextMenu");
        QVERIFY(menu);
        QTimer::singleShot(3000, menu, &QMenu::close);
        QCOMPARE(menu->actions().size(), 2);
        QCOMPARE(menu->actions()[0]->text(), QStringLiteral("查看详情"));
        QCOMPARE(menu->actions()[1]->text(), QStringLiteral("修改管理员信息"));
        menuChecked = true;
        QTimer::singleShot(30, &fixture.window, [&] {
            auto *dialog = fixture.window.findChild<QDialog *>("adminAccountDialog");
            QVERIFY(dialog);
            QTimer::singleShot(2000, dialog, &QDialog::reject);
            QCOMPARE(dialog->windowTitle(), QStringLiteral("编辑管理员"));
            dialog->findChild<QLineEdit *>("adminDisplayName")->setText(QStringLiteral("已修改的管理员"));
            dialog->findChild<QLineEdit *>("adminChangeReason")->setText(QStringLiteral("界面回归验证"));
            auto *save = dialog->findChild<QDialogButtonBox *>("adminFormButtons")->button(QDialogButtonBox::Save);
            QVERIFY(save->property("primary").toBool());
            screenImage(*dialog, "admin-edit-form");
            editChecked = true;
            save->click();
        });
        auto *edit = menu->actions()[1];
        menu->close(); edit->trigger();
    });
    table->customContextMenuRequested(table->visualItemRect(table->item(row,1)).center());
    QVERIFY(menuChecked && editChecked);
    QCOMPARE(fixture.repository.findAdminById(adminId)->displayName, QStringLiteral("已修改的管理员"));
}

void AdminUiTests::navigationRestoresViewsAndRefreshIsLocal()
{
    LoginFixture fixture;
    fixture.window.show(); fixture.username->setText("admin"); fixture.password->setText("123456"); fixture.submit->click();
    auto *nav = fixture.window.findChild<QListWidget *>("navigation");
    auto *back = fixture.window.findChild<QToolButton *>("adminPageBack");
    auto *forward = fixture.window.findChild<QToolButton *>("adminPageForward");
    auto *refresh = fixture.window.findChild<QToolButton *>("adminPageRefresh");
    QVERIFY(!back->isEnabled() && !forward->isEnabled());
    for (int page = 1; page < 8; ++page) nav->setCurrentRow(page);
    for (int page = 6; page >= 0; --page) { back->click(); QCOMPARE(nav->currentRow(), page); }
    QVERIFY(!back->isEnabled());
    for (int page = 1; page < 8; ++page) { forward->click(); QCOMPARE(nav->currentRow(), page); }
    QVERIFY(!forward->isEnabled());

    auto *admins = fixture.window.findChild<QTableWidget *>("adminsTable");
    auto *adminSearch = fixture.window.findChild<QLineEdit *>("adminSearch");
    adminSearch->setText("admin"); admins->selectRow(0);
    nav->setCurrentRow(6); back->click();
    QCOMPARE(adminSearch->text(), QStringLiteral("admin"));
    QCOMPARE(admins->currentRow(), 0);
    refresh->click();
    QVERIFY(adminSearch->text().isEmpty());
    QVERIFY(forward->isEnabled());
    forward->click(); QCOMPARE(nav->currentRow(), 6);
    back->click(); nav->setCurrentRow(0); QVERIFY(!forward->isEnabled());

    nav->setCurrentRow(1);
    auto *revenue = fixture.window.findChild<QScrollArea *>("revenueAnalysisScroll");
    auto *range = revenue->findChild<QComboBox *>();
    range->setCurrentIndex(range->findData(-1));
    const auto dates = revenue->findChildren<QDateEdit *>();
    dates[0]->setDate(QDate(2026,9,1)); dates[1]->setDate(QDate(2026,9,3));
    revenue->findChild<QPushButton *>()->click();
    nav->setCurrentRow(7); adminSearch->setText("admin");
    back->click();
    QCOMPARE(range->currentData().toInt(), -1);
    QCOMPARE(dates[0]->date(), QDate(2026,9,1));
    QCOMPARE(dates[1]->date(), QDate(2026,9,3));
    QCOMPARE(revenue->findChild<RevenueChart *>()->points().size(), 3);
    refresh->click();
    QCOMPARE(range->currentData().toInt(), 7);
    QVERIFY(!dates[0]->isVisible());
    QCOMPARE(adminSearch->text(), QStringLiteral("admin")); // Hidden page is untouched.
    QVERIFY(forward->isEnabled()); forward->click(); QCOMPARE(nav->currentRow(), 7);

    // Consecutive history entries can refer to different filters on one page.
    auto *status = fixture.window.findChild<QScrollArea *>("operationsAnalysisScroll")->findChild<PileStatusChart *>();
    status->statusClicked("CHARGING");
    QTableWidget *piles = nullptr;
    for (auto *table : fixture.window.findChildren<QTableWidget *>()) if (table->isVisible()) piles = table;
    QVERIFY(piles); QCOMPARE(piles->rowCount(),1);
    const auto chargingId = piles->item(0,0)->data(Qt::UserRole).toLongLong();
    status->statusClicked("FAULT");
    QCOMPARE(piles->rowCount(),1);
    const auto faultId = piles->item(0,0)->data(Qt::UserRole).toLongLong();
    QVERIFY(chargingId != faultId);
    back->click(); QCOMPARE(nav->currentRow(),3);
    QCOMPARE(piles->item(0,0)->data(Qt::UserRole).toLongLong(), chargingId);
    forward->click(); QCOMPARE(piles->item(0,0)->data(Qt::UserRole).toLongLong(), faultId);
    refresh->click(); QCOMPARE(piles->rowCount(),6);

    nav->setCurrentRow(2);
    auto *stations = fixture.window.findChild<QTreeWidget *>();
    QVERIFY(stations); stations->topLevelItem(0)->setExpanded(true);
    stations->setCurrentItem(stations->topLevelItem(0));
    nav->setCurrentRow(7);
    // Simulate another retained view having a different expansion state.
    stations->topLevelItem(1)->setExpanded(true);
    back->click();
    QVERIFY(stations->topLevelItem(0)->isExpanded());
    QVERIFY(!stations->topLevelItem(1)->isExpanded());
    QCOMPARE(stations->currentItem(), stations->topLevelItem(0));
    refresh->click();
    for (int i=0;i<stations->topLevelItemCount();++i) QVERIFY(!stations->topLevelItem(i)->isExpanded());

    nav->setCurrentRow(7);
    for (auto *button : fixture.window.findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("退出登录")) { button->click(); break; }
    fixture.username->setText("admin"); fixture.password->setText("123456"); fixture.submit->click();
    QCOMPARE(nav->currentRow(),0);
    QVERIFY(!back->isEnabled() && !forward->isEnabled());
}

void AdminUiTests::chartAnimationsFollowNavigationAndRefreshRules()
{
    LoginFixture fixture;
    fixture.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
    fixture.username->setText("admin"); fixture.password->setText("123456"); fixture.submit->click();
    auto *nav = fixture.window.findChild<QListWidget *>("navigation");
    auto *back = fixture.window.findChild<QToolButton *>("adminPageBack");
    auto *forward = fixture.window.findChild<QToolButton *>("adminPageForward");
    auto *refresh = fixture.window.findChild<QToolButton *>("adminPageRefresh");
    auto *ops = fixture.window.findChild<QScrollArea *>("operationsAnalysisScroll");
    auto *revenue = fixture.window.findChild<QScrollArea *>("revenueAnalysisScroll");
    const auto animations = [](QWidget *page) { return page->findChildren<QVariantAnimation *>("chartIntroAnimation"); };
    const auto metricAnimations = [](QWidget *page) { return page->findChildren<QVariantAnimation *>("metricIntroAnimation"); };
    QCOMPARE(metricAnimations(ops).size(), 4);
    QCOMPARE(metricAnimations(revenue).size(), 8);
    for (auto *intro : metricAnimations(ops)) QCOMPARE(intro->state(), QAbstractAnimation::Running);
    auto *opsIntro = animations(ops).first();
    QCOMPARE(animations(ops).size(), 4);
    QCOMPARE(animations(revenue).size(), 6);
    QCOMPARE(opsIntro->state(), QAbstractAnimation::Running);
    QTRY_COMPARE(opsIntro->state(), QAbstractAnimation::Stopped);
    nav->setCurrentRow(7); back->click();
    QCOMPARE(nav->currentRow(),0);
    QCOMPARE(opsIntro->state(), QAbstractAnimation::Stopped);
    for (auto *intro : metricAnimations(ops)) QCOMPARE(intro->state(), QAbstractAnimation::Stopped);
    forward->click(); nav->setCurrentRow(0);
    QCOMPARE(opsIntro->state(), QAbstractAnimation::Stopped);
    for (auto *intro : metricAnimations(ops)) QCOMPARE(intro->state(), QAbstractAnimation::Stopped);
    refresh->click();
    QCOMPARE(opsIntro->state(), QAbstractAnimation::Running);
    auto *refreshAnimation = fixture.window.findChild<QVariantAnimation *>("adminPageRefreshAnimation");
    QVERIFY(refreshAnimation);
    QCOMPARE(refreshAnimation->state(), QAbstractAnimation::Running);
    QTRY_COMPARE(refreshAnimation->state(), QAbstractAnimation::Stopped);

    nav->setCurrentRow(1);
    for (auto *intro : animations(revenue)) QCOMPARE(intro->state(), QAbstractAnimation::Running);
    auto *line = animations(revenue).first();
    QTRY_COMPARE(line->state(), QAbstractAnimation::Stopped);
    nav->setCurrentRow(7); nav->setCurrentRow(1);
    for (auto *intro : animations(revenue)) QCOMPARE(intro->state(), QAbstractAnimation::Stopped);
    for (auto *intro : metricAnimations(revenue)) QCOMPARE(intro->state(), QAbstractAnimation::Stopped);
    auto *range = revenue->findChild<QComboBox *>();
    auto *period = fixture.window.findChild<QLabel *>("revenueDateRange");
    QVERIFY(period && period->isVisible());
    const QDate today = QDateTime::currentDateTimeUtc().toTimeZone(QTimeZone("Asia/Shanghai")).date();
    for (int days : {60,90}) {
        range->setCurrentIndex(range->findData(days));
        for (auto *chart : revenue->findChildren<RevenueChart *>()) QCOMPARE(chart->points().size(),days);
        QCOMPARE(period->text(),QStringLiteral("%1 — %2").arg(today.addDays(1-days).toString("yyyy.MM.dd"),today.toString("yyyy.MM.dd")));
        QCOMPARE(line->state(), QAbstractAnimation::Running);
        for (auto *intro : metricAnimations(revenue)) QCOMPARE(intro->state(), QAbstractAnimation::Running);
    }
    range->setCurrentIndex(range->findData(30));
    QCOMPARE(line->state(), QAbstractAnimation::Running);
    nav->setCurrentRow(7); // Leaving interrupts an unfinished animation.
    for (auto *intro : animations(revenue)) QCOMPARE(intro->state(), QAbstractAnimation::Stopped);
    for (auto *intro : metricAnimations(revenue)) QCOMPARE(intro->state(), QAbstractAnimation::Stopped);
    back->click();
    QCOMPARE(range->currentData().toInt(),30);
    for (auto *intro : animations(revenue)) QCOMPARE(intro->state(), QAbstractAnimation::Stopped);
    for (auto *intro : metricAnimations(revenue)) QCOMPARE(intro->state(), QAbstractAnimation::Stopped);
    refresh->click();
    QCOMPARE(range->currentData().toInt(),7);
    QCOMPARE(line->state(), QAbstractAnimation::Running);
    refresh->click(); // Restart, without queuing multiple rotations.
    QVERIFY(refreshAnimation->currentTime() < refreshAnimation->duration());
    QTRY_COMPARE(line->state(), QAbstractAnimation::Stopped);
    range->setCurrentIndex(range->findData(-1));
    QCOMPARE(line->state(), QAbstractAnimation::Stopped);
    revenue->findChild<QPushButton *>()->click();
    QCOMPARE(line->state(), QAbstractAnimation::Running);
    QTRY_COMPARE(line->state(), QAbstractAnimation::Stopped);
    QCOMPARE(line->currentValue().toReal(), 1.0);
    nav->setCurrentRow(6);
    QVERIFY(!fixture.window.findChild<QPushButton *>("adminTicketRefresh"));
    refresh->click();
    auto *feedback = fixture.window.findChild<QWidget *>("pageRefreshFeedback");
    QVERIFY(feedback->isVisible());
    QCOMPARE(refreshAnimation->state(), QAbstractAnimation::Running);
    QVERIFY(feedback->testAttribute(Qt::WA_TransparentForMouseEvents));
    QTRY_VERIFY(!feedback->isVisible());
    refresh->click(); back->click();
    QVERIFY(!feedback->isVisible());
}

void AdminUiTests::ordersLocateAssociatedAssetsAndRestoreSource()
{
    LoginFixture fixture;
    fixture.window.resize(1380,860); fixture.window.show();
    fixture.username->setText("admin"); fixture.password->setText("123456"); fixture.submit->click();
    auto *nav = fixture.window.findChild<QListWidget *>("navigation");
    auto *back = fixture.window.findChild<QToolButton *>("adminPageBack");
    auto *forward = fixture.window.findChild<QToolButton *>("adminPageForward");
    auto *stations = fixture.window.findChild<QTreeWidget *>();
    auto *piles = fixture.window.findChild<QTableWidget *>("pilesTable");
    auto *orders = fixture.window.findChild<QTableWidget *>("ordersTable");
    nav->setCurrentRow(2);
    stations->parentWidget()->findChild<QLineEdit *>()->setText("missing station");
    nav->setCurrentRow(3);
    piles->parentWidget()->findChild<QLineEdit *>()->setText("missing pile");
    nav->setCurrentRow(5);
    auto *search = orders->parentWidget()->findChild<QLineEdit *>();
    search->setText("PILE-A-01");
    QVERIFY(orders->rowCount()>0);
    const int count = orders->rowCount();
    const int row = count-1; // Right-click a row other than the current selection.
    const auto orderId = orders->item(row,0)->data(Qt::UserRole).toLongLong();
    const auto stationId = orders->item(row,3)->data(Qt::UserRole).toLongLong();
    const auto pileId = orders->item(row,4)->data(Qt::UserRole).toLongLong();
    QVERIFY(stationId>0 && pileId>0);
    const auto activate = [&](const QString &label) {
        bool found = false;
        QTimer::singleShot(30,&fixture.window,[&] {
            auto *menu = fixture.window.findChild<QMenu *>("orderContextMenu");
            QVERIFY(menu);
            QTimer::singleShot(2000,menu,&QMenu::close);
            for (auto *action : menu->actions()) {
                if (action->text()!=label) continue;
                found = action->isEnabled();
                menu->close(); action->trigger(); return;
            }
        });
        orders->scrollToItem(orders->item(row,1));
        orders->customContextMenuRequested(orders->visualItemRect(orders->item(row,1)).center());
        QVERIFY(found);
    };
    activate(QStringLiteral("管理该充电站"));
    QCOMPARE(nav->currentRow(),2);
    auto *selected = stations->currentItem();
    QVERIFY(selected && selected->parent() && selected->parent()->isExpanded());
    QCOMPARE(selected->parent()->data(0,Qt::UserRole).toLongLong(),stationId);
    QCOMPARE(selected->data(0,Qt::UserRole+1).toLongLong(),pileId);
    back->click(); QCOMPARE(nav->currentRow(),5);
    QCOMPARE(search->text(),QStringLiteral("PILE-A-01"));
    QCOMPARE(orders->rowCount(),count);
    QCOMPARE(orders->item(orders->currentRow(),0)->data(Qt::UserRole).toLongLong(),orderId);
    activate(QStringLiteral("管理该充电桩"));
    QCOMPARE(nav->currentRow(),3);
    QVERIFY(piles->currentRow()>=0);
    QCOMPARE(piles->item(piles->currentRow(),0)->data(Qt::UserRole).toLongLong(),pileId);
    back->click(); QCOMPARE(nav->currentRow(),5);
    QCOMPARE(search->text(),QStringLiteral("PILE-A-01"));
    QCOMPARE(orders->item(orders->currentRow(),0)->data(Qt::UserRole).toLongLong(),orderId);
    forward->click(); QCOMPARE(nav->currentRow(),3);
    QCOMPARE(piles->item(piles->currentRow(),0)->data(Qt::UserRole).toLongLong(),pileId);
}

void AdminUiTests::repairTicketsLocatePilesAndExpandedStationsWithHistory()
{
    LoginFixture fixture;
    const auto token = fixture.service.loginUser({{"phone", "13800000001"}}).data.value("token").toString();
    qint64 repairId = 0;
    for (int i=0; i<13; ++i) {
        QJsonObject draft{{"submissionId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"title", QStringLiteral("工单 %1 PILE-A-01").arg(i)},
            {"summary", QStringLiteral("屏幕无法显示，请检查电桩")}, {"sourceModel", ""}};
        if (i==0) draft.insert("repair", QJsonObject{{"pileCode", "PILE-A-01"}, {"faultType", "screen"}});
        const auto result = fixture.service.createSupportTicket(token, draft);
        QVERIFY(result.ok());
        if (i==0) repairId = result.data.value("ticket").toObject().value("ticketId").toInteger();
    }
    fixture.window.show(); fixture.username->setText("admin"); fixture.password->setText("123456"); fixture.submit->click();
    auto *nav = fixture.window.findChild<QListWidget *>("navigation");
    auto *back = fixture.window.findChild<QToolButton *>("adminPageBack");
    auto *forward = fixture.window.findChild<QToolButton *>("adminPageForward");
    auto *stations = fixture.window.findChild<QTreeWidget *>();
    auto *piles = fixture.window.findChild<QTableWidget *>("pilesTable");
    QVERIFY(stations && piles);
    // Existing searches must not hide either destination.
    nav->setCurrentRow(2);
    stations->parentWidget()->findChild<QLineEdit *>()->setText(QStringLiteral("和平"));
    QCOMPARE(stations->topLevelItemCount(),1);
    nav->setCurrentRow(3);
    piles->parentWidget()->findChild<QLineEdit *>()->setText("PILE-C");
    nav->setCurrentRow(6);
    auto *tickets = fixture.window.findChild<SupportTicketsPage *>("supportTicketsPage");
    auto *list = tickets->findChild<QListWidget *>("adminTicketList");
    auto *locate = tickets->findChild<QPushButton *>("adminTicketLocatePile");
    QVERIFY(locate);
    QCOMPARE(list->count(),10);
    // A code mentioned in an ordinary ticket's title is not a device association.
    QVERIFY(!locate->isVisible());
    tickets->findChild<QPushButton *>("adminTicketMore")->click();
    QCOMPARE(list->count(),13);
    list->setCurrentRow(12);
    QCOMPARE(tickets->selectedTicketId(), repairId);
    QVERIFY(locate->isVisible() && locate->isEnabled());
    screenImage(fixture.window,"repair-ticket-locate-action");
    locate->click();
    QCOMPARE(nav->currentRow(),3);
    QVERIFY(piles->currentRow() >= 0);
    QCOMPARE(piles->item(piles->currentRow(),1)->text(), QStringLiteral("PILE-A-01"));
    const qint64 pileId = piles->item(piles->currentRow(),0)->data(Qt::UserRole).toLongLong();
    QVERIFY(piles->item(piles->currentRow(),1)->isSelected());
    screenImage(fixture.window,"repair-selected-pile");
    bool actionFound = false;
    QTimer::singleShot(30, &fixture.window, [&] {
        auto *menu = fixture.window.findChild<QMenu *>("pileContextMenu");
        QVERIFY(menu);
        QTimer::singleShot(2000, menu, &QMenu::close);
        for (auto *action : menu->actions()) {
            if (action->text() == QStringLiteral("定位到充电站")) {
                actionFound = true;
                menu->close(); action->trigger();
                return;
            }
        }
    });
    piles->customContextMenuRequested(piles->visualItemRect(piles->item(piles->currentRow(),1)).center());
    QVERIFY(actionFound);
    QCOMPARE(nav->currentRow(),2);
    auto *selected = stations->currentItem();
    QVERIFY(selected && selected->parent());
    QVERIFY(selected->parent()->isExpanded());
    QVERIFY(selected->isSelected());
    QCOMPARE(selected->data(0,Qt::UserRole+1).toLongLong(),pileId);
    QCOMPARE(selected->text(0),QStringLiteral("PILE-A-01"));
    QVERIFY(stations->parentWidget()->findChild<QLineEdit *>()->text().isEmpty());
    screenImage(fixture.window,"repair-selected-station-child");
    back->click(); QCOMPARE(nav->currentRow(),3);
    QCOMPARE(piles->item(piles->currentRow(),0)->data(Qt::UserRole).toLongLong(),pileId);
    back->click(); QCOMPARE(nav->currentRow(),6);
    QCOMPARE(tickets->selectedTicketId(),repairId);
    QCOMPARE(list->count(),13); // Restore the selected ticket across pagination.
    QVERIFY(locate->isVisible());
    forward->click(); QCOMPARE(nav->currentRow(),3);
    forward->click(); QCOMPARE(nav->currentRow(),2);
    selected = stations->currentItem();
    QVERIFY(selected && selected->parent() && selected->parent()->isExpanded());
    QCOMPARE(selected->data(0,Qt::UserRole+1).toLongLong(),pileId);
    QVERIFY(selected->isSelected());
}

void AdminUiTests::filtersIncludeNewRegionsAndStayWithinScreen()
{
    LoginFixture fixture;
    fixture.window.resize(1080,700); fixture.window.show();
    fixture.username->setText("admin"); fixture.password->setText("123456"); fixture.submit->click();
    QString targetName;
    for (int i=0; i<40; ++i) {
        const QString name = QStringLiteral("新区域服务区%1长名称充电站用于验证筛选文字完整提示").arg(i,2,10,QChar('0'));
        QJsonArray piles;
        if (i==39) { targetName = name; piles.append(QJsonObject{{"pileCode","PILE-FILTER-END"},{"pileType","FAST"},{"ratedPowerKw",60}}); }
        const auto result = fixture.facade.createStation({{"name",name},{"region",QStringLiteral("新增区域")},
            {"address",QStringLiteral("测试路1号")},{"longitude",123.4},{"latitude",41.8},
            {"priceCentsPerKwh",120},{"piles",piles}});
        QVERIFY(result.ok());
    }
    auto *nav = fixture.window.findChild<QListWidget *>("navigation");
    nav->setCurrentRow(2);
    auto *tree = fixture.window.findChild<QTreeWidget *>();
    auto *search = tree->parentWidget()->findChild<QLineEdit *>();
    search->setText(QStringLiteral("和平"));
    QCOMPARE(tree->topLevelItemCount(),1);
    QPushButton *regionButton = nullptr;
    for (auto *button : tree->parentWidget()->findChildren<QPushButton *>())
        if (button->property("filterTitle").toString() == QStringLiteral("区域")) regionButton = button;
    QVERIFY(regionButton);
    bool regionFound = false;
    QTimer::singleShot(30, &fixture.window, [&] {
        auto *dialog = fixture.window.findChild<QDialog *>("managementFilterPopup");
        QVERIFY(dialog); QTimer::singleShot(2000, dialog, &QDialog::reject);
        auto *scroll = dialog->findChild<QScrollArea *>("filterOptionsScroll");
        int matches = 0;
        for (auto *box : scroll->findChildren<QCheckBox *>()) {
            if (box->accessibleName() == QStringLiteral("新增区域")) { ++matches; box->setChecked(true); }
        }
        QCOMPARE(matches,1); // Deduplicated, and independent of the current search result.
        regionFound = true;
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    regionButton->click(); QVERIFY(regionFound);
    QCOMPARE(tree->topLevelItemCount(),0); // Region and keyword combine with AND.
    search->clear(); QCOMPARE(tree->topLevelItemCount(),40);
    const auto extra = fixture.facade.createStation({{"name",QStringLiteral("后续新增站")},{"region",QStringLiteral("后续区域")},
        {"address",QStringLiteral("测试路2号")},{"longitude",123.4},{"latitude",41.8},
        {"priceCentsPerKwh",120},{"piles",QJsonArray{}}});
    QVERIFY(extra.ok());
    fixture.window.findChild<QToolButton *>("adminPageRefresh")->click();
    bool updated = false;
    QTimer::singleShot(30, &fixture.window, [&] {
        auto *dialog = fixture.window.findChild<QDialog *>("managementFilterPopup");
        QVERIFY(dialog); QTimer::singleShot(2000, dialog, &QDialog::reject);
        for (auto *box : dialog->findChild<QScrollArea *>()->findChildren<QCheckBox *>())
            if (box->accessibleName() == QStringLiteral("后续区域")) updated = true;
        dialog->reject();
    });
    regionButton->click(); QVERIFY(updated);
    nav->setCurrentRow(3);
    auto *table = fixture.window.findChild<QTableWidget *>("pilesTable");
    QPushButton *stationButton = nullptr;
    for (auto *button : table->parentWidget()->findChildren<QPushButton *>())
        if (button->property("filterTitle").toString() == QStringLiteral("站点")) stationButton = button;
    QVERIFY(stationButton);
    bool scrollChecked = false;
    QTimer::singleShot(30, &fixture.window, [&] {
        auto *dialog = fixture.window.findChild<QDialog *>("managementFilterPopup");
        QVERIFY(dialog); QTimer::singleShot(2000, dialog, &QDialog::reject);
        auto *scroll = dialog->findChild<QScrollArea *>("filterOptionsScroll");
        QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QVERIFY(dialog->screen()->availableGeometry().contains(dialog->geometry()));
        QVERIFY(dialog->height() <= 540);
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        auto *all = dialog->findChild<QCheckBox *>("filterSelectAll");
        auto *confirm = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        QVERIFY(dialog->rect().contains(QRect(all->mapTo(dialog,QPoint()),all->size())));
        QVERIFY(dialog->rect().contains(QRect(confirm->mapTo(dialog,QPoint()),confirm->size())));
        bool targetFound = false;
        for (auto *box : scroll->findChildren<QCheckBox *>()) {
            if (box->accessibleName() == targetName) {
                targetFound = true; QCOMPARE(box->toolTip(),targetName);
                box->setChecked(true); scroll->ensureWidgetVisible(box);
            }
        }
        QVERIFY(targetFound);
        screenImage(*dialog,"scrollable-station-filter");
        scrollChecked = true;
        confirm->click();
    });
    stationButton->click(); QVERIFY(scrollChecked);
    QCOMPARE(table->rowCount(),1);
    QCOMPARE(table->item(0,1)->text(),QStringLiteral("PILE-FILTER-END"));
}

void AdminUiTests::userAdminOnlySeesAuthorizedPages()
{
    LoginFixture fixture;
    const auto systemLogin = fixture.service.loginAdmin(QStringLiteral("admin"),
                                                        QStringLiteral("123456"));
    QVERIFY(systemLogin.ok());
    const auto created = fixture.service.createAdminAccount(1, {
        {QStringLiteral("username"), QStringLiteral("ui_user_admin")},
        {QStringLiteral("initialPassword"), QStringLiteral("Initial-123")},
        {QStringLiteral("displayName"), QStringLiteral("界面用户管理员")},
        {QStringLiteral("role"), QStringLiteral("USER_ADMIN")},
        {QStringLiteral("stationIds"), QJsonArray{}},
    });
    QVERIFY(created.ok());
    const qint64 adminId = created.data.value(QStringLiteral("admin")).toObject()
                               .value(QStringLiteral("adminId")).toInteger();
    QVERIFY(fixture.service.changeAdminPassword(adminId,
                                                QStringLiteral("Initial-123"),
                                                QStringLiteral("Changed-456")).ok());

    fixture.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
    fixture.username->setText(QStringLiteral("ui_user_admin"));
    fixture.password->setText(QStringLiteral("Changed-456"));
    QTest::mouseClick(fixture.submit, Qt::LeftButton);
    QTRY_VERIFY(!fixture.page->isVisible());

    auto *navigation = fixture.window.findChild<QListWidget *>(QStringLiteral("navigation"));
    QVERIFY(navigation != nullptr);
    QVERIFY(navigation->item(0)->isHidden());
    QVERIFY(!navigation->item(1)->isHidden());
    QVERIFY(navigation->item(2)->isHidden());
    QVERIFY(navigation->item(3)->isHidden());
    QVERIFY(!navigation->item(4)->isHidden());
    QVERIFY(!navigation->item(5)->isHidden());
    QVERIFY(navigation->item(6)->isHidden());
    QVERIFY(navigation->item(7)->isHidden());
    navigation->setCurrentRow(5);
    auto *orders = fixture.window.findChild<QTableWidget *>("ordersTable");
    QVERIFY(orders->rowCount()>0);
    bool inspected = false;
    QTimer::singleShot(30,&fixture.window,[&] {
        auto *menu = fixture.window.findChild<QMenu *>("orderContextMenu");
        QVERIFY(menu);
        menu->close(); inspected = true;
        for (auto *action : menu->actions()) {
            QVERIFY(action->text()!=QStringLiteral("管理该充电站"));
            QVERIFY(action->text()!=QStringLiteral("管理该充电桩"));
        }
    });
    orders->customContextMenuRequested(orders->visualItemRect(orders->item(0,1)).center());
    QVERIFY(inspected);
    navigation->setCurrentRow(6); // A hidden item cannot be entered programmatically either.
    QVERIFY(!fixture.window.findChild<QWidget *>(QStringLiteral("supportTicketsPage"))->isVisible());
}

void AdminUiTests::revokedTicketAccessClearsCachedPage()
{
    LoginFixture fixture;
    const auto token = fixture.service.loginUser({{"phone", "13800000001"}}).data.value("token").toString();
    const auto ticket = fixture.service.createSupportTicket(token, {
        {"submissionId", "98d804af-47d8-4248-80d3-f7eaeec109ed"}, {"title", "private feedback"},
        {"summary", "confirmed summary"}, {"sourceModel", ""}});
    QVERIFY(ticket.ok());
    fixture.window.show();
    fixture.username->setText("admin"); fixture.password->setText("123456");
    fixture.submit->click();
    auto *navigation = fixture.window.findChild<QListWidget *>("navigation");
    navigation->setCurrentRow(6);
    auto *list = fixture.window.findChild<QListWidget *>("adminTicketList");
    auto *summary = fixture.window.findChild<QPlainTextEdit *>("adminTicketSummary");
    auto *reply = fixture.window.findChild<QPlainTextEdit *>("adminTicketReply");
    QCOMPARE(list->count(), 1);
    QVERIFY(!summary->toPlainText().isEmpty());
    auto admin = fixture.repository.findAdminById(1).value();
    admin.status = "DISABLED";
    QVERIFY(fixture.repository.updateAdmin(admin));
    reply->setPlainText("must not be saved");
    fixture.window.findChild<QPushButton *>("adminTicketSave")->click();
    QCOMPARE(list->count(), 0);
    QVERIFY(summary->toPlainText().isEmpty() && reply->toPlainText().isEmpty());
    const auto id = ticket.data.value("ticket").toObject().value("ticketId").toInteger();
    QVERIFY(fixture.repository.findSupportTicket(id)->reply.isEmpty());
}

QTEST_MAIN(AdminUiTests)
#include "admin_ui_tests.moc"
