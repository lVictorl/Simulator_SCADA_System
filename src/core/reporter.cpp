#include "reporter.h"
#include "qcustomplot.h"
#include <QtCore/QFile>
#include <QtCore/QDateTime>
#include <QtCore/QBuffer>
#include <QtGui/QPixmap>
#include <algorithm>

static QString fmtDouble(double v, int prec = 2)
{
    return QString::number(v, 'f', prec);
}

bool Reporter::generate(const SessionInfo &info,
                        const QVector<double> &timestamps,
                        const QVector<SensorData> &data,
                        const QString &outPath)
{
    // Извлекаем векторы для каждого параметра
    QVector<double> rpm, torque, engTemp, oilPrs, dynoTemp, resTemp;
    for (const auto &s : data) {
        rpm      << s.engine_rpm;
        torque   << s.torque;
        engTemp  << s.engine_temp;
        oilPrs   << s.oil_pressure;
        dynoTemp << s.dyno_motor_temp;
        resTemp  << s.resistor_temp;
    }

    const QString chartRpm     = renderChartPng(timestamps, rpm,     "об/мин", 0, 5000);
    const QString chartTorque  = renderChartPng(timestamps, torque,  "Н·м",   0, 300);
    const QString chartTemp    = renderChartPng(timestamps, engTemp, "°C",    0, 120);
    const QString chartOil     = renderChartPng(timestamps, oilPrs,  "бар",   0, 8);
    const QString chartDyno    = renderChartPng(timestamps, dynoTemp,"°C",    0, 180);
    const QString chartResistor= renderChartPng(timestamps, resTemp, "°C",    0, 250);

    const QString dt = QDateTime::fromSecsSinceEpoch(
                           static_cast<qint64>(info.start_time))
                       .toString(QStringLiteral("dd.MM.yyyy hh:mm:ss"));
    const QString dtEnd = QDateTime::fromSecsSinceEpoch(
                              static_cast<qint64>(info.end_time))
                          .toString(QStringLiteral("dd.MM.yyyy hh:mm:ss"));

    QString html;
    html += QStringLiteral("<!DOCTYPE html><html lang='ru'><head>"
                           "<meta charset='utf-8'>"
                           "<title>Отчёт об испытании</title>"
                           "<style>"
                           "body{font-family:Arial,sans-serif;margin:20px;}"
                           "h1{color:#333;}h2{color:#555;margin-top:30px;}"
                           "table{border-collapse:collapse;width:100%;}"
                           "td,th{border:1px solid #ccc;padding:6px 12px;}"
                           "th{background:#f0f0f0;}"
                           ".ok{color:green;} .err{color:red;}"
                           "img{max-width:100%;margin:10px 0;}"
                           "</style></head><body>");

    html += QStringLiteral("<h1>Отчёт об обкатке дизельного двигателя</h1>");
    html += QStringLiteral("<h2>Общая информация</h2><table>");
    auto row = [](const QString &k, const QString &v) {
        return QString("<tr><th>%1</th><td>%2</td></tr>").arg(k, v);
    };
    html += row(QStringLiteral("Оператор"),       info.operator_name);
    html += row(QStringLiteral("Режим"),           breakInModeDisplayName(info.mode));
    html += row(QStringLiteral("Начало"),          dt);
    html += row(QStringLiteral("Конец"),           dtEnd);
    html += row(QStringLiteral("Длительность, с"), fmtDouble(info.duration));
    html += row(QStringLiteral("Итог"),            breakInStateDisplayName(info.final_state));
    html += QStringLiteral("</table>");

    html += QStringLiteral("<h2>Заданные параметры и уставки</h2><table>");
    html += row(QStringLiteral("Цель (об/мин или Н·м)"), fmtDouble(info.target));
    html += row(QStringLiteral("Прогрев, с"),     fmtDouble(info.warmup_duration));
    html += row(QStringLiteral("Макс. темп. ДВС"),fmtDouble(info.limits.max_engine_temp) + " °C");
    html += row(QStringLiteral("Давление масла"), fmtDouble(info.limits.min_oil_pressure)
                + " … " + fmtDouble(info.limits.max_oil_pressure) + " бар");
    html += row(QStringLiteral("Макс. темп. ЭД"), fmtDouble(info.limits.max_dyno_motor_temp)+" °C");
    html += row(QStringLiteral("Макс. обор."),    fmtDouble(info.limits.max_engine_rpm)+" об/мин");
    html += QStringLiteral("</table>");

    if (!data.isEmpty()) {
        const auto [minRpm, maxRpm] = std::minmax_element(rpm.begin(), rpm.end());
        html += QStringLiteral("<h2>Фактические параметры (сводка)</h2><table>");
        html += row(QStringLiteral("Мин. обороты"), fmtDouble(*minRpm) + " об/мин");
        html += row(QStringLiteral("Макс. обороты"),fmtDouble(*maxRpm) + " об/мин");
        html += QStringLiteral("</table>");
    }

    html += QStringLiteral("<h2>Графики</h2>");
    html += QStringLiteral("<p><b>Обороты двигателя</b></p><img src='data:image/png;base64,") + chartRpm + QStringLiteral("'/>");
    html += QStringLiteral("<p><b>Крутящий момент</b></p><img src='data:image/png;base64,") + chartTorque + QStringLiteral("'/>");
    html += QStringLiteral("<p><b>Температура ДВС</b></p><img src='data:image/png;base64,") + chartTemp + QStringLiteral("'/>");
    html += QStringLiteral("<p><b>Давление масла</b></p><img src='data:image/png;base64,") + chartOil + QStringLiteral("'/>");
    html += QStringLiteral("<p><b>Температура ЭД</b></p><img src='data:image/png;base64,") + chartDyno + QStringLiteral("'/>");
    html += QStringLiteral("<p><b>Температура резистора</b></p><img src='data:image/png;base64,") + chartResistor + QStringLiteral("'/>");

    html += QStringLiteral("</body></html>");

    QFile file(outPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    file.write(html.toUtf8());
    return true;
}

QString Reporter::renderChartPng(const QVector<double> &time,
                                  const QVector<double> &values,
                                  const QString &yLabel,
                                  double yMin, double yMax) const
{
    QCustomPlot plot;
    plot.resize(800, 300);
    plot.addGraph();
    plot.graph(0)->setData(time, values);
    plot.graph(0)->setPen(QPen(Qt::blue, 1.5));
    plot.xAxis->setLabel(QStringLiteral("Время, с"));
    plot.yAxis->setLabel(yLabel);
    if (!time.isEmpty())
        plot.xAxis->setRange(time.first(), time.last());
    plot.yAxis->setRange(yMin, yMax);
    plot.replot();

    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QBuffer::WriteOnly);
    plot.toPixmap(800, 300).save(&buf, "PNG");
    return QString::fromLatin1(ba.toBase64());
}
