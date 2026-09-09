#include "ui/station_browser_page.h"
#include "ui/station_map_view.h"
#include "ui/station_preview_card.h"

#include "ui/station_discovery_policy.h"

#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

// 本文件实现电站浏览页的搜索、推荐与发现列表渲染
namespace charging::client {
namespace {
// 按用户ID分别保存搜索历史的配置键
QString historyKey(qint64 userId)
{
    return QStringLiteral("stationDiscovery/users/%1/searches").arg(userId);
}
// 关键词为空即全部匹配，否则匹配站名、地址或区域
bool matches(const protocol::StationDto &station, const QString &keyword)
{
    return keyword.isEmpty() || station.name.contains(keyword, Qt::CaseInsensitive)
        || station.address.contains(keyword, Qt::CaseInsensitive)
        || station.region.contains(keyword, Qt::CaseInsensitive);
}
// 清空容器内已有控件，避免重复渲染时残留
void clearContent(QWidget *widget)
{
    while (auto *item = widget->layout()->takeAt(0)) {
        if (auto *child = item->widget()) {
            child->hide();
            child->setObjectName({});
            child->deleteLater();
        }
        delete item;
    }
}
}

// 切换用户后重置访问记录并读取该用户最近8条搜索
void StationBrowserPage::setUserId(qint64 userId)
{
    if (userId_ != userId) { visitedStationIds_.clear(); visitHistoryFailed_ = false; }
    userId_ = userId;
    searchHistory_ = userId > 0 ? QSettings().value(historyKey(userId)).toStringList() : QStringList{};
    searchHistory_.removeDuplicates();
    while (searchHistory_.size() > 8) searchHistory_.removeLast();
    renderDiscovery();
}

// 从订单中找出最近一个真正开始过充电的电站
void StationBrowserPage::showVisitHistory(const QList<protocol::OrderDto> &orders)
{
    auto sorted = orders;
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return a.startedAt == b.startedAt ? a.orderId > b.orderId
            : a.startedAt.value_or(QString{}) > b.startedAt.value_or(QString{});
    });
    visitedStationIds_.clear();
    visitHistoryFailed_ = false;
    for (const auto &order : sorted) {
        // A reservation or a map preview does not mean the user visited the station.
        if (order.startedAt && !order.startedAt->isEmpty() && !visitedStationIds_.contains(order.stationId))
            { visitedStationIds_.append(order.stationId); break; }
    }
    renderDiscovery();
}

void StationBrowserPage::showVisitHistoryError()
{
    visitHistoryFailed_ = true;
    renderDiscovery();
}

// 打开搜索页并回填已生效的关键词
void StationBrowserPage::openSearch()
{
    keywordInput_->setText(appliedKeyword_);
    pages_->setCurrentWidget(searchPage_);
    renderSearch();
    keywordInput_->setFocus();
    keywordInput_->selectAll();
}

// 进入位置设置页，记住返回来源页面
void StationBrowserPage::openLocationSettings()
{
    if (pages_->currentWidget() == locationPage_) return;
    locationReturnPage_ = pages_->currentWidget();
    int preset = locationPresetCombo_->findData(currentLocation_.address);
    locationPresetCombo_->setCurrentIndex(preset >= 0 ? preset : locationPresetCombo_->count() - 1);
    locationAddressInput_->setText(currentLocation_.address);
    locationMessageLabel_->hide();
    pages_->setCurrentWidget(locationPage_);
}

// 提交搜索：写入历史并重新过滤电站
void StationBrowserPage::submitSearch()
{
    appliedKeyword_ = keywordInput_->text().trimmed().left(80);
    keywordInput_->setText(appliedKeyword_);
    if (!appliedKeyword_.isEmpty() && userId_ > 0) {
        searchHistory_.removeAll(appliedKeyword_);
        searchHistory_.prepend(appliedKeyword_);
        while (searchHistory_.size() > 8) searchHistory_.removeLast();
        QSettings().setValue(historyKey(userId_), searchHistory_);
    }
    applyDiscovery();
}

// 按关键词过滤电站、标记推荐站并刷新地图与文案
void StationBrowserPage::applyDiscovery()
{
    stations_.clear();
    for (const auto &station : catalog_)
        if (matches(station, appliedKeyword_)) stations_.append(station);
    const auto recommended = discovery::recommend(stations_);
    for (auto &station : stations_) station.recommended = station.stationId == recommended;
    const qint64 selected = stationMap_->selectedStationId();
    const bool retained = std::any_of(stations_.cbegin(), stations_.cend(), [selected](const auto &s) {
        return s.stationId == selected;
    });
    // 选中站被过滤掉时先关闭详情，避免它被重新打开
    if (selected > 0 && !retained) {
        // Cancel any outstanding detail request before it can reopen a filtered-out station.
        QWidget *returnPage = pages_->currentWidget();
        emit detailBackRequested();
        pages_->setCurrentWidget(returnPage);
    }
    stationMap_->setStations(stations_);
    if (retained) for (const auto &station : stations_)
        if (station.stationId == selected) stationPreview_->setStation(station);
    stationCountLabel_->setText(QStringLiteral("%1 个电站").arg(stations_.size()));
    homeSearchButton_->setText(appliedKeyword_.isEmpty() ? QStringLiteral("⌕  搜索电站、区域或地址")
                                                       : QStringLiteral("⌕  %1").arg(appliedKeyword_));
    listMessageLabel_->setText(stations_.isEmpty() ? QStringLiteral("没有找到符合条件的充电站") : QString{});
    listMessageLabel_->setVisible(stations_.isEmpty());
    renderDiscovery();
    layoutHomeOverlays();
}

// 重绘首页发现列表，同时同步搜索页内容
void StationBrowserPage::renderDiscovery()
{
    if (!discoveryList_) return;
    renderStationSections(discoveryList_, stations_, false);
    renderSearch();
}

// 按输入框草稿关键词实时预览搜索结果
void StationBrowserPage::renderSearch()
{
    QList<protocol::StationDto> results;
    const QString draft = keywordInput_->text().trimmed();
    for (const auto &station : catalog_)
        if (matches(station, draft)) results.append(station);
    searchMessage_->setText(draft.isEmpty() ? QStringLiteral("从熟悉的地方出发，也看看附近")
                                          : QStringLiteral("找到 %1 个电站").arg(results.size()));
    renderStationSections(searchContent_, results, true);
}

// 构建列表区块：历史、搜索结果或推荐与附近电站
void StationBrowserPage::renderStationSections(QWidget *container,
                                               const QList<protocol::StationDto> &items, bool search)
{
    clearContent(container);
    auto *layout = static_cast<QVBoxLayout *>(container->layout());
    const auto label = [container, layout](const QString &text, bool heading = false) {
        auto *result = new QLabel(text, container);
        result->setTextFormat(Qt::PlainText);
        result->setWordWrap(true);
        result->setProperty("role", heading ? "discoveryHeading" : "discoveryHint");
        layout->addWidget(result);
    };
    const auto action = [container, layout](const QString &text, const QString &name) {
        auto *button = new QPushButton(text, container);
        button->setObjectName(name);
        button->setMinimumWidth(0);
        button->setMinimumHeight(36);
        layout->addWidget(button);
        return button;
    };
    const bool hasKeyword = !(search ? keywordInput_->text().trimmed() : appliedKeyword_).isEmpty();
    // 无关键词的搜索页展示最近搜索与清空入口
    if (search && !hasKeyword) {
        label(QStringLiteral("最近搜索"), true);
        if (searchHistory_.isEmpty()) label(QStringLiteral("还没有搜索记录"));
        for (const auto &term : searchHistory_) {
            auto *button = action(term, "stationHistoryTerm");
            button->setProperty("searchTerm", term);
            connect(button, &QPushButton::clicked, this, [this, term] {
                keywordInput_->setText(term); submitSearch();
            });
        }
        if (!searchHistory_.isEmpty()) {
            auto *clear = action(QStringLiteral("清空搜索记录"), "stationHistoryClear");
            connect(clear, &QPushButton::clicked, this, [this] {
                if (userId_ > 0) QSettings().remove(historyKey(userId_));
                searchHistory_.clear(); renderDiscovery();
            });
        }
    }
    if (hasKeyword) {
        auto *clear = action(QStringLiteral("清除搜索"), search ? "stationSearchClearQuery" : "stationHomeClearQuery");
        connect(clear, &QPushButton::clicked, this, [this] {
            appliedKeyword_.clear(); keywordInput_->clear(); applyDiscovery();
        });
    }
    // 生成单个电站卡片，点击后打开预览
    const auto stationCard = [this, container, layout, search](const protocol::StationDto &station, const QString &section) {
        auto *card = new QPushButton(container);
        card->setObjectName(QStringLiteral("discoveryStation_%1_%2_%3").arg(search ? "search" : "home", section).arg(station.stationId));
        card->setProperty("stationId", station.stationId);
        card->setProperty("role", "discoveryStation");
        card->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(12, 10, 12, 10);
        cardLayout->setSpacing(4);
        const auto text = [card, cardLayout](const QString &value, bool title) {
            auto *line = new QLabel(value, card);
            line->setTextFormat(Qt::PlainText); line->setWordWrap(true);
            line->setAttribute(Qt::WA_TransparentForMouseEvents);
            line->setProperty("role", title ? "stationTitle" : "discoveryHint");
            line->setMinimumWidth(0);
            cardLayout->addWidget(line);
        };
        text(station.name, true);
        text(QStringLiteral("%1 · 空闲 %2/%3 · ¥%4/度")
            .arg(station.distanceKm ? QStringLiteral("%1 km").arg(*station.distanceKm, 0, 'f', 1) : QStringLiteral("距离未知"))
            .arg(station.availablePileCount).arg(station.totalPileCount)
            .arg(station.priceCentsPerKwh / 100.0, 0, 'f', 2), false);
        text(station.address, false);
        card->setAccessibleName(station.name + QStringLiteral("，查看电站信息"));
        layout->addWidget(card);
        connect(card, &QPushButton::clicked, this, [this, station, search] {
            if (search) submitSearch();
            previewStation(station.stationId);
        });
    };
    if (hasKeyword) {
        label(QStringLiteral("搜索结果"), true);
        for (const auto &station : items) stationCard(station, "results");
    // 首页分区：上次使用、为你推荐、附近电站
    } else {
        label(QStringLiteral("上次使用的电站"), true);
        bool visited = false;
        if (!visitHistoryFailed_ && !visitedStationIds_.isEmpty()) {
            for (const auto &station : items) if (station.stationId == visitedStationIds_.first()) {
                stationCard(station, "visited"); visited = true; break;
            }
        }
        if (!visited) label(visitHistoryFailed_ ? QStringLiteral("暂时无法读取充电记录，请刷新重试")
            : visitedStationIds_.isEmpty() ? QStringLiteral("首次充电后会在这里显示")
                                         : QStringLiteral("上次使用的电站当前不可用"));
        label(QStringLiteral("为你推荐"), true);
        const auto recommended = discovery::recommend(items);
        if (recommended) {
            for (const auto &station : items) if (station.stationId == recommended) {
                label(station.distanceKm && *station.distanceKm > discovery::preferredRadiusKm
                    ? QStringLiteral("10 公里内暂无可用站，已扩大至 30 公里推荐")
                    : QStringLiteral("综合距离、空闲情况与价格推荐"));
                stationCard(station, "recommended"); break;
            }
        } else label(QStringLiteral("30 公里内暂无可用电站，可修改当前位置或稍后刷新"));
        // 附近站不足时提示已从10公里扩大到30公里
        const auto nearby = discovery::nearby(items);
        const bool expanded = !nearby.isEmpty() && *nearby.last().distanceKm > discovery::preferredRadiusKm;
        label(expanded ? QStringLiteral("附近电站 · 30 公里内") : QStringLiteral("附近电站 · 10 公里内"), true);
        if (expanded) label(QStringLiteral("10 公里内可用电站较少，已补充稍远的站点"));
        for (const auto &station : nearby) stationCard(station, "nearby");
        if (nearby.isEmpty()) label(QStringLiteral("30 公里内暂无空闲电站，请稍后刷新或修改位置"));
    }
    if (items.isEmpty() && hasKeyword) label(QStringLiteral("没有找到电站，请更换关键词"));
    // 列表末尾提供刷新电站信息按钮
    auto *reload = action(QStringLiteral("刷新电站信息"), search ? "stationSearchReload" : "stationHomeReload");
    connect(reload, &QPushButton::clicked, this, &StationBrowserPage::refreshRequested);
    layout->addStretch();
}
} // namespace charging::client
