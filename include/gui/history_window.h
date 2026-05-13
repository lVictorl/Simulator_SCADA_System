#pragma once
#include <QtWidgets/QDialog>
#include <QtWidgets/QTableWidget>
#include "session_history.h"

class HistoryWindow : public QDialog
{
    Q_OBJECT
public:
    HistoryWindow(SessionHistory *history, QWidget *parent = nullptr);

private slots:
    void onOpenReport(int row, int col);

private:
    void populate();
    SessionHistory  *m_history;
    QTableWidget    *m_table = nullptr;
    QStringList      m_reportPaths;
};
