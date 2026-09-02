#include "charging/protocol/dto.h"
#include "charging/protocol/envelope.h"
#include "charging/protocol/frame_codec.h"
#include "charging/protocol/protocol_constants.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace charging::protocol;

namespace {

QJsonObject loadObject(const QString &fileName)
{
    QFile file(QDir(QString::fromUtf8(CHARGING_PROTOCOL_FIXTURE_DIR)).filePath(fileName));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    return document.object();
}

QByteArray rawFrame(const QByteArray &body)
{
    const quint32 length = static_cast<quint32>(body.size());
    QByteArray frame;
    frame.append(static_cast<char>((length >> 24U) & 0xFFU));
    frame.append(static_cast<char>((length >> 16U) & 0xFFU));
    frame.append(static_cast<char>((length >> 8U) & 0xFFU));
    frame.append(static_cast<char>(length & 0xFFU));
    frame.append(body);
    return frame;
}

}  // namespace

class ProtocolTests : public QObject {
    Q_OBJECT

private slots:
    void encodeUsesBigEndianLength();
    void decodesCompleteFrame();
    void buffersSplitHeader();
    void buffersSplitBody();
    void decodesCoalescedFrames();
    void encoderRejectsOversizedBody();
    void rejectsInvalidLengths();
    void rejectsInvalidJson();
    void rejectsNonObjectRoot();
    void requestEnvelopeRoundTrip();
    void responseEnvelopeRoundTrip();
    void envelopeRejectsInvalidFields();
    void dtoRoundTrips();
    void fixtureEnvelopesMatch();
    void progressFixturesAreMonotonicAndBillCorrectly();
};

void ProtocolTests::encodeUsesBigEndianLength()
{
    const QByteArray frame = encodeFrame(QJsonObject{});
    QCOMPARE(frame.left(4).toHex(), QByteArray("00000002"));
    QCOMPARE(frame.mid(4), QByteArray("{}"));
}

void ProtocolTests::decodesCompleteFrame()
{
    const QJsonObject message{{QStringLiteral("type"), QStringLiteral("system.ping")}};
    FrameDecoder decoder;
    const DecodeResult result = decoder.append(encodeFrame(message));

    QVERIFY(result.ok());
    QCOMPARE(result.messages.size(), qsizetype{1});
    QCOMPARE(result.messages.first(), message);
    QCOMPARE(decoder.bufferedBytes(), qsizetype{0});
}

void ProtocolTests::buffersSplitHeader()
{
    const QJsonObject message{{QStringLiteral("part"), QStringLiteral("header")}};
    const QByteArray frame = encodeFrame(message);
    FrameDecoder decoder;

    DecodeResult result = decoder.append(frame.left(2));
    QVERIFY(result.ok());
    QVERIFY(result.messages.isEmpty());
    QCOMPARE(decoder.bufferedBytes(), qsizetype{2});

    result = decoder.append(frame.mid(2));
    QVERIFY(result.ok());
    QCOMPARE(result.messages, QList<QJsonObject>{message});
}

void ProtocolTests::buffersSplitBody()
{
    const QJsonObject message{{QStringLiteral("part"), QStringLiteral("body")}};
    const QByteArray frame = encodeFrame(message);
    FrameDecoder decoder;

    DecodeResult result = decoder.append(frame.left(5));
    QVERIFY(result.ok());
    QVERIFY(result.messages.isEmpty());

    result = decoder.append(frame.mid(5));
    QVERIFY(result.ok());
    QCOMPARE(result.messages, QList<QJsonObject>{message});
}

void ProtocolTests::decodesCoalescedFrames()
{
    const QJsonObject first{{QStringLiteral("index"), 1}};
    const QJsonObject second{{QStringLiteral("index"), 2}};
    FrameDecoder decoder;

    const DecodeResult result = decoder.append(encodeFrame(first) + encodeFrame(second));
    QVERIFY(result.ok());
    QCOMPARE(result.messages, (QList<QJsonObject>{first, second}));
}

void ProtocolTests::encoderRejectsOversizedBody()
{
    const QString oversized(static_cast<qsizetype>(kMaxFrameBodyBytes), QLatin1Char('x'));
    QVERIFY(encodeFrame({{QStringLiteral("value"), oversized}}).isEmpty());
}

void ProtocolTests::rejectsInvalidLengths()
{
    FrameDecoder decoder;
    DecodeResult result = decoder.append(QByteArray(4, '\0'));
    QCOMPARE(result.error, FrameError::InvalidLength);
    QVERIFY(decoder.failed());

    decoder.reset();
    QByteArray oversized;
    oversized.append('\0');
    oversized.append('\x04');
    oversized.append('\0');
    oversized.append('\x01');
    result = decoder.append(oversized);
    QCOMPARE(result.error, FrameError::InvalidLength);
}

void ProtocolTests::rejectsInvalidJson()
{
    FrameDecoder decoder;
    const DecodeResult result = decoder.append(rawFrame(QByteArray("{")));
    QCOMPARE(result.error, FrameError::InvalidJson);
    QVERIFY(result.messages.isEmpty());
}

void ProtocolTests::rejectsNonObjectRoot()
{
    FrameDecoder decoder;
    const DecodeResult result = decoder.append(rawFrame(QByteArray("[]")));
    QCOMPARE(result.error, FrameError::RootNotObject);
    QVERIFY(result.messages.isEmpty());
}

void ProtocolTests::requestEnvelopeRoundTrip()
{
    RequestEnvelope original;
    original.version = kProtocolVersion;
    original.type = QString::fromLatin1(MessageType::StationList);
    original.requestId = QStringLiteral("request-1");
    original.token = QStringLiteral("token-1");
    original.data = {{QStringLiteral("region"), QStringLiteral("浑南区")}};

    QString error;
    RequestEnvelope parsed;
    QVERIFY2(RequestEnvelope::fromJson(original.toJson(), &parsed, &error), qPrintable(error));
    QCOMPARE(parsed.toJson(), original.toJson());
}

void ProtocolTests::responseEnvelopeRoundTrip()
{
    ResponseEnvelope original;
    original.version = kProtocolVersion;
    original.type = QString::fromLatin1(MessageType::StationList);
    original.requestId = QStringLiteral("request-1");
    original.code = ErrorCode::Ok;
    original.message = QStringLiteral("OK");
    original.data = {{QStringLiteral("items"), QJsonArray{}}};

    QString error;
    ResponseEnvelope parsed;
    QVERIFY2(ResponseEnvelope::fromJson(original.toJson(), &parsed, &error), qPrintable(error));
    QCOMPARE(parsed.toJson(), original.toJson());
}

void ProtocolTests::envelopeRejectsInvalidFields()
{
    QJsonObject request{
        {QStringLiteral("version"), 1},
        {QStringLiteral("type"), QStringLiteral("system.ping")},
        {QStringLiteral("requestId"), QStringLiteral("request-1")},
        {QStringLiteral("data"), QJsonArray{}},
    };
    RequestEnvelope parsed;
    QString error;
    QVERIFY(!RequestEnvelope::fromJson(request, &parsed, &error));
    QVERIFY(error.contains(QStringLiteral("data")));

    request.insert(QStringLiteral("data"), QJsonObject{});
    request.insert(QStringLiteral("version"), 2);
    QVERIFY(!RequestEnvelope::fromJson(request, &parsed, &error));
    QVERIFY(error.contains(QStringLiteral("version")));
}

void ProtocolTests::dtoRoundTrips()
{
    QString error;

    const QJsonObject login = loadObject(QStringLiteral("auth-user-login.response.json"));
    QVERIFY(!login.isEmpty());
    const QJsonObject userJson = login.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("user")).toObject();
    UserDto user;
    QVERIFY2(fromJson(userJson, &user, &error), qPrintable(error));
    QCOMPARE(toJson(user), userJson);

    const QJsonObject detail = loadObject(QStringLiteral("station-detail.response.json"));
    QVERIFY(!detail.isEmpty());
    const QJsonObject data = detail.value(QStringLiteral("data")).toObject();
    const QJsonObject stationJson = data.value(QStringLiteral("station")).toObject();
    StationDto station;
    QVERIFY2(fromJson(stationJson, &station, &error), qPrintable(error));
    QCOMPARE(toJson(station), stationJson);

    const QJsonObject pileJson = data.value(QStringLiteral("piles")).toArray().first().toObject();
    PileDto pile;
    QVERIFY2(fromJson(pileJson, &pile, &error), qPrintable(error));
    QCOMPARE(toJson(pile), pileJson);

    const QJsonObject stop = loadObject(QStringLiteral("order-stop.response.json"));
    const QJsonObject orderJson = stop.value(QStringLiteral("data")).toObject()
                                      .value(QStringLiteral("order")).toObject();
    OrderDto order;
    QVERIFY2(fromJson(orderJson, &order, &error), qPrintable(error));
    QCOMPARE(toJson(order), orderJson);

    QJsonObject badOrder = orderJson;
    badOrder.insert(QStringLiteral("status"), QStringLiteral("UNKNOWN"));
    QVERIFY(!fromJson(badOrder, &order, &error));
    QVERIFY(error.contains(QStringLiteral("status")));
}

void ProtocolTests::fixtureEnvelopesMatch()
{
    const QDir directory(QString::fromUtf8(CHARGING_PROTOCOL_FIXTURE_DIR));
    const QStringList requests = directory.entryList(
        {QStringLiteral("*.request.json")}, QDir::Files, QDir::Name);
    QVERIFY(!requests.isEmpty());

    for (const QString &requestName : requests) {
        const QJsonObject requestJson = loadObject(requestName);
        QVERIFY2(!requestJson.isEmpty(), qPrintable(requestName));

        RequestEnvelope request;
        QString error;
        QVERIFY2(RequestEnvelope::fromJson(requestJson, &request, &error),
                 qPrintable(requestName + QStringLiteral(": ") + error));

        QString responseName = requestName;
        responseName.replace(QStringLiteral(".request.json"), QStringLiteral(".response.json"));
        const QJsonObject responseJson = loadObject(responseName);
        QVERIFY2(!responseJson.isEmpty(), qPrintable(responseName));

        ResponseEnvelope response;
        QVERIFY2(ResponseEnvelope::fromJson(responseJson, &response, &error),
                 qPrintable(responseName + QStringLiteral(": ") + error));
        QCOMPARE(response.version, request.version);
        QCOMPARE(response.type, request.type);
        QCOMPARE(response.requestId, request.requestId);

        FrameDecoder decoder;
        const DecodeResult framed = decoder.append(encodeFrame(requestJson));
        QVERIFY(framed.ok());
        QCOMPARE(framed.messages, QList<QJsonObject>{requestJson});
    }
}

void ProtocolTests::progressFixturesAreMonotonicAndBillCorrectly()
{
    const QJsonObject first = loadObject(QStringLiteral("order-progress-1.response.json"));
    const QJsonObject second = loadObject(QStringLiteral("order-progress-2.response.json"));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());

    OrderDto firstOrder;
    OrderDto secondOrder;
    QString error;
    QVERIFY2(fromJson(first.value(QStringLiteral("data")).toObject()
                          .value(QStringLiteral("order")).toObject(), &firstOrder, &error),
             qPrintable(error));
    QVERIFY2(fromJson(second.value(QStringLiteral("data")).toObject()
                          .value(QStringLiteral("order")).toObject(), &secondOrder, &error),
             qPrintable(error));

    QVERIFY(secondOrder.durationSeconds >= firstOrder.durationSeconds);
    QVERIFY(secondOrder.energyWh >= firstOrder.energyWh);
    QVERIFY(secondOrder.amountCents >= firstOrder.amountCents);

    const auto expectedAmount = [](const OrderDto &order) {
        return (order.energyWh * *order.unitPriceCentsPerKwh + 500) / 1000;
    };
    QVERIFY(firstOrder.unitPriceCentsPerKwh.has_value());
    QVERIFY(secondOrder.unitPriceCentsPerKwh.has_value());
    QCOMPARE(firstOrder.amountCents, expectedAmount(firstOrder));
    QCOMPARE(secondOrder.amountCents, expectedAmount(secondOrder));
}

QTEST_GUILESS_MAIN(ProtocolTests)

#include "protocol_tests.moc"
