#pragma once
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include "datatypes.h"

class ActuatorWindow : public QDialog {
    Q_OBJECT
public:
    explicit ActuatorWindow(QWidget *parent = nullptr);
    void updateFeedback(const ActuatorFeedback &fb);
private:
    QLabel *m_lblThrottle, *m_lblSpeed, *m_lblTorque;
    QLabel *m_lblEngFan, *m_lblDynoFan, *m_lblResFan;
};
