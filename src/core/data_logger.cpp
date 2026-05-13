#include "data_logger.h"
#include <QtCore/QDir>
#include <QtCore/QDateTime>

DataLogger::DataLogger(const QString &baseDir)
    : m_base_dir(baseDir)
{
    QDir().mkpath(baseDir);
}

DataLogger::~DataLogger()
{
    close();
}

bool DataLogger::startSession(const QString &filename, SessionInfo &sessionInfo)
{
    close();
    const QString filepath = m_base_dir + QStringLiteral("/") + filename;
    m_file.setFileName(filepath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    m_stream.setDevice(&m_file);
    m_stream.setEncoding(QStringConverter::Utf8);

    // Заголовок CSV
    m_stream << "timestamp,engine_rpm,torque,engine_temp,"
                "oil_pressure,fuel_pressure,boost_pressure,"
                "dyno_motor_temp,resistor_temp,oil_level,fuel_level\n";

    sessionInfo.csv_filename = filepath;
    m_log_counter = 0;
    return true;
}

void DataLogger::log(const MCUTelemetry &tele)
{
    if (!m_file.isOpen()) return;
    const SensorData &s = tele.sensors;
    m_stream << s.timestamp     << ','
             << s.engine_rpm    << ','
             << s.torque        << ','
             << s.engine_temp   << ','
             << s.oil_pressure  << ','
             << s.fuel_pressure << ','
             << s.boost_pressure<< ','
             << s.dyno_motor_temp<< ','
             << s.resistor_temp << ','
             << s.oil_level     << ','
             << s.fuel_level    << '\n';

    ++m_log_counter;
    if (m_log_counter % 50 == 0) m_file.flush();
}

void DataLogger::close()
{
    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }
    m_stream.setDevice(nullptr);
}
