#pragma once
/**
 * main_window.h — главное окно SCADA.
 *
 * Компоновка:
 *  ┌─────────────────────────────────────────────────────┐
 *  │ [Панель управления]  |  [Панель параметров сессии]  │
 *  ├──────────────────────┴─────────────────────────────-┤
 *  │ [Графики QCustomPlot — 6 каналов]                   │
 *  ├──────────────────────────────────────────────────────┤
 *  │ [Текущие показания — QLabel'ы]                       │
 *  ├──────────────────────────────────────────────────────┤
 *  │ [Журнал событий QTextEdit]                           │
 *  └──────────────────────────────────────────────────────┘
 */

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QSlider>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QProgressBar>
#include <QtCore/QTimer>
#include <QtCore/QVector>

#include "datatypes.h"
#include "qcustomplot.h"

class HighLevelController;
class ModbusMasterAdapter;
class DataLogger;
class SessionHistory;
class Reporter;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(HighLevelController *controller,
               ModbusMasterAdapter *master,
               DataLogger          *logger,
               SessionHistory      *history,
               QWidget             *parent = nullptr);
    ~MainWindow() override;

public slots:
    void onTelemetryReceived(const MCUTelemetry &tele);
    void onConnectionLost(int slaveId);
    void onConnectionRestored(int slaveId);

private slots:
    void onStartClicked();
    void onStopClicked();
    void onEmergencyClicked();
    void onNextStageClicked();
    void onResetEmergencyClicked();
    void onThrottleChanged(int value);
    void onManualThrottleToggled(bool checked);
    void onOpenActuators();
    void onOpenHistory();
    void onOpenFaultInjection();
    void processTelemetryQueue();

private:
    void setupUi();
    void setupMenuBar();
    void setupControlPanel(QWidget *parent);
    void setupSessionPanel(QWidget *parent);
    void setupPlots(QWidget *parent);
    void setupReadingsPanel(QWidget *parent);
    void setupEventLog(QWidget *parent);

    void updateUIState(BreakInState state);
    void updateReadings(const SensorData &s, const MCUTelemetry &tele);
    void appendLog(const QString &msg, bool isError = false);
    CriticalLimits collectLimits() const;

    void finishSession(const MCUTelemetry &last);

    // ── Компоненты управления ────────────────────────────────────────────────
    QComboBox     *m_modeCombo   = nullptr;
    QDoubleSpinBox*m_durationSpin= nullptr;
    QDoubleSpinBox*m_warmupSpin  = nullptr;
    QDoubleSpinBox*m_targetSpin  = nullptr;
    QPushButton   *m_btnStart    = nullptr;
    QPushButton   *m_btnStop     = nullptr;
    QPushButton   *m_btnEmergency= nullptr;
    QPushButton   *m_btnNextStage= nullptr;
    QPushButton   *m_btnReset    = nullptr;
    QCheckBox     *m_chkManualThr= nullptr;
    QSlider       *m_sliderThr   = nullptr;
    QLabel        *m_lblThrValue = nullptr;

    // ── Критические уставки ───────────────────────────────────────────────────
    QDoubleSpinBox *m_limitEngTemp    = nullptr;
    QDoubleSpinBox *m_limitOilMin     = nullptr;
    QDoubleSpinBox *m_limitOilMax     = nullptr;
    QDoubleSpinBox *m_limitFuelMin    = nullptr;
    QDoubleSpinBox *m_limitFuelMax    = nullptr;
    QDoubleSpinBox *m_limitBoost      = nullptr;
    QDoubleSpinBox *m_limitRpm        = nullptr;
    QDoubleSpinBox *m_limitDynoTemp   = nullptr;
    QDoubleSpinBox *m_limitResTemp    = nullptr;

    // ── Показания ─────────────────────────────────────────────────────────────
    QLabel *m_lblRpm          = nullptr;
    QLabel *m_lblTorque       = nullptr;
    QLabel *m_lblEngTemp      = nullptr;
    QLabel *m_lblOilPrs       = nullptr;
    QLabel *m_lblFuelPrs       = nullptr;
    QLabel *m_lblBoost        = nullptr;
    QLabel *m_lblDynoTemp     = nullptr;
    QLabel *m_lblResTemp      = nullptr;
    QLabel *m_lblOilLevel     = nullptr;
    QLabel *m_lblFuelLevel    = nullptr;
    QLabel *m_lblState        = nullptr;
    QLabel *m_lblThrottle     = nullptr;
    QLabel *m_lblStatus       = nullptr;

    // ── Журнал событий ────────────────────────────────────────────────────────
    QTextEdit *m_eventLog = nullptr;

    // ── Графики ───────────────────────────────────────────────────────────────
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

    // ── Зависимости ───────────────────────────────────────────────────────────
    HighLevelController *m_controller = nullptr;
    ModbusMasterAdapter *m_master     = nullptr;
    DataLogger          *m_logger     = nullptr;
    SessionHistory      *m_history    = nullptr;
    Reporter            *m_reporter   = nullptr;

    // ── Внутреннее состояние ──────────────────────────────────────────────────
    MCUTelemetry      m_lastTelemetry;
    bool              m_sessionActive = false;
    QVector<SensorData> m_sessionData;
    QVector<double>     m_sessionTimestamps;
    double              m_sessionStartTime = 0.0;
    QString             m_operatorName = QStringLiteral("Оператор");
};
