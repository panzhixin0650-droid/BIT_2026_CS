// 看板导出实现：把已组装好的快照写成 JSON 文件
#include "dashboard_exporter.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

namespace charging::server {

// 导出快照，失败时通过 error 返回原因
bool DashboardExporter::exportSnapshot(const QString &path,
                                       const QJsonObject &snapshot,
                                       QString *error) const
{
    // 路径为空直接失败，避免写到未知位置
    if (path.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("dashboard output path is empty");
        }
        return false;
    }

    // 用 QSaveFile 先写临时文件再提交，减少写坏文件的风险
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = output.errorString();
        }
        return false;
    }

    // 以缩进 JSON 输出，便于 Web 端和人工查看
    const QByteArray json = QJsonDocument(snapshot).toJson(QJsonDocument::Indented);
    // 写入不完整或提交失败都算导出失败
    if (output.write(json) != json.size() || !output.commit()) {
        if (error != nullptr) {
            *error = output.errorString();
        }
        return false;
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

}  // namespace charging::server
