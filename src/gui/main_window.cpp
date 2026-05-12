#include "main_window.h"
#include "high_level_controller.h"
#include "modbus_master_adapter.h"
#include "data_logger.h"
#include "session_history.h"
#include "reporter.h"
#include "actuator_window.h"
#include "history_window.h"
#include "fault_injection_dialog.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLineEdit>
#include <QtCore/QDateTime>
#include <QtCore/QElapsedTimer>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────

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
    setupMenuBar();
    updateUIState(BreakInState::IDLE);

    // Телеметрия приходит через сигнал Qt::QueuedConnection из рабочего потока
    connect(master, &ModbusMasterAdapter::telemetryReady,
            this,   &MainWindow::onTelemetryReceived,
            Qt::QueuedConnection);
    connect(master, &ModbusMasterAdapter::connectionLost,
            this,   &MainWindow::onConnectionLost,
            Qt::QueuedConnection);
    connect(master, &ModbusMasterAdapter::connectionRestored,
            this,   &MainWindow::onConnectionRestored,
            Qt::QueuedConnection);
}

MainWindow::~MainWindow()
{
    delete m_reporter;
}

// ─── UI построение ───────────────────────────────────────────────────────────

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);

    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    // Верхняя панель: управление + уставки
    // Layout создаётся ДО вызова setupControlPanel/setupSessionPanel
    auto *topWidget = new QWidget;
    auto *topLayout = new QHBoxLayout(topWidget);  // ← layout уже есть
    topLayout->setContentsMargins(0, 0, 0, 0);
    setupControlPanel(topWidget);
    setupSessionPanel(topWidget);
    mainLayout->addWidget(topWidget);

    // Разделитель: графики + показания + журнал
    auto *splitter = new QSplitter(Qt::Vertical);

    auto *plotWidget = new QWidget;
    setupPlots(plotWidget);
    splitter->addWidget(plotWidget);

    // Layout создаётся ДО вызова setupReadingsPanel/setupEventLog
    auto *bottomWidget = new QWidget;
    auto *bottomLayout = new QHBoxLayout(bottomWidget);  // ← layout уже есть
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    setupReadingsPanel(bottomWidget);
    setupEventLog(bottomWidget);
    splitter->addWidget(bottomWidget);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter, 1);

    // Строка состояния
    m_lblStatus = new QLabel(QStringLiteral("Готов"));
    statusBar()->addPermanentWidget(m_lblStatus);
}

void MainWindow::setupMenuBar()
{
    auto *mb = menuBar();

    auto *fileMenu = mb->addMenu(QStringLiteral("Файл"));
    fileMenu->addAction(QStringLiteral("История испытаний"),
                        this, &MainWindow::onOpenHistory);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Выход"),
                        qApp, &QApplication::quit);

    auto *toolsMenu = mb->addMenu(QStringLiteral("Инструменты"));
    toolsMenu->addAction(QStringLiteral("Управление исполнительными механизмами"),
                         this, &MainWindow::onOpenActuators);
    toolsMenu->addAction(QStringLiteral("Инжекция отказов"),
                         this, &MainWindow::onOpenFaultInjection);
}

void MainWindow::setupControlPanel(QWidget *parent)
{
    auto *group = new QGroupBox(QStringLiteral("Управление"), parent);
    auto *layout = new QVBoxLayout(group);

    // Режим
    auto *modeRow = new QHBoxLayout;
    modeRow->addWidget(new QLabel(QStringLiteral("Режим:")));
    m_modeCombo = new QComboBox;
    m_modeCombo->addItem(QStringLiteral("Холодная обкатка"),    QVariant::fromValue(BreakInMode::COLD));
    m_modeCombo->addItem(QStringLiteral("Горячая без нагрузки"),QVariant::fromValue(BreakInMode::HOT_NOLOAD));
    m_modeCombo->addItem(QStringLiteral("Горячая под нагрузкой"),QVariant::fromValue(BreakInMode::HOT_LOAD));
    modeRow->addWidget(m_modeCombo);
    layout->addLayout(modeRow);

    // Параметры этапа
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

    // Кнопки
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

    // Ручное управление дросселем
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

    // Добавляем группу в родительский layout
    auto *topLayout = qobject_cast<QHBoxLayout*>(parent->layout());
    if (topLayout) topLayout->addWidget(group);
}

void MainWindow::setupSessionPanel(QWidget *parent)
{
    auto *group = new QGroupBox(QStringLiteral("Критические уставки"), parent);
    auto *grid  = new QGridLayout(group);

    auto mkSpin = [](double lo, double hi, double def, const QString &suffix = QString()) {
        auto *s = new QDoubleSpinBox;
        s->setRange(lo, hi); s->setValue(def);
        if (!suffix.isEmpty()) s->setSuffix(QStringLiteral(" ") + suffix);
        return s;
    };

    int row = 0;
    grid->addWidget(new QLabel(QStringLiteral("Макс. темп. ДВС:")), row, 0);
    m_limitEngTemp  = mkSpin(50, 150, 95, "°C");    grid->addWidget(m_limitEngTemp, row++, 1);

    grid->addWidget(new QLabel(QStringLiteral("Давление масла мин.:")), row, 0);
    m_limitOilMin   = mkSpin(0, 5, 0.8, "бар");     grid->addWidget(m_limitOilMin, row++, 1);

    grid->addWidget(new QLabel(QStringLiteral("Давление масла макс.:")), row, 0);
    m_limitOilMax   = mkSpin(0, 20, 6.0, "бар");    grid->addWidget(m_limitOilMax, row++, 1);

    grid->addWidget(new QLabel(QStringLiteral("Давление топлива мин.:")), row, 0);
    m_limitFuelMin  = mkSpin(0, 10, 2.0, "бар");    grid->addWidget(m_limitFuelMin, row++, 1);

    grid->addWidget(new QLabel(QStringLiteral("Давление топлива макс.:")), row, 0);
    m_limitFuelMax  = mkSpin(0, 15, 5.0, "бар");    grid->addWidget(m_limitFuelMax, row++, 1);

    grid->addWidget(new QLabel(QStringLiteral("Макс. наддув:")), row, 0);
    m_limitBoost    = mkSpin(0, 10, 2.5, "бар");    grid->addWidget(m_limitBoost, row++, 1);

    grid->addWidget(new QLabel(QStringLiteral("Макс. обороты:")), row, 0);
    m_limitRpm      = mkSpin(500, 8000, 4000, "об/мин"); grid->addWidget(m_limitRpm, row++, 1);

    grid->addWidget(new QLabel(QStringLiteral("Макс. темп. ЭД:")), row, 0);
    m_limitDynoTemp = mkSpin(50, 300, 150, "°C");   grid->addWidget(m_limitDynoTemp, row++, 1);

    grid->addWidget(new QLabel(QStringLiteral("Макс. темп. резистора:")), row, 0);
    m_limitResTemp  = mkSpin(50, 400, 200, "°C");   grid->addWidget(m_limitResTemp, row++, 1);

    auto *topLayout = qobject_cast<QHBoxLayout*>(parent->layout());
    if (topLayout) topLayout->addWidget(group);
}

void MainWindow::setupPlots(QWidget *parent)
{
    auto *layout = new QGridLayout(parent);
    layout->setContentsMargins(2, 2, 2, 2);

    auto makePlot = [&](const QString &yLabel, const QColor &color) {
        auto *p = new QCustomPlot;
        p->addGraph();
        p->graph(0)->setPen(QPen(color, 1.5));
        p->xAxis->setLabel(QStringLiteral("t, с"));
        p->yAxis->setLabel(yLabel);
        p->setMinimumHeight(160);
        return p;
    };

    m_plotRpm     = makePlot(QStringLiteral("об/мин"), Qt::blue);
    m_plotTorque  = makePlot(QStringLiteral("Н·м"),    Qt::darkGreen);
    m_plotEngTemp = makePlot(QStringLiteral("°C"),     Qt::red);
    m_plotOilPrs  = makePlot(QStringLiteral("бар"),    Qt::darkCyan);
    m_plotDynoTemp= makePlot(QStringLiteral("°C"),     Qt::magenta);
    m_plotResTemp = makePlot(QStringLiteral("°C"),     QColor(180,90,0));

    layout->addWidget(m_plotRpm,     0, 0);
    layout->addWidget(m_plotTorque,  0, 1);
    layout->addWidget(m_plotEngTemp, 0, 2);
    layout->addWidget(m_plotOilPrs,  1, 0);
    layout->addWidget(m_plotDynoTemp,1, 1);
    layout->addWidget(m_plotResTemp, 1, 2);
}

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

void MainWindow::setupEventLog(QWidget *parent)
{
    auto *group  = new QGroupBox(QStringLiteral("Журнал событий"), parent);
    auto *layout = new QVBoxLayout(group);
    m_eventLog   = new QTextEdit;
    m_eventLog->setReadOnly(true);
    m_eventLog->setMaximumHeight(200);
    layout->addWidget(m_eventLog);

    auto *botLayout = qobject_cast<QHBoxLayout*>(parent->layout());
    if (botLayout) botLayout->addWidget(group);
}

// ─── Slots ────────────────────────────────────────────────────────────────────

void MainWindow::onStartClicked()
{
    bool ok;
    const QString op = QInputDialog::getText(
        this, QStringLiteral("Оператор"),
        QStringLiteral("Введите имя оператора:"),
        QLineEdit::Normal, m_operatorName, &ok);
    if (!ok) return;
    m_operatorName = op;

    const BreakInMode mode = m_modeCombo->currentData().value<BreakInMode>();
    const CriticalLimits limits = collectLimits();

    try {
        m_controller->start(mode, m_durationSpin->value(),
                            m_targetSpin->value(), limits,
                            m_warmupSpin->value());

        // Открываем CSV-сессию
        m_sessionData.clear();
        m_sessionTimestamps.clear();
        m_sessionStartTime = QDateTime::currentSecsSinceEpoch();
        m_plotTime.clear();
        m_plotRpmData.clear(); m_plotTorqueData.clear();
        m_plotEngTempData.clear(); m_plotOilPrsData.clear();
        m_plotDynoTempData.clear(); m_plotResTempData.clear();

        const QString fname = QDateTime::currentDateTime()
                              .toString(QStringLiteral("yyyyMMdd_hhmmss")) + ".csv";
        SessionInfo si;
        si.operator_name    = m_operatorName;
        si.mode             = mode;
        si.duration         = m_durationSpin->value();
        si.warmup_duration  = m_warmupSpin->value();
        si.target           = m_targetSpin->value();
        si.limits           = limits;
        si.start_time       = m_sessionStartTime;
        m_lastTelemetry     = MCUTelemetry{};
        m_logger->startSession(fname, si);
        m_sessionActive = true;

        appendLog(QStringLiteral("Сессия начата. Режим: ") + breakInModeDisplayName(mode));
    } catch (const std::exception &e) {
        QMessageBox::critical(this, QStringLiteral("Ошибка"), QString::fromLocal8Bit(e.what()));
    }
}

void MainWindow::onStopClicked()
{
    try { m_controller->stop(); }
    catch (...) {}
    appendLog(QStringLiteral("Команда СТОП"));
}

void MainWindow::onEmergencyClicked()
{
    try { m_controller->emergencyStop(); }
    catch (...) {}
    appendLog(QStringLiteral("⚠ АВАРИЙНАЯ ОСТАНОВКА"), true);
}

void MainWindow::onNextStageClicked()
{
    try { m_controller->nextStage(); }
    catch (...) {}
    appendLog(QStringLiteral("Ручной переход на следующий этап"));
}

void MainWindow::onResetEmergencyClicked()
{
    try { m_controller->resetEmergency(); }
    catch (...) {}
    appendLog(QStringLiteral("Сброс аварии"));
}

void MainWindow::onThrottleChanged(int value)
{
    m_lblThrValue->setText(QString::number(value) + QStringLiteral(" %"));
    if (m_chkManualThr->isChecked()) {
        try { m_controller->setThrottleManual(value); }
        catch (...) {}
    }
}

void MainWindow::onManualThrottleToggled(bool checked)
{
    m_sliderThr->setEnabled(checked);
    if (!checked) {
        try { m_controller->setThrottleAuto(); }
        catch (...) {}
    }
}

void MainWindow::onOpenActuators()
{
    // ActuatorWindow создаётся по требованию
    auto *dlg = new ActuatorWindow(m_controller, this);
    connect(m_master, auto *dlg = new ActuatorWindow(m_controller, this);ModbusMasterAdapter::telemetryReady, dlg, auto *dlg = new ActuatorWindow(m_controller, this);ActuatorWindow::updateFeedback, Qt::QueuedConnection);
    if (m_lastTelemetry.sensors.timestamp != 0.0) dlg->updateFeedback(m_lastTelemetry.actuator_feedback);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onOpenHistory()
{
    auto *dlg = new HistoryWindow(m_history, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onOpenFaultInjection()
{
    auto *dlg = new FaultInjectionDialog(m_controller, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

// ─── Телеметрия ───────────────────────────────────────────────────────────────

void MainWindow::onTelemetryReceived(const MCUTelemetry &tele)
{
    m_lastTelemetry = tele;
    updateUIState(tele.state);
    updateReadings(tele.sensors, tele);

    // Журнал ошибок/предупреждений
    for (const auto &err : tele.errors)
        appendLog(QStringLiteral("⛔ ") + err, true);
    for (const auto &warn : tele.warnings)
        appendLog(QStringLiteral("⚠ ") + warn);

    // Обновление графиков
    const double t = tele.sensors.timestamp;
    m_plotTime          << t;
    m_plotRpmData       << tele.sensors.engine_rpm;
    m_plotTorqueData    << tele.sensors.torque;
    m_plotEngTempData   << tele.sensors.engine_temp;
    m_plotOilPrsData    << tele.sensors.oil_pressure;
    m_plotDynoTempData  << tele.sensors.dyno_motor_temp;
    m_plotResTempData   << tele.sensors.resistor_temp;

    // Ограничиваем длину буферов
    while (m_plotTime.size() > MAX_PLOT_POINTS) {
        m_plotTime.removeFirst();
        m_plotRpmData.removeFirst();      m_plotTorqueData.removeFirst();
        m_plotEngTempData.removeFirst();  m_plotOilPrsData.removeFirst();
        m_plotDynoTempData.removeFirst(); m_plotResTempData.removeFirst();
    }

    auto updatePlot = [](QCustomPlot *p, const QVector<double> &x, const QVector<double> &y) {
        p->graph(0)->setData(x, y);
        if (!x.isEmpty()) {
            p->xAxis->setRange(x.first(), x.last());
            double lo = *std::min_element(y.begin(), y.end());
            double hi = *std::max_element(y.begin(), y.end());
            const double margin = (hi - lo) * 0.1 + 1.0;
            p->yAxis->setRange(lo - margin, hi + margin);
        }
        p->replot(QCustomPlot::rpQueuedReplot);
    };

    updatePlot(m_plotRpm,     m_plotTime, m_plotRpmData);
    updatePlot(m_plotTorque,  m_plotTime, m_plotTorqueData);
    updatePlot(m_plotEngTemp, m_plotTime, m_plotEngTempData);
    updatePlot(m_plotOilPrs,  m_plotTime, m_plotOilPrsData);
    updatePlot(m_plotDynoTemp,m_plotTime, m_plotDynoTempData);
    updatePlot(m_plotResTemp, m_plotTime, m_plotResTempData);

    // Логирование CSV
    if (m_sessionActive && m_logger->isOpen()) {
        m_logger->log(tele);
        m_sessionData    << tele.sensors;
        m_sessionTimestamps << t;
    }

    // Проверяем завершение сессии
    if (m_sessionActive &&
        (tele.state == BreakInState::STOPPED ||
         tele.state == BreakInState::EMERGENCY))
    {
        finishSession(tele);
    }
}

void MainWindow::onConnectionLost(int slaveId)
{
    appendLog(QString(QStringLiteral("⛔ Потеряна связь с устройством %1")).arg(slaveId), true);
    m_lblStatus->setText(QStringLiteral("Нет связи"));
    m_lblStatus->setStyleSheet(QStringLiteral("color:red;"));
}

void MainWindow::onConnectionRestored(int slaveId)
{
    appendLog(QString(QStringLiteral("✓ Связь с устройством %1 восстановлена")).arg(slaveId));
    m_lblStatus->setText(QStringLiteral("Связь OK"));
    m_lblStatus->setStyleSheet(QStringLiteral("color:green;"));
}

// ─── Вспомогательные ─────────────────────────────────────────────────────────

void MainWindow::updateUIState(BreakInState state)
{
    const bool active = (state != BreakInState::IDLE &&
                         state != BreakInState::STOPPED &&
                         state != BreakInState::EMERGENCY);

    m_btnStart->setEnabled(!active);
    m_btnStop->setEnabled(active);
    m_btnNextStage->setEnabled(active);
    m_btnReset->setEnabled(state == BreakInState::EMERGENCY);
    m_modeCombo->setEnabled(!active);
    m_durationSpin->setEnabled(!active);
    m_warmupSpin->setEnabled(!active);
    m_targetSpin->setEnabled(!active);

    m_lblState->setText(breakInStateDisplayName(state));
    const QString style = (state == BreakInState::EMERGENCY)
        ? QStringLiteral("font-weight:bold;color:red;")
        : (active ? QStringLiteral("font-weight:bold;color:darkgreen;")
                  : QStringLiteral("font-weight:bold;color:gray;"));
    m_lblState->setStyleSheet(style);
}

void MainWindow::updateReadings(const SensorData &s, const MCUTelemetry &tele)
{
    auto fmt1 = [](double v) { return QString::number(v, 'f', 1); };
    auto fmt2 = [](double v) { return QString::number(v, 'f', 2); };

    m_lblRpm->setText(fmt1(s.engine_rpm) + QStringLiteral(" об/мин"));
    m_lblTorque->setText(fmt1(s.torque) + QStringLiteral(" Н·м"));
    m_lblEngTemp->setText(fmt1(s.engine_temp) + QStringLiteral(" °C"));
    m_lblOilPrs->setText(fmt2(s.oil_pressure) + QStringLiteral(" бар"));
    m_lblFuelPrs->setText(fmt2(s.fuel_pressure) + QStringLiteral(" бар"));
    m_lblBoost->setText(fmt2(s.boost_pressure) + QStringLiteral(" бар"));
    m_lblDynoTemp->setText(fmt1(s.dyno_motor_temp) + QStringLiteral(" °C"));
    m_lblResTemp->setText(fmt1(s.resistor_temp) + QStringLiteral(" °C"));
    m_lblOilLevel->setText(fmt1(s.oil_level) + QStringLiteral(" %"));
    m_lblFuelLevel->setText(fmt1(s.fuel_level) + QStringLiteral(" %"));
    m_lblThrottle->setText(fmt1(tele.throttle_pct) + QStringLiteral(" %"));
}

void MainWindow::appendLog(const QString &msg, bool isError)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"));
    const QString line = QString(QStringLiteral("[%1] %2")).arg(ts, msg);
    if (isError)
        m_eventLog->append(QStringLiteral("<span style='color:red;'>") + line + QStringLiteral("</span>"));
    else
        m_eventLog->append(line);
}

CriticalLimits MainWindow::collectLimits() const
{
    CriticalLimits lim;
    lim.max_engine_temp    = m_limitEngTemp->value();
    lim.min_oil_pressure   = m_limitOilMin->value();
    lim.max_oil_pressure   = m_limitOilMax->value();
    lim.min_fuel_pressure  = m_limitFuelMin->value();
    lim.max_fuel_pressure  = m_limitFuelMax->value();
    lim.max_boost_pressure = m_limitBoost->value();
    lim.max_engine_rpm     = m_limitRpm->value();
    lim.max_dyno_motor_temp= m_limitDynoTemp->value();
    lim.max_resistor_temp  = m_limitResTemp->value();
    return lim;
}

void MainWindow::finishSession(const MCUTelemetry &last)
{
    m_sessionActive = false;
    m_logger->close();

    // Формируем отчёт
    SessionInfo si;
    si.operator_name   = m_operatorName;
    si.mode            = last.mode;
    si.duration        = m_durationSpin->value();
    si.warmup_duration = m_warmupSpin->value();
    si.target          = m_targetSpin->value();
    si.limits          = collectLimits();
    si.start_time      = m_sessionStartTime;
    si.end_time        = QDateTime::currentSecsSinceEpoch();
    si.final_state     = last.state;

    const QString rptPath = QStringLiteral("logs/report_") +
        QDateTime::fromSecsSinceEpoch(static_cast<qint64>(si.start_time))
            .toString(QStringLiteral("yyyyMMdd_hhmmss")) + ".html";

    m_reporter->generate(si, m_sessionTimestamps, m_sessionData, rptPath);
    si.report_file = rptPath;
    m_history->addEntry(si, rptPath);

    appendLog(QStringLiteral("✓ Сессия завершена. Отчёт: ") + rptPath);
}

void MainWindow::processTelemetryQueue() {}
