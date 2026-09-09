// 本文件测试助理界面：预设、发送键、重试、布局与登出清理
#include "assistant/assistant_service.h"
#include "assistant_test_network.h"
#include "api/mock_charging_api.h"
#include "local/mock_map_service.h"
#include "ui/client_theme.h"
#include "ui/main_window.h"
#include "ui/support_page.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QtTest>

using namespace charging::client;

namespace {
template<typename T> T *child(QWidget &page, const char *name)
{
    return page.findChild<T *>(QString::fromLatin1(name));
}

// 辅助函数：填入问题并点击发送按钮
void send(QWidget &page, const QString &text)
{
    child<QPlainTextEdit>(page, "assistantInput")->setPlainText(text);
    child<QPushButton>(page, "assistantSend")->click();
}

// 按环境变量导出窗口截图供人工核对
void capture(QWidget &window, const QString &name)
{
    const auto directory = qEnvironmentVariable("CHARGING_ASSISTANT_SCREENSHOTS");
    if (!directory.isEmpty()) {
        QVERIFY(window.grab().save(QDir(directory).filePath(name + QStringLiteral(".png"))));
    }
}
}  // namespace

// 助理界面测试集合
class AssistantUiTests final : public QObject {
    Q_OBJECT
private slots:
    void presetsCopySourcesAndReset();
    void enterShiftEnterAndInputMethod();
    void failedRequestsCanRetryOrStop();
    void resetCancelsAndDiscardsLateResults();
    void longMessagesRemainPlainTextAndWrapped();
    void fullWindowAtSmallSizes_data();
    void fullWindowAtSmallSizes();
    void logoutClearsConversation();
    void liveFullWindow();
};

// 预设问题、复制回答、查看来源与新建会话
void AssistantUiTests::presetsCopySourcesAndReset()
{
    assistant_test::Network network;
    AssistantService service({}, nullptr, &network);
    SupportPage page(service);
    page.resize(420, 704);
    page.show();
    QVERIFY(!child<QPushButton>(page, "assistantSend")->isEnabled());
    QVERIFY(child<QWidget>(page, "supportCard")->isVisible());
    child<QPushButton>(page, "assistantSuggestion1")->click();
    QTRY_VERIFY(child<QToolButton>(page, "assistantCopy") != nullptr);
    QVERIFY(!child<QWidget>(page, "supportCard")->isVisible());
    QCOMPARE(network.requests.size(), 0);
    auto *answer = child<QLabel>(page, "assistantAnswerText");
    QVERIFY(answer->text().contains(QStringLiteral("预约")));
    child<QToolButton>(page, "assistantCopy")->click();
    QCOMPARE(QApplication::clipboard()->text(), answer->text());
    child<QToolButton>(page, "assistantSources")->click();
    QVERIFY(child<QLabel>(page, "assistantSourceText")->isVisible());
    child<QPushButton>(page, "assistantNewChat")->click();
    QTRY_VERIFY(child<QLabel>(page, "assistantAnswerText") == nullptr);
    QVERIFY(child<QWidget>(page, "supportCard")->isVisible());
    QVERIFY(child<QPlainTextEdit>(page, "assistantInput")->toPlainText().isEmpty());
}

// 回车发送、Shift 回车换行，输入法预编辑期间不误发
void AssistantUiTests::enterShiftEnterAndInputMethod()
{
    AssistantService service;
    SupportPage page(service);
    page.resize(420, 700);
    page.show();
    auto *input = child<QPlainTextEdit>(page, "assistantInput");
    input->setPlainText(QStringLiteral("预约"));
    input->moveCursor(QTextCursor::End);
    QTest::keyClick(input, Qt::Key_Return, Qt::ShiftModifier);
    QVERIFY(input->toPlainText().contains(QLatin1Char('\n')));
    QCOMPARE(page.findChildren<QLabel *>(QStringLiteral("assistantUserText")).size(), 0);
    QInputMethodEvent preedit(QStringLiteral("yu"), {});
    QApplication::sendEvent(input, &preedit);
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(page.findChildren<QLabel *>(QStringLiteral("assistantUserText")).size(), 0);
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("约"));
    QApplication::sendEvent(input, &commit);
    QTest::keyClick(input, Qt::Key_Return);
    QTRY_COMPARE(page.findChildren<QLabel *>(QStringLiteral("assistantUserText")).size(), 1);
    QTRY_VERIFY(!service.isBusy());
    input->setPlainText(QString(1201, QLatin1Char('x')));
    QVERIFY(!child<QPushButton>(page, "assistantSend")->isEnabled());
}

// 失败后可重试，失败回答不作为上下文；也可停止等待
void AssistantUiTests::failedRequestsCanRetryOrStop()
{
    assistant_test::Network network;
    network.status = 401;
    AssistantService service(assistant_test::config(), nullptr, &network);
    SupportPage page(service);
    page.resize(420, 700);
    page.show();
    send(page, QStringLiteral("如何预约充电？"));
    QVERIFY(child<QPushButton>(page, "assistantStop")->isVisible());
    QVERIFY(!child<QComboBox>(page, "assistantMode")->isEnabled());
    QTRY_VERIFY(child<QToolButton>(page, "assistantRetry") != nullptr);
    QVERIFY(child<QLabel>(page, "assistantError")->text().contains(QStringLiteral("鉴权")));
    network.status = 200;
    child<QToolButton>(page, "assistantRetry")->click();
    QTRY_VERIFY(child<QToolButton>(page, "assistantCopy") != nullptr);
    const auto secondInput = QJsonDocument::fromJson(network.payloads.last()).object().value("input").toArray();
    QCOMPARE(secondInput.size(), 1); // Failed answers are not conversation context.
    network.hang = true;
    send(page, QStringLiteral("如何导航？"));
    child<QPushButton>(page, "assistantStop")->click();
    QVERIFY(!service.isBusy());
    QVERIFY(child<QPushButton>(page, "assistantSend")->isVisible());
    QVERIFY(child<QComboBox>(page, "assistantMode")->isEnabled());
}

// 重置会话会取消请求并丢弃迟到结果
void AssistantUiTests::resetCancelsAndDiscardsLateResults()
{
    assistant_test::Network network;
    network.hang = true;
    AssistantService service(assistant_test::config(), nullptr, &network);
    SupportPage page(service);
    page.show();
    send(page, QStringLiteral("预约充电"));
    QVERIFY(service.isBusy());
    page.resetConversation();
    QVERIFY(!service.isBusy());
    QTRY_VERIFY(child<QLabel>(page, "assistantAnswerText") == nullptr);
    network.hang = false;
    send(page, QStringLiteral("如何导航？"));
    QTRY_VERIFY(child<QToolButton>(page, "assistantCopy") != nullptr);
    const auto input = QJsonDocument::fromJson(network.payloads.last()).object().value("input").toArray();
    QCOMPARE(input.size(), 1);
    QCOMPARE(page.findChildren<QLabel *>(QStringLiteral("assistantUserText")).size(), 1);
}

// 长回答保持纯文本换行，不出现横向滚动
void AssistantUiTests::longMessagesRemainPlainTextAndWrapped()
{
    assistant_test::Network network;
    network.body = assistant_test::success(QStringLiteral("<img src='https://example.invalid/x'>")
                                            + QString(1800, QChar(0x5145)));
    AssistantService service(assistant_test::config(), nullptr, &network);
    SupportPage page(service);
    page.resize(360, 590);
    page.show();
    send(page, QStringLiteral("预约充电"));
    QTRY_VERIFY(child<QToolButton>(page, "assistantCopy") != nullptr);
    auto *label = child<QLabel>(page, "assistantAnswerText");
    QCOMPARE(label->textFormat(), Qt::PlainText);
    QVERIFY(label->wordWrap());
    auto *scroll = child<QScrollArea>(page, "assistantScroll");
    QTRY_COMPARE(scroll->horizontalScrollBar()->maximum(), 0);
    QVERIFY(child<QPlainTextEdit>(page, "assistantInput")->isVisible());
}

void AssistantUiTests::fullWindowAtSmallSizes_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("360x640") << QSize(360, 640);
    QTest::newRow("420x760") << QSize(420, 760);
}

// 小窗口下整窗布局：输入区不越界且在导航栏之上
void AssistantUiTests::fullWindowAtSmallSizes()
{
    QFETCH(QSize, size);
    MockChargingApi api;
    MainWindow window(api);
    window.resize(size);
    window.show();
    child<QLineEdit>(window, "phoneInput")->setText(QStringLiteral("13800000001"));
    child<QLineEdit>(window, "verificationCodeInput")->setText(QStringLiteral("123456"));
    child<QPushButton>(window, "loginButton")->click();
    auto *tabs = child<QTabWidget>(window, "mainNavigation");
    QTRY_VERIFY(tabs->isVisible());
    tabs->setCurrentIndex(3);
    QTest::qWait(60);
    QCOMPARE(window.size(), size);
    auto *scroll = child<QScrollArea>(window, "assistantScroll");
    QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
    auto *composer = child<QWidget>(window, "assistantComposer");
    QVERIFY(composer->isVisible());
    const auto origin = composer->mapTo(&window, QPoint(0, 0));
    QVERIFY(origin.x() >= 0);
    QVERIFY(origin.x() + composer->width() <= window.width());
    const int navigationTop = tabs->tabBar()->mapTo(&window, QPoint(0, 0)).y();
    QVERIFY(origin.y() + composer->height() <= navigationTop);
    const auto suffix = QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
    capture(window, QStringLiteral("assistant-welcome-") + suffix);
    child<QPushButton>(window, "assistantSuggestion1")->click();
    QTRY_VERIFY(child<QToolButton>(window, "assistantCopy") != nullptr);
    QTest::qWait(60);
    QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
    capture(window, QStringLiteral("assistant-chat-") + suffix);
}

// 退出登录后助理会话内容被清空
void AssistantUiTests::logoutClearsConversation()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();
    child<QLineEdit>(window, "phoneInput")->setText(QStringLiteral("13800000001"));
    child<QLineEdit>(window, "verificationCodeInput")->setText(QStringLiteral("123456"));
    child<QPushButton>(window, "loginButton")->click();
    auto *tabs = child<QTabWidget>(window, "mainNavigation");
    QTRY_VERIFY(tabs->isVisible());
    tabs->setCurrentIndex(3);
    child<QPushButton>(window, "assistantSuggestion1")->click();
    QTRY_VERIFY(child<QToolButton>(window, "assistantCopy") != nullptr);
    tabs->setCurrentIndex(4);
    auto *logout = child<QPushButton>(window, "logoutButton");
    QTRY_VERIFY(logout->isEnabled());
    logout->click();
    QTRY_VERIFY(child<QWidget>(window, "loginPage")->isVisible());
    QTRY_VERIFY(child<QLabel>(window, "assistantAnswerText") == nullptr);
}

// 真实接口的界面验证需显式配置才执行
void AssistantUiTests::liveFullWindow()
{
    const auto path = qEnvironmentVariable("CHARGING_ASSISTANT_LIVE_CONFIG");
    if (path.isEmpty()) { QSKIP("Live GUI verification is opt-in."); }
    const auto config = AssistantConfig::load(path);
    QVERIFY2(config.isReady(), qPrintable(config.validationError()));
    MockChargingApi api;
    MockMapService map;
    MainWindow window(api, map, config);
    window.show();
    child<QLineEdit>(window, "phoneInput")->setText(QStringLiteral("13800000001"));
    child<QLineEdit>(window, "verificationCodeInput")->setText(QStringLiteral("123456"));
    child<QPushButton>(window, "loginButton")->click();
    auto *tabs = child<QTabWidget>(window, "mainNavigation");
    QTRY_VERIFY(tabs->isVisible());
    tabs->setCurrentIndex(3);
    QTest::qWait(60);
    QCOMPARE(child<QComboBox>(window, "assistantMode")->currentIndex(), 0);
    capture(window, QStringLiteral("assistant-live-welcome-420x760"));
    auto *service = window.findChild<AssistantService *>();
    QSignalSpy finished(service, &AssistantService::finished);
    QSignalSpy deltas(service, &AssistantService::answerUpdated);
    send(window, QStringLiteral("如何预约充电？用三步简单说明。"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 125000);
    const auto result = qvariant_cast<AssistantResult>(finished.first()[1]);
    QVERIFY2(result.success && result.remote, qPrintable(result.error));
    QVERIFY(deltas.size() > 0);
    QVERIFY(child<QToolButton>(window, "assistantCopy") != nullptr);
    QTest::qWait(100);
    capture(window, QStringLiteral("assistant-live-answer-420x760"));
    finished.clear();
    deltas.clear();
    send(window, QStringLiteral("那可以取消吗？"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 125000);
    const auto followUp = qvariant_cast<AssistantResult>(finished.first()[1]);
    QVERIFY2(followUp.success && followUp.remote, qPrintable(followUp.error));
    QVERIFY(deltas.size() > 0);
    QVERIFY(!followUp.sources.isEmpty());
    QCOMPARE(followUp.sources.first().id, QStringLiteral("reserve"));
    QCOMPARE(window.findChildren<QLabel *>(QStringLiteral("assistantUserText")).size(), 2);
}

QTEST_MAIN(AssistantUiTests)
#include "assistant_ui_tests.moc"
