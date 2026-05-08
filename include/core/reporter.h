#pragma once
#include "datatypes.h"
#include <QtCore/QVector>

/**
 * reporter.h — генерация HTML-отчёта по результатам испытания.
 * Использует QCustomPlot для рендеринга графиков в PNG (base64 → inline HTML).
 */
class Reporter
{
public:
    Reporter() = default;

    /**
     * Создаёт HTML-отчёт.
     * @param info       метаданные сессии
     * @param timestamps вектор меток времени
     * @param data       вектор SensorData (параллельный timestamps)
     * @param outPath    путь для сохранения .html
     * @return true при успехе
     */
    bool generate(const SessionInfo &info,
                  const QVector<double> &timestamps,
                  const QVector<SensorData> &data,
                  const QString &outPath);

private:
    QString renderChartPng(const QVector<double> &time,
                           const QVector<double> &values,
                           const QString &yLabel,
                           double yMin, double yMax) const;
};
