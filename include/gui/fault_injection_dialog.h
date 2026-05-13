#pragma once
#include <QtWidgets/QDialog>
#include <QtWidgets/QComboBox>
#include "datatypes.h"
#include "high_level_controller.h"

class FaultInjectionDialog : public QDialog
{
    Q_OBJECT
public:
    FaultInjectionDialog(HighLevelController *ctrl, QWidget *parent = nullptr);

private slots:
    void onInject();
    void onClear();

private:
    QComboBox *m_sensorCombo = nullptr;
    QComboBox *m_faultCombo  = nullptr;
    HighLevelController *m_ctrl;
};
