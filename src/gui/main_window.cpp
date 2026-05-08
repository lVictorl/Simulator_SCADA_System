#include "gui/main_window.h"
#include "high_level_controller.h"
#include "modbus_master_adapter.h"
#include "data_logger.h"
#include "session_history.h"
#include "reporter.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLineEdit>
#include <QtCore/QDateTime>
#include <QtCore/QElapsedTimer>

MainWindow::MainWindow(HighLevelController *controller,
                       ModbusMasterAdapter *master,
                       DataLogger          *logger,
                       SessionHistory      *history,
                       QWidget             *parent)
    : QMainWindow(parent)
    , m_controller(controller)
    , m_master(master)
    , m_logger(logger)
    , m_history(history)
    , m_reporter(new Reporter)
{
    setWindowTitle(QStringLiteral("SCADA — Обкатка дизельного двигателя"));
    resize(1400, 900);
    setupUi();
    updateUIState(BreakInState::IDLE);
    connect(master, &ModbusMasterAdapter::telemetryReady,
            this,   &MainWindow::onTelemetryReceived);
    connect(master, &ModbusMasterAdapter::connectionLost,
            this,   &MainWindow::onConnectionLost);
    connect(master, &ModbusMasterAdapter::connectionRestored,
            this,   &MainWindow::onConnectionRestored);
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    auto *topWidget = new QWidget;
    auto *topLayout = new QHBoxLayout(topWidget);
    setupControlPanel(topWidget);
    mainLayout->addWidget(topWidget);

    auto *splitter = new QSplitter(Qt::Vertical);
    auto *plotWidget = new QWidget;
    // setupPlots(plotWidget);   // БУДЕТ ДОБАВЛЕНО ПОЗЖЕ
    splitter->addWidget(plotWidget);

    auto *bottomWidget = new QWidget;
    auto *bottomLayout = new QHBoxLayout(bottomWidget);
    setupReadingsPanel(bottomWidget);
    // setupEventLog(bottomWidget);       // БУДЕТ
    splitter->addWidget(bottomWidget);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(splitter, 1);

    m_lblStatus = new QLabel(QStringLiteral("Готов"));
    statusBar()->addPermanentWidget(m_lblStatus);
}

// --- setupControlPanel ---
void MainWindow::setupControlPanel(QWidget *parent)
{
    auto *group = new QGroupBox(QStringLiteral("Управление"), parent);
    auto *layout = new QVBoxLayout(group);

    auto *modeRow = new QHBoxLayout;
    modeRow->addWidget(new QLabel(QStringLiteral("Режим:")));
    m_modeCombo = new QComboBox;
    m_modeCombo->addItem(QStringLiteral("Холодная обкатка"),    QVariant::fromValue(BreakInMode::COLD));
    m_modeCombo->addItem(QStringLiteral("Горячая без нагрузки"),QVariant::fromValue(BreakInMode::HOT_NOLOAD));
    m_modeCombo->addItem(QStringLiteral("Горячая под нагрузкой"),QVariant::fromValue(BreakInMode::HOT_LOAD));
    modeRow->addWidget(m_modeCombo);
    layout->addLayout(modeRow);

    auto *paramsGrid = new QGridLayout;
    paramsGrid->addWidget(new QLabel(QStringLiteral("Длительность, с:")), 0, 0);
    m_durationSpin = new QDoubleSpinBox;
    m_durationSpin->setRange(10, 86400); m_durationSpin->setValue(60);
    paramsGrid->addWidget(m_durationSpin, 0, 1);

    paramsGrid->addWidget(new QLabel(QStringLiteral("Прогрев, с:")), 1, 0);
    m_warmupSpin = new QDoubleSpinBox;
    m_warmupSpin->setRange(0, 3600); m_warmupSpin->setValue(30);
    paramsGrid->addWidget(m_warmupSpin, 1, 1);

    paramsGrid->addWidget(new QLabel(QStringLiteral("Цель (об/мин | Н·м):")), 2, 0);
    m_targetSpin = new QDoubleSpinBox;
    m_targetSpin->setRange(0, 5000); m_targetSpin->setValue(1000);
    paramsGrid->addWidget(m_targetSpin, 2, 1);
    layout->addLayout(paramsGrid);

    m_btnStart = new QPushButton(QStringLiteral("▶  СТАРТ"));
    m_btnStart->setStyleSheet(QStringLiteral("background:#2d8a2d;color:white;font-weight:bold;padding:8px;"));
    connect(m_btnStart, &QPushButton::clicked, this, &MainWindow::onStartClicked);

    m_btnStop = new QPushButton(QStringLiteral("■  СТОП"));
    m_btnStop->setStyleSheet(QStringLiteral("background:#5a5a5a;color:white;font-weight:bold;padding:8px;"));
    connect(m_btnStop, &QPushButton::clicked, this, &MainWindow::onStopClicked);

    m_btnEmergency = new QPushButton(QStringLiteral("⚠  АВАРИЯ"));
    m_btnEmergency->setStyleSheet(QStringLiteral("background:#c00000;color:white;font-weight:bold;padding:8px;"));
    connect(m_btnEmergency, &QPushButton::clicked, this, &MainWindow::onEmergencyClicked);

    m_btnNextStage = new QPushButton(QStringLiteral("→  Следующий этап"));
    connect(m_btnNextStage, &QPushButton::clicked, this, &MainWindow::onNextStageClicked);

    m_btnReset = new QPushButton(QStringLiteral("↺  Сброс аварии"));
    connect(m_btnReset, &QPushButton::clicked, this, &MainWindow::onResetEmergencyClicked);

    auto *btnRow1 = new QHBoxLayout;
    btnRow1->addWidget(m_btnStart);
    btnRow1->addWidget(m_btnStop);
    layout->addLayout(btnRow1);
    layout->addWidget(m_btnEmergency);

    auto *btnRow2 = new QHBoxLayout;
    btnRow2->addWidget(m_btnNextStage);
    btnRow2->addWidget(m_btnReset);
    layout->addLayout(btnRow2);

    auto *thrGroup = new QGroupBox(QStringLiteral("Дроссельная заслонка"), group);
    auto *thrLayout = new QVBoxLayout(thrGroup);

    m_chkManualThr = new QCheckBox(QStringLiteral("Ручное управление"));
    connect(m_chkManualThr, &QCheckBox::toggled, this, &MainWindow::onManualThrottleToggled);
    thrLayout->addWidget(m_chkManualThr);

    auto *sliderRow = new QHBoxLayout;
    m_sliderThr = new QSlider(Qt::Horizontal);
    m_sliderThr->setRange(0, 100); m_sliderThr->setEnabled(false);
    m_lblThrValue = new QLabel(QStringLiteral("0 %"));
    m_lblThrValue->setMinimumWidth(40);
    connect(m_sliderThr, &QSlider::valueChanged, this, &MainWindow::onThrottleChanged);
    sliderRow->addWidget(m_sliderThr);
    sliderRow->addWidget(m_lblThrValue);
    thrLayout->addLayout(sliderRow);
    layout->addWidget(thrGroup);

    auto *topLayout = qobject_cast<QHBoxLayout*>(parent->layout());
    if (topLayout) topLayout->addWidget(group);
}

// Заглушки для остальных методов (будут реализованы позже)
void MainWindow::onTelemetryReceived(const MCUTelemetry &) {}
void MainWindow::onConnectionLost(int) {}
void MainWindow::onConnectionRestored(int) {}
void MainWindow::onStartClicked() {}
void MainWindow::onStopClicked() {}
void MainWindow::onEmergencyClicked() {}
void MainWindow::onNextStageClicked() {}
void MainWindow::onResetEmergencyClicked() {}
void MainWindow::onOpenActuators() {}
void MainWindow::onOpenHistory() {}
void MainWindow::onOpenFaultInjection() {}
void MainWindow::updateUIState(BreakInState) {}
void MainWindow::updateReadings(const SensorData &, const MCUTelemetry &) {}
void MainWindow::appendLog(const QString &, bool) {}
CriticalLimits MainWindow::collectLimits() const { return CriticalLimits(); }
void MainWindow::finishSession(const MCUTelemetry &) {}

void MainWindow::setupReadingsPanel(QWidget *parent)
{
    auto *group  = new QGroupBox(QStringLiteral("Текущие значения"), parent);
    auto *grid   = new QGridLayout(group);

    auto mkLabel = [&](const QString &caption, QLabel *&lbl, int row, int col) {
        grid->addWidget(new QLabel(caption), row, col);
        lbl = new QLabel(QStringLiteral("—"));
        lbl->setMinimumWidth(80);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(lbl, row, col + 1);
    };

    mkLabel(QStringLiteral("Обороты:"),    m_lblRpm,     0, 0);
    mkLabel(QStringLiteral("Момент:"),     m_lblTorque,  1, 0);
    mkLabel(QStringLiteral("Темп. ДВС:"),  m_lblEngTemp, 2, 0);
    mkLabel(QStringLiteral("Давл. масла:"),m_lblOilPrs,  3, 0);
    mkLabel(QStringLiteral("Давл. топл.:"),m_lblFuelPrs, 4, 0);

    mkLabel(QStringLiteral("Наддув:"),     m_lblBoost,   0, 2);
    mkLabel(QStringLiteral("Темп. ЭД:"),   m_lblDynoTemp,1, 2);
    mkLabel(QStringLiteral("Темп. рез.:"), m_lblResTemp, 2, 2);
    mkLabel(QStringLiteral("Масло, %:"),   m_lblOilLevel,3, 2);
    mkLabel(QStringLiteral("Топливо, %:"), m_lblFuelLevel,4, 2);

    grid->addWidget(new QLabel(QStringLiteral("Состояние:")), 5, 0);
    m_lblState = new QLabel(QStringLiteral("Ожидание"));
    m_lblState->setStyleSheet(QStringLiteral("font-weight:bold;color:gray;"));
    grid->addWidget(m_lblState, 5, 1);

    grid->addWidget(new QLabel(QStringLiteral("Дроссель:")), 5, 2);
    m_lblThrottle = new QLabel(QStringLiteral("0 %"));
    grid->addWidget(m_lblThrottle, 5, 3);

    auto *botLayout = qobject_cast<QHBoxLayout*>(parent->layout());
    if (botLayout) botLayout->addWidget(group);
}
void MainWindow::onThrottleChanged(int value)
{
    m_lblThrValue->setText(QString::number(value) + " %");
    if (m_chkManualThr->isChecked()) {
        // TODO: Вызвать контроллер при интеграции
        // m_controller->setThrottleManual(value);
    }
}

void MainWindow::onManualThrottleToggled(bool checked)
{
    m_sliderThr->setEnabled(checked);
    if (!checked) {
        // TODO: Вернуть автоматический режим
    }
}
