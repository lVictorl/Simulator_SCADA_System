#include "actuator_window.h"
#include <QtWidgets/QVBoxLayout>

ActuatorWindow::ActuatorWindow(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Исполнительные механизмы");
    auto *layout = new QVBoxLayout(this);
    m_lblThrottle = new QLabel("Заслонка: —");
    layout->addWidget(m_lblThrottle);
    // ... остальные метки
}
void ActuatorWindow::updateFeedback(const ActuatorFeedback &fb) {
    m_lblThrottle->setText(QString("Заслонка: %1 %").arg(fb.throttle_actual));
}
