// 本文件声明知识条目结构与知识库检索接口
#pragma once

#include <QList>
#include <QStringList>

namespace charging::client {

// KnowledgeEntry 表示一条知识：标题、关键词、正文与来源
struct KnowledgeEntry {
    QString id;
    QString title;
    QString question;
    QStringList keywords;
    QString content;
    QString source;
};

// KnowledgeBase 提供内置加载、检索与推荐问题
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
