#pragma once

#include <QList>
#include <QStringList>

namespace charging::client {

struct KnowledgeEntry {
    QString id;
    QString title;
    QString question;
    QStringList keywords;
    QString content;
    QString source;
};

class KnowledgeBase final {
public:
    static KnowledgeBase bundled();
    const QList<KnowledgeEntry> &entries() const { return entries_; }
    QList<KnowledgeEntry> retrieve(const QString &question,
                                  const QString &previousQuestion = {}) const;
    QStringList suggestedQuestions() const;

private:
    QList<KnowledgeEntry> entries_;
};

}  // namespace charging::client
