/**
 * main.cpp — точка входа SCADA-системы обкатки дизельного двигателя.
 *
 * Порядок создания компонентов:
 *  1. VirtualMCU  (поток симуляции)
 *  2. ModbusSlaveAdapter  (поток ведомого)
 *  3. EmulatedModbusTransport  (связь MCU ↔ мастер)
 *  4. ModbusMasterAdapter  (поток мастера)
 *  5. HighLevelController  (API для GUI)
 *  6. DataLogger, SessionHistory, Reporter
 *  7. MainWindow
 */

#include <QtWidgets/QApplication>
#include <QtCore/QDir>

#include "datatypes.h"
#include "virtual_mcu.h"
#include "modbus_slave_adapter.h"
#include "emulated_modbus_transport.h"
#include "modbus_master_adapter.h"
#include "high_level_controller.h"
#include "data_logger.h"
#include "session_history.h"
#include "reporter.h"
#include "main_window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("EngineSCADA"));
    app.setApplicationVersion(QStringLiteral("1.0"));
    app.setOrganizationName(QStringLiteral("Lab"));

    // Регистрируем все пользовательские типы для Qt мета-системы
    // (необходимо для Qt::QueuedConnection через границы потоков)
    registerMetaTypes();

    QDir().mkpath(QStringLiteral("logs"));

    // ── 1. Виртуальный МКУ (ведомый) ─────────────────────────────────────────
    auto *mcu = new VirtualMCU(nullptr, /*slaveId=*/1, /*dt=*/0.02);

    // ── 2. Modbus ведомый ─────────────────────────────────────────────────────
    auto *slave = new ModbusSlaveAdapter(1);
    slave->setMCU(mcu);
    mcu->setSlaveAdapter(slave);

    // ── 3. Транспорт ─────────────────────────────────────────────────────────
    auto *transport = new EmulatedModbusTransport(slave);

    // ── 4. Modbus мастер ──────────────────────────────────────────────────────
    auto *master = new ModbusMasterAdapter(transport, {1});

    // ── 5. Высокоуровневый контроллер ─────────────────────────────────────────
    auto *controller = new HighLevelController(master, 1);

    // ── 6. Логирование и история ──────────────────────────────────────────────
    auto *logger  = new DataLogger(QStringLiteral("logs"));
    auto *history = new SessionHistory(QStringLiteral("logs"));

    // ── 7. Запуск потоков ─────────────────────────────────────────────────────
    slave->start();
    mcu->start();
    master->start();

    // ── 8. Главное окно ───────────────────────────────────────────────────────
    MainWindow win(controller, master, logger, history);
    win.show();

    const int ret = app.exec();

    // ── Корректное завершение ─────────────────────────────────────────────────
    master->stop();
    mcu->stopMCU();
    slave->stopAdapter();

    mcu->wait(3000);
    slave->wait(3000);

    delete controller;
    delete master;
    delete transport;
    delete slave;
    delete mcu;
    delete logger;
    delete history;

    return ret;
}
