#include "actuator_window.h"
#include <QtWidgets/QGridLayout>

ActuatorWindow::ActuatorWindow(HighLevelController *ctrl, QWidget *parent)
    : QDialog(parent), m_ctrl(ctrl)
{
    setWindowTitle(QStringLiteral("Исполнительные механизмы"));
    setMinimumWidth(300);
    auto *grid = new QGridLayout(this);

    int row = 0;
    auto addRow = [&](const QString &label, QLabel *&lbl) {
        grid->addWidget(new QLabel(label), row, 0);
        lbl = new QLabel(QStringLiteral("—"));
        lbl->setAlignment(Qt::AlignRight);
        grid->addWidget(lbl, row++, 1);
    };

    addRow(QStringLiteral("Дроссель, %:"),      m_lblThrottle);
    addRow(QStringLiteral("Скорость ЭД, об/мин:"),m_lblSpeed);
    addRow(QStringLiteral("Момент ЭД, Н·м:"),   m_lblTorque);
    addRow(QStringLiteral("Вентилятор ДВС:"),   m_lblEngFan);
    addRow(QStringLiteral("Вентилятор ЭД:"),    m_lblDynoFan);
    addRow(QStringLiteral("Вентилятор рез.:"),  m_lblResFan);
}

void ActuatorWindow::updateFeedback(const ActuatorFeedback &fb)
{
    m_lblThrottle->setText(QString::number(fb.throttle_actual, 'f', 1) + " %");
    m_lblSpeed->setText(QString::number(fb.dyno_speed_actual, 'f', 1) + " об/мин");
    m_lblTorque->setText(QString::number(fb.dyno_torque_actual, 'f', 1) + " Н·м");
    m_lblEngFan->setText(fb.engine_fan_actual    ? "ВКЛ" : "ВЫКЛ");
    m_lblDynoFan->setText(fb.dyno_motor_fan_actual? "ВКЛ" : "ВЫКЛ");
    m_lblResFan->setText(fb.resistor_fan_actual  ? "ВКЛ" : "ВЫКЛ");
}
