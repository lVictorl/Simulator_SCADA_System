#pragma once
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QCheckBox>
#include "datatypes.h"

class HighLevelController;

/** Диалог отображения/управления исполнительными механизмами. */
class ActuatorWindow : public QDialog
{
    Q_OBJECT
public:
    explicit ActuatorWindow(HighLevelController *ctrl, QWidget *parent = nullptr);
    void updateFeedback(const ActuatorFeedback &fb);

private:
    QLabel *m_lblThrottle = nullptr;
    QLabel *m_lblSpeed    = nullptr;
    QLabel *m_lblTorque   = nullptr;
    QLabel *m_lblEngFan   = nullptr;
    QLabel *m_lblDynoFan  = nullptr;
    QLabel *m_lblResFan   = nullptr;

    HighLevelController *m_ctrl;
};
