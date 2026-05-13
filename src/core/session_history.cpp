#include "session_history.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

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
    limits[QStringLiteral("max_engine_temp")]    = session.limits.max_engine_temp;
    limits[QStringLiteral("min_oil_pressure")]   = session.limits.min_oil_pressure;
    limits[QStringLiteral("max_oil_pressure")]   = session.limits.max_oil_pressure;
    limits[QStringLiteral("min_fuel_pressure")]  = session.limits.min_fuel_pressure;
    limits[QStringLiteral("max_fuel_pressure")]  = session.limits.max_fuel_pressure;
    limits[QStringLiteral("max_boost_pressure")] = session.limits.max_boost_pressure;
    limits[QStringLiteral("max_engine_rpm")]     = session.limits.max_engine_rpm;
    limits[QStringLiteral("max_dyno_motor_temp")]= session.limits.max_dyno_motor_temp;
    limits[QStringLiteral("max_resistor_temp")]  = session.limits.max_resistor_temp;

    QJsonObject entry;
    entry[QStringLiteral("start_time")]      = session.start_time;
    entry[QStringLiteral("end_time")]        = session.end_time;
    entry[QStringLiteral("operator")]        = session.operator_name;
    entry[QStringLiteral("mode")]            = breakInModeDisplayName(session.mode);
    entry[QStringLiteral("duration")]        = session.duration;
    entry[QStringLiteral("warmup_duration")] = session.warmup_duration;
    entry[QStringLiteral("target")]          = session.target;
    entry[QStringLiteral("limits")]          = limits;
    entry[QStringLiteral("final_state")]     = static_cast<int>(session.final_state);
    entry[QStringLiteral("csv_file")]        = session.csv_filename;
    entry[QStringLiteral("report_file")]     = reportPath;

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
