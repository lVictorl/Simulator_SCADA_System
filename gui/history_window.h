#pragma once
#include <QtWidgets/QDialog>
#include <QtWidgets/QTableWidget>
#include "session_history.h"

class HistoryWindow : public QDialog {
    Q_OBJECT
public:
    explicit HistoryWindow(SessionHistory *history, QWidget *parent = nullptr);
private:
    QTableWidget *m_table;
    SessionHistory *m_history;
};
