#include "fault_injection_dialog.h"
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QDialogButtonBox>

FaultInjectionDialog::FaultInjectionDialog(HighLevelController *ctrl, QWidget *parent)
    : QDialog(parent), m_ctrl(ctrl)
{
    setWindowTitle(QStringLiteral("Инжекция отказов датчиков"));
    auto *layout = new QVBoxLayout(this);
    auto *form   = new QFormLayout;

    m_sensorCombo = new QComboBox;
    m_sensorCombo->addItem(QStringLiteral("0 — Обороты"),          0);
    m_sensorCombo->addItem(QStringLiteral("1 — Момент"),            1);
    m_sensorCombo->addItem(QStringLiteral("2 — Температура ДВС"),   2);
    m_sensorCombo->addItem(QStringLiteral("3 — Давления"),          3);
    m_sensorCombo->addItem(QStringLiteral("4 — Температура ЭД"),    4);
    m_sensorCombo->addItem(QStringLiteral("5 — Температура рез."),  5);
    form->addRow(QStringLiteral("Датчик:"), m_sensorCombo);

    m_faultCombo = new QComboBox;
    m_faultCombo->addItem(QStringLiteral("Нет"),               QVariant::fromValue(SensorFaultType::NONE));
    m_faultCombo->addItem(QStringLiteral("Обрыв цепи"),        QVariant::fromValue(SensorFaultType::OPEN_CIRCUIT));
    m_faultCombo->addItem(QStringLiteral("Короткое замыкание"),QVariant::fromValue(SensorFaultType::SHORT_CIRCUIT));
    m_faultCombo->addItem(QStringLiteral("Уход диапазона"),    QVariant::fromValue(SensorFaultType::OUT_OF_RANGE));
    form->addRow(QStringLiteral("Тип отказа:"), m_faultCombo);
    layout->addLayout(form);

    auto *btnInject = new QPushButton(QStringLiteral("Внедрить отказ"));
    auto *btnClear  = new QPushButton(QStringLiteral("Сбросить отказ"));
    connect(btnInject, &QPushButton::clicked, this, &FaultInjectionDialog::onInject);
    connect(btnClear,  &QPushButton::clicked, this, &FaultInjectionDialog::onClear);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close);
    btns->addButton(btnInject, QDialogButtonBox::ActionRole);
    btns->addButton(btnClear,  QDialogButtonBox::ActionRole);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(btns);
}

void FaultInjectionDialog::onInject()
{
    const int idx = m_sensorCombo->currentData().toInt();
    const auto ft = m_faultCombo->currentData().value<SensorFaultType>();
    try { m_ctrl->injectFault(idx, ft); }
    catch (...) {}
}

void FaultInjectionDialog::onClear()
{
    const int idx = m_sensorCombo->currentData().toInt();
    try { m_ctrl->clearFault(idx); }
    catch (...) {}
}
