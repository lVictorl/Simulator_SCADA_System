#include "history_window.h"
#include <QtWidgets/QVBoxLayout>

HistoryWindow::HistoryWindow(SessionHistory *history, QWidget *parent)
    : QDialog(parent), m_history(history) {
    setWindowTitle("История испытаний");
    auto *layout = new QVBoxLayout(this);
    m_table = new QTableWidget(0, 5);
    layout->addWidget(m_table);
}
