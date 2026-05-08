#pragma once
#include <QtCore/QJsonArray>
#include "datatypes.h"

/**
 * session_history.h — хранение истории завершённых испытаний (JSON).
 */
class SessionHistory
{
public:
    explicit SessionHistory(const QString &baseDir = QStringLiteral("logs"));

    void addEntry(const SessionInfo &session, const QString &reportPath);
    QJsonArray loadAll() const;

private:
    QString m_index_path;
    void ensureIndex();
};
