#pragma once
/**
 * data_logger.h — журналирование телеметрии в CSV.
 * Периодически сбрасывает буфер на диск для защиты от потери данных.
 */
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include "datatypes.h"

class DataLogger
{
public:
    explicit DataLogger(const QString &baseDir = QStringLiteral("logs"));
    ~DataLogger();

    bool startSession(const QString &filename, SessionInfo &sessionInfo);
    void log(const MCUTelemetry &tele);
    void close();

    bool isOpen() const { return m_file.isOpen(); }

private:
    QString     m_base_dir;
    QFile       m_file;
    QTextStream m_stream;
    int         m_log_counter = 0;
};
