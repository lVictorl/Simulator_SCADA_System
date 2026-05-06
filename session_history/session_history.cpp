#include "session_history.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>

SessionHistory::SessionHistory(const QString &baseDir)
    : m_index_path(baseDir + QStringLiteral("/session_index.json"))
{
    QDir().mkpath(baseDir);
    ensureIndex();
}

void SessionHistory::ensureIndex()
{
    QFile f(m_index_path);
    if (!f.exists()) {
        if (f.open(QIODevice::WriteOnly)) { f.write("[]"); }
    }
}
