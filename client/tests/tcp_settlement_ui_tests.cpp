#include "api/tcp_charging_api.h"
#include "charging/protocol/envelope.h"
#include "charging/protocol/frame_codec.h"
#include "charging/protocol/protocol_constants.h"
#include "ui/main_window.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTcpServer>
#include <QTimer>
#include <QtTest>

#include <algorithm>
#include <memory>

using namespace charging;

namespace {

QJsonObject fixtureData(const QString &fileName)
{
    QFile file(QString::fromUtf8(SETTLEMENT_FIXTURE_DIR) + QLatin1Char('/') + fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object()
        .value(QStringLiteral("data")).toObject();
}

} // namespace

class TcpSettlementUiTests final : public QObject {
    Q_OBJECT

private slots:
    void settlementNoticePreservesSession_data();
    void settlementNoticePreservesSession();
};

void TcpSettlementUiTests::settlementNoticePreservesSession_data()
{
    QTest::addColumn<bool>("fromOrders");
    QTest::addColumn<bool>("paid");
    QTest::newRow("home-paid") << false << true;
    QTest::newRow("orders-paid") << true << true;
    QTest::newRow("home-pending-payment") << false << false;
    QTest::newRow("orders-pending-payment") << true << false;
}

void TcpSettlementUiTests::settlementNoticePreservesSession()
{
    QFETCH(bool, fromOrders);
    QFETCH(bool, paid);
    const QJsonObject login = fixtureData(QStringLiteral("auth-user-login.response.json"));
    const QJsonObject stations = fixtureData(QStringLiteral("station-list.response.json"));
    const QJsonObject charging = fixtureData(QStringLiteral("order-progress-1.response.json"));
    const QJsonObject settlement = fixtureData(paid
        ? QStringLiteral("order-stop.response.json")
        : QStringLiteral("order-stop.pending-payment.response.json"));
    QVERIFY(!login.isEmpty());
    QVERIFY(!stations.isEmpty());
    QVERIFY(!charging.isEmpty());
    QVERIFY(!settlement.isEmpty());

    // Real sockets and the production adapter/UI; only server data comes from
    // V1 fixtures. No SQL, external network or business-side effects are involved.
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QList<protocol::RequestEnvelope> requests;
    QString serverError;
    bool stopped = false;
    int connectionCount = 0;
    connect(&server, &QTcpServer::newConnection, &server, [&]() {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            ++connectionCount;
            auto decoder = std::make_shared<protocol::FrameDecoder>();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, &server, [&, socket, decoder]() {
                const auto decoded = decoder->append(socket->readAll());
                if (!decoded.ok()) {
                    serverError = decoded.errorMessage;
                    return;
                }
                for (const QJsonObject &json : decoded.messages) {
                    protocol::RequestEnvelope request;
                    if (!protocol::RequestEnvelope::fromJson(json, &request, &serverError)) {
                        return;
                    }
                    requests.append(request);
                    protocol::ResponseEnvelope response;
                    response.type = request.type;
                    response.requestId = request.requestId;
                    response.message = QStringLiteral("OK");
                    if (request.type == QString::fromLatin1(protocol::MessageType::AuthUserLogin)) {
                        response.data = login;
                    } else if (request.token != login.value(QStringLiteral("token")).toString()) {
                        response.code = protocol::ErrorCode::InvalidSession;
                    } else if (request.type == QString::fromLatin1(protocol::MessageType::StationList)) {
                        response.data = stations;
                    } else if (request.type == QString::fromLatin1(protocol::MessageType::OrderCurrent)) {
                        response.data = {{QStringLiteral("order"), stopped && paid
                            ? QJsonValue(QJsonValue::Null)
                            : (stopped ? settlement : charging).value(QStringLiteral("order"))}};
                    } else if (request.type == QString::fromLatin1(protocol::MessageType::OrderList)) {
                        response.data = {{QStringLiteral("items"), QJsonArray{
                            (stopped ? settlement : charging).value(QStringLiteral("order"))}}};
                    } else if (request.type == QString::fromLatin1(protocol::MessageType::OrderStop)) {
                        stopped = true;
                        response.data = settlement;
                    } else if (request.type == QString::fromLatin1(protocol::MessageType::UserProfileGet)) {
                        auto user = login.value(QStringLiteral("user")).toObject();
                        if (stopped) {
                            user.insert(QStringLiteral("balanceCents"),
                                        settlement.value(QStringLiteral("balanceCents")));
                        }
                        response.data = {{QStringLiteral("user"), user}};
                    } else {
                        response.code = protocol::ErrorCode::InvalidRequest;
                    }
                    const QPointer<QTcpSocket> guard(socket);
                    const QByteArray frame = protocol::encodeFrame(response.toJson());
                    QTimer::singleShot(10, &server, [guard, frame]() {
                        if (guard != nullptr) {
                            guard->write(frame);
                        }
                    });
                }
            });
        }
    });

    constexpr int timeoutMs = 250;
    client::TcpChargingApi api(QStringLiteral("127.0.0.1"), server.serverPort(), timeoutMs);
    client::MainWindow window(api);
    window.show();
    QSignalSpy stationSpy(&api, &client::IChargingApi::stationListCompleted);
    QSignalSpy currentSpy(&api, &client::IChargingApi::currentOrderCompleted);
    QSignalSpy stopSpy(&api, &client::IChargingApi::chargingStopCompleted);
    QSignalSpy profileSpy(&api, &client::IChargingApi::profileCompleted);
    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneInput"));
    auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    QVERIFY(phone != nullptr);
    QVERIFY(loginButton != nullptr);
    phone->setText(QStringLiteral("13800000001"));
    window.findChild<QLineEdit *>(QStringLiteral("verificationCodeInput"))->setText(QStringLiteral("123456"));
    QTest::mouseClick(loginButton, Qt::LeftButton);
    auto *currentCard = window.findChild<QWidget *>(QStringLiteral("currentOrderCard"));
    auto *navigation = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    auto *refresh = window.findChild<QPushButton *>(QStringLiteral("stationRefreshButton"));
    QVERIFY(currentCard != nullptr);
    QVERIFY(navigation != nullptr);
    QVERIFY(refresh != nullptr);
    QTRY_VERIFY(currentCard->isVisible());
    QTRY_VERIFY(refresh->isEnabled());

    const auto loginRequest = std::find_if(requests.cbegin(), requests.cend(), [](const auto &request) {
        return request.type == QString::fromLatin1(protocol::MessageType::AuthUserLogin);
    });
    QVERIFY(loginRequest != requests.cend());
    QCOMPARE(loginRequest->data, QJsonObject({{QStringLiteral("phone"), QStringLiteral("13800000001")}}));

    if (fromOrders) {
        navigation->setCurrentIndex(1);
        QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("orderCard_1001")) != nullptr);
        QTest::mouseClick(window.findChild<QWidget *>(QStringLiteral("orderCard_1001")),
                          Qt::LeftButton, Qt::NoModifier, QPoint(12, 12));
    }
    auto *stopButton = window.findChild<QPushButton *>(fromOrders
        ? QStringLiteral("orderDetailStopButton") : QStringLiteral("chargingStopButton"));
    if (!fromOrders) window.findChild<QPushButton *>(QStringLiteral("currentOrderToggle"))->click();
    QVERIFY(stopButton != nullptr);
    QTRY_VERIFY(stopButton->isVisible() && stopButton->isEnabled());

    const auto stationCount = stationSpy.count();
    const auto currentCount = currentSpy.count();
    bool noticeOpened = false;
    bool noticeClosed = false;
    bool refreshedWhileOpen = false;
    QTimer dialogPoller;
    connect(&dialogPoller, &QTimer::timeout, &window, [&]() {
        for (auto *dialog : window.findChildren<QMessageBox *>()) {
            if (!dialog->isVisible()) {
                continue;
            }
            if (dialog->windowTitle() == QStringLiteral("确认结束充电")) {
                dialog->done(QMessageBox::Yes);
            } else if (!noticeOpened
                       && dialog->objectName() == (paid ? QStringLiteral("chargingStoppedDialog")
                                                       : QStringLiteral("chargingDebtDialog"))) {
                noticeOpened = true;
                // Do not auto-dismiss immediately: that hid the original TCP bug.
                QTimer::singleShot(timeoutMs * 2, dialog, [&, dialog]() {
                    refreshedWhileOpen = stationSpy.count() > stationCount
                        && currentSpy.count() > currentCount
                        && qvariant_cast<client::StationListResult>(stationSpy.last().at(0)).ok()
                        && qvariant_cast<client::CurrentOrderResult>(currentSpy.last().at(0)).ok();
                    noticeClosed = true;
                    dialog->accept();
                });
            }
        }
    });
    dialogPoller.start(5);
    QTest::mouseClick(stopButton, Qt::LeftButton);
    QTRY_VERIFY(noticeClosed);
    dialogPoller.stop();
    QVERIFY(refreshedWhileOpen);
    QCOMPARE(stopSpy.count(), 1);
    QVERIFY(qvariant_cast<client::ChargingStopResult>(stopSpy.first().at(0)).ok());
    if (!paid) {
        QTRY_COMPARE(navigation->currentIndex(), 4); // Debt still leads to the wallet.
    }

    navigation->setCurrentIndex(0);
    QTRY_VERIFY(refresh->isEnabled());
    const auto beforeRefresh = stationSpy.count();
    QTest::mouseClick(refresh, Qt::LeftButton);
    QTRY_VERIFY(stationSpy.count() > beforeRefresh);
    QVERIFY(qvariant_cast<client::StationListResult>(stationSpy.last().at(0)).ok());
    const auto beforeProfile = profileSpy.count();
    (void)api.getProfile();
    QTRY_VERIFY(profileSpy.count() > beforeProfile);
    QVERIFY(qvariant_cast<client::UserResult>(profileSpy.last().at(0)).ok());
    QVERIFY(navigation->isVisible());
    QTRY_COMPARE(currentCard->isVisible(), !paid);
    QVERIFY(!window.findChild<QLabel *>(QStringLiteral("stationActionMessage"))
                 ->text().contains(QStringLiteral("服务暂不可用")));
    QCOMPARE(connectionCount, 1);
    QCOMPARE(serverError, QString{});
    int stopRequests = 0;
    for (const auto &request : requests) {
        if (request.type == QString::fromLatin1(protocol::MessageType::OrderStop)) {
            ++stopRequests;
        }
        if (request.type != QString::fromLatin1(protocol::MessageType::AuthUserLogin)) {
            QCOMPARE(request.token.value_or(QString{}), login.value(QStringLiteral("token")).toString());
        }
    }
    QCOMPARE(stopRequests, 1);
}

QTEST_MAIN(TcpSettlementUiTests)

#include "tcp_settlement_ui_tests.moc"
