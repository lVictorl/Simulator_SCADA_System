#include "history_window.h"
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtCore/QJsonObject>
#include <QtCore/QDateTime>
#include <QtCore/QFileInfo>
#include <QtGui/QDesktopServices>
#include <QtCore/QUrl>

HistoryWindow::HistoryWindow(SessionHistory *history, QWidget *parent)
    : QDialog(parent), m_history(history)
{
    setWindowTitle(QStringLiteral("История испытаний"));
    resize(900, 500);
    auto *layout = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("Дата начала"),
        QStringLiteral("Оператор"),
        QStringLiteral("Режим"),
        QStringLiteral("Длит., с"),
        QStringLiteral("Итог"),
        QStringLiteral("Отчёт")
    });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_table, &QTableWidget::cellDoubleClicked,
            this, &HistoryWindow::onOpenReport);
    layout->addWidget(m_table);

    populate();
}

void HistoryWindow::populate()
{
    const QJsonArray arr = m_history->loadAll();
    m_table->setRowCount(arr.size());
    m_reportPaths.clear();

    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject obj = arr[i].toObject();
        const double st = obj[QStringLiteral("start_time")].toDouble();
        const QString dt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(st))
                           .toString(QStringLiteral("dd.MM.yyyy hh:mm"));

        m_table->setItem(i, 0, new QTableWidgetItem(dt));
        m_table->setItem(i, 1, new QTableWidgetItem(obj[QStringLiteral("operator")].toString()));
        m_table->setItem(i, 2, new QTableWidgetItem(obj[QStringLiteral("mode")].toString()));
        m_table->setItem(i, 3, new QTableWidgetItem(
            QString::number(obj[QStringLiteral("duration")].toDouble(), 'f', 0)));
        m_table->setItem(i, 4, new QTableWidgetItem(
            obj[QStringLiteral("final_state")].toString()));
        m_table->setItem(i, 5, new QTableWidgetItem(QStringLiteral("Двойной клик → открыть")));

        m_reportPaths << obj[QStringLiteral("report_file")].toString();
    }
}

void HistoryWindow::onOpenReport(int row, int /*col*/)
{
    if (row < 0 || row >= m_reportPaths.size()) return;
    const QString path = QFileInfo(m_reportPaths[row]).absoluteFilePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
