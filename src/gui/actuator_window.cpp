#include "actuator_window.h"
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>

ActuatorWindow::ActuatorWindow(HighLevelController *ctrl, QWidget *parent)
    : QDialog(parent), m_ctrl(ctrl)
{
    setWindowTitle(QStringLiteral("Исполнительные механизмы"));
    setMinimumWidth(360);
    auto *mainLayout = new QVBoxLayout(this);

    // ── Группа: дроссель и ЭД ────────────────────────────────────────────────
    auto *grpActuators = new QGroupBox(QStringLiteral("Приводы"), this);
    auto *grid = new QGridLayout(grpActuators);

    int row = 0;
    auto addRow = [&](const QString &label, QLabel *&lbl, const QString &unit = QString()) {
        grid->addWidget(new QLabel(label), row, 0);
        lbl = new QLabel(QStringLiteral("—"));
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lbl->setMinimumWidth(100);
        if (!unit.isEmpty()) {
            auto *unitLbl = new QLabel(unit);
            unitLbl->setStyleSheet(QStringLiteral("color: gray;"));
            grid->addWidget(lbl, row, 1);
            grid->addWidget(unitLbl, row, 2);
        } else {
            grid->addWidget(lbl, row, 1, 1, 2);
        }
        ++row;
    };

    addRow(QStringLiteral("Дроссель факт.:"),      m_lblThrottle,   "%");
    addRow(QStringLiteral("Скорость ЭД факт.:"),   m_lblSpeed,      "об/мин");
    addRow(QStringLiteral("Момент ЭД факт.:"),      m_lblTorque,     "Н·м");
    addRow(QStringLiteral("Режим ЭД:"),             m_lblDynoMode);
    mainLayout->addWidget(grpActuators);

    // ── Группа: вентиляторы ───────────────────────────────────────────────────
    auto *grpFans = new QGroupBox(QStringLiteral("Вентиляторы"), this);
    auto *gridF = new QGridLayout(grpFans);
    row = 0;

    auto addFanRow = [&](const QString &label, QLabel *&lbl) {
        gridF->addWidget(new QLabel(label), row, 0);
        lbl = new QLabel(QStringLiteral("ВЫКЛ"));
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lbl->setMinimumWidth(60);
        gridF->addWidget(lbl, row, 1);
        ++row;
    };

    addFanRow(QStringLiteral("Вентилятор ДВС:"),       m_lblEngFan);
    addFanRow(QStringLiteral("Вентилятор ЭД:"),        m_lblDynoFan);
    addFanRow(QStringLiteral("Вентилятор резистора:"), m_lblResFan);
    mainLayout->addWidget(grpFans);

    mainLayout->addStretch();
}

void ActuatorWindow::onTelemetryReceived(const MCUTelemetry &tele)
{
    updateFeedback(tele.actuator_feedback);

    // Режим ЭД из телеметрии
    m_lblDynoMode->setText(tele.dyno_mode == DynoMotorMode::SPIN
                            ? QStringLiteral("Прокрутка")
                            : QStringLiteral("Торможение"));
}

void ActuatorWindow::updateFeedback(const ActuatorFeedback &fb)
{
    auto fmt1 = [](double v) { return QString::number(v, 'f', 1); };

    m_lblThrottle->setText(fmt1(fb.throttle_actual));
    m_lblSpeed->setText(fmt1(fb.dyno_speed_actual));
    m_lblTorque->setText(fmt1(fb.dyno_torque_actual));

    auto fanLabel = [](bool on) -> QString {
        return on ? QStringLiteral("<b style='color:green'>ВКЛ</b>")
                  : QStringLiteral("<span style='color:gray'>ВЫКЛ</span>");
    };

    m_lblEngFan->setText(fanLabel(fb.engine_fan_actual));
    m_lblEngFan->setTextFormat(Qt::RichText);

    m_lblDynoFan->setText(fanLabel(fb.dyno_motor_fan_actual));
    m_lblDynoFan->setTextFormat(Qt::RichText);

    m_lblResFan->setText(fanLabel(fb.resistor_fan_actual));
    m_lblResFan->setTextFormat(Qt::RichText);
}
