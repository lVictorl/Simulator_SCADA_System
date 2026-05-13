#pragma once
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include "datatypes.h"

class HighLevelController;

class ActuatorWindow : public QDialog
{
    Q_OBJECT
public:
    explicit ActuatorWindow(HighLevelController *ctrl, QWidget *parent = nullptr);

public slots:
    void onTelemetryReceived(const MCUTelemetry &tele);

private:
    void updateFeedback(const ActuatorFeedback &fb);

    QLabel *m_lblThrottle  = nullptr;
    QLabel *m_lblSpeed     = nullptr;
    QLabel *m_lblTorque    = nullptr;
    QLabel *m_lblEngFan    = nullptr;
    QLabel *m_lblDynoFan   = nullptr;
    QLabel *m_lblResFan    = nullptr;
    QLabel *m_lblDynoMode  = nullptr;
    QLabel *m_lblEngRunning= nullptr;

    HighLevelController *m_ctrl;
};
