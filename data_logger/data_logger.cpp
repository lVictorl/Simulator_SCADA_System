#include "data_logger.h"
#include <QtCore/QDir>

DataLogger::DataLogger(const QString &baseDir)
    : m_base_dir(baseDir)
{
    QDir().mkpath(baseDir);
}

DataLogger::~DataLogger()
{
    close();
}
