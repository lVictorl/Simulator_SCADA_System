#include "fault_injection_dialog.h"
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QPushButton>

FaultInjectionDialog::FaultInjectionDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Инжекция отказов");
    auto *layout = new QVBoxLayout(this);
    m_sensorCombo = new QComboBox;
    m_sensorCombo->addItem("Обороты (0)");
    m_faultCombo = new QComboBox;
    m_faultCombo->addItem("Обрыв");
    layout->addWidget(m_sensorCombo);
    layout->addWidget(m_faultCombo);
    layout->addWidget(new QPushButton("Внести"));
}
