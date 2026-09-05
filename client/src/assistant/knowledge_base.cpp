#include "assistant/knowledge_base.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

// Explicit initialization also keeps the qrc object linked from the static library.
static void initializeAssistantResources()
{
    Q_INIT_RESOURCE(assistant_resources);
}

namespace charging::client {
namespace {

QSet<QString> terms(const QString &text)
{
    QSet<QString> result;
    static const QSet<QString> stop{QStringLiteral("怎么"), QStringLiteral("如何"),
        QStringLiteral("什么"), QStringLiteral("可以"), QStringLiteral("一下"),
        QStringLiteral("帮我"), QStringLiteral("这个"), QStringLiteral("我的"),
        QStringLiteral("你好"), QStringLiteral("请问"), QStringLiteral("是否")};
    static const QRegularExpression words(QStringLiteral("[a-z0-9]+|[\\x{4e00}-\\x{9fff}]+"));
    auto matches = words.globalMatch(text.toLower());
    while (matches.hasNext()) {
        const QString word = matches.next().captured();
        if (word.front().unicode() < 128) {
            if (word.size() > 1) {
                result.insert(word);
            }
        } else {
            for (int i = 0; i + 1 < word.size(); ++i) {
                const auto term = word.mid(i, 2);
                if (!stop.contains(term)) {
                    result.insert(term);
                }
            }
        }
    }
    return result;
}

}  // namespace

KnowledgeBase KnowledgeBase::bundled()
{
    initializeAssistantResources();
    KnowledgeBase base;
    QFile file(QStringLiteral(":/assistant/knowledge.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return base;
    }
    const auto array = QJsonDocument::fromJson(file.readAll()).array();
    for (const auto value : array) {
        const auto object = value.toObject();
        KnowledgeEntry entry;
        entry.id = object.value(QStringLiteral("id")).toString();
        entry.title = object.value(QStringLiteral("title")).toString();
        entry.question = object.value(QStringLiteral("question")).toString();
        entry.content = object.value(QStringLiteral("content")).toString();
        entry.source = object.value(QStringLiteral("source")).toString();
        for (const auto keyword : object.value(QStringLiteral("keywords")).toArray()) {
            entry.keywords.append(keyword.toString());
        }
        if (!entry.id.isEmpty() && !entry.content.isEmpty()) {
            base.entries_.append(entry);
        }
    }
    return base;
}

QList<KnowledgeEntry> KnowledgeBase::retrieve(const QString &question,
                                             const QString &previousQuestion) const
{
    QString query = question.toLower().trimmed();
    // Resolve short follow-ups, without pulling old subjects into a new full question.
    static const QRegularExpression followUp(
        QStringLiteral("^(那|然后|还有|能取消|可以取消|为什么|具体|详细|它|这个|继续)"));
    if (query.size() <= 18 && followUp.match(query).hasMatch()) {
        query += QLatin1Char(' ') + previousQuestion.left(1200).toLower();
    }
    const auto queryTerms = terms(query);
    struct Match { double score; int index; };
    QList<Match> matches;
    for (int i = 0; i < entries_.size(); ++i) {
        const auto &entry = entries_[i];
        double score = 0;
        for (const auto &keyword : entry.keywords) {
            if (keyword.size() >= 2 && query.contains(keyword.toLower())) {
                score += 5.0 + std::min(keyword.size(), qsizetype(8)) * 0.2;
            }
        }
        const auto titleTerms = terms(entry.title + entry.question);
        const auto bodyTerms = terms(entry.content);
        int titleHits = 0;
        for (const auto &term : queryTerms) {
            titleHits += titleTerms.contains(term) ? 1 : 0;
            score += bodyTerms.contains(term) ? 0.18 : 0;
        }
        score += titleHits * 1.4;
        if (score >= 3.0) {
            matches.append({score, i});
        }
    }
    std::stable_sort(matches.begin(), matches.end(),
                     [](const Match &a, const Match &b) { return a.score > b.score; });
    QList<KnowledgeEntry> result;
    for (int i = 0; i < std::min(qsizetype(3), matches.size()); ++i) {
        // Do not attach weakly related documents merely to fill three slots.
        if (i == 0 || matches[i].score >= matches.front().score * 0.3) {
            result.append(entries_[matches[i].index]);
        }
    }
    return result;
}

QStringList KnowledgeBase::suggestedQuestions() const
{
    QStringList questions;
    for (const auto &entry : entries_) {
        if (!entry.question.isEmpty() && questions.size() < 6) {
            questions.append(entry.question);
        }
    }
    return questions;
}

}  // namespace charging::client
