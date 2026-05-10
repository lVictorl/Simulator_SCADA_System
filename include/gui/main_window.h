#pragma once
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QSlider>
#include <QtWidgets/QGroupBox>
#include "datatypes.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
private:
    void setupUi();
    // остальные поля будут добавляться позже
};
    QCustomPlot *m_plotRpm     = nullptr;
    QCustomPlot *m_plotTorque  = nullptr;
    QCustomPlot *m_plotEngTemp = nullptr;
    QCustomPlot *m_plotOilPrs  = nullptr;
    QCustomPlot *m_plotDynoTemp= nullptr;
    QCustomPlot *m_plotResTemp = nullptr;

    QVector<double> m_plotTime;
    QVector<double> m_plotRpmData, m_plotTorqueData, m_plotEngTempData;
    QVector<double> m_plotOilPrsData, m_plotDynoTempData, m_plotResTempData;
    static constexpr int MAX_PLOT_POINTS = 3000;
    void setupPlots(QWidget *parent);
    void updatePlots();
    QTextEdit *m_eventLog = nullptr;
    void setupEventLog(QWidget *parent);
