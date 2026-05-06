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

void SessionHistory::addEntry(const SessionInfo &session, const QString &reportPath)
{
    QJsonArray arr = loadAll();

    QJsonObject limits;
    limits["max_engine_temp"]     = session.limits.max_engine_temp;
    limits["min_oil_pressure"]    = session.limits.min_oil_pressure;
    limits["max_oil_pressure"]    = session.limits.max_oil_pressure;
    limits["min_fuel_pressure"]   = session.limits.min_fuel_pressure;
    limits["max_fuel_pressure"]   = session.limits.max_fuel_pressure;
    limits["max_boost_pressure"]  = session.limits.max_boost_pressure;
    limits["max_engine_rpm"]      = session.limits.max_engine_rpm;
    limits["max_dyno_motor_temp"] = session.limits.max_dyno_motor_temp;
    limits["max_resistor_temp"]   = session.limits.max_resistor_temp;

    QJsonObject entry;
    entry["start_time"]      = session.start_time;
    entry["end_time"]        = session.end_time;
    entry["operator"]        = session.operator_name;
    entry["mode"]            = breakInModeDisplayName(session.mode);
    entry["duration"]        = session.duration;
    entry["warmup_duration"] = session.warmup_duration;
    entry["target"]          = session.target;
    entry["limits"]          = limits;
    entry["final_state"]     = static_cast<int>(session.final_state);
    entry["csv_file"]        = session.csv_filename;
    entry["report_file"]     = reportPath;

    arr.append(entry);
    QFile f(m_index_path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    }
}

QJsonArray SessionHistory::loadAll() const
{
    QFile f(m_index_path);
    if (!f.open(QIODevice::ReadOnly)) return QJsonArray();
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.isArray() ? doc.array() : QJsonArray();
}
