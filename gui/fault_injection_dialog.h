#pragma once
#include <QtWidgets/QDialog>
#include <QtWidgets/QComboBox>
#include "datatypes.h"

class FaultInjectionDialog : public QDialog {
    Q_OBJECT
public:
    explicit FaultInjectionDialog(QWidget *parent = nullptr);
private:
    QComboBox *m_sensorCombo;
    QComboBox *m_faultCombo;
};
