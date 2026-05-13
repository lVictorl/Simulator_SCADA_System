/**
 * @file main.cpp
 * @brief Точка входа SCADA-системы обкатки дизельного двигателя.
 *
 * Порядок инициализации важен:
 *
 *  1. QApplication          — Qt event loop
 *  2. registerMetaTypes()   — ДО создания любых потоков!
 *  3. VirtualMCU            — физика и конечный автомат
 *  4. ModbusSlaveAdapter    — ведомое Modbus-устройство
 *  5. EmulatedModbusTransport — связь мастер ↔ ведомый
 *  6. ModbusMasterAdapter   — ведущее Modbus-устройство
 *  7. HighLevelController   — API для GUI
 *  8. DataLogger + SessionHistory — хранение данных
 *  9. Запуск потоков (slave → mcu → master, порядок важен!)
 * 10. MainWindow            — главное окно
 * 11. app.exec()            — цикл событий Qt
 * 12. Корректное завершение (обратный порядок)
 *
 * Диаграмма владения объектами:
 *   main владеет всеми объектами (сырые указатели)
 *   QThread-объекты удаляются после wait()
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
    // ── Qt Application ────────────────────────────────────────────────────────
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("EngineSCADA"));
    app.setApplicationVersion(QStringLiteral("1.0"));
    app.setOrganizationName(QStringLiteral("Lab"));

    // ── ВАЖНО: регистрация мета-типов ДО создания потоков ────────────────────
    // Qt использует мета-систему для передачи объектов через Qt::QueuedConnection.
    // Без регистрации сигнал telemetryReady(MCUTelemetry) не будет работать
    // между потоками — Qt не знает как скопировать MCUTelemetry в очередь.
    registerMetaTypes();

    // Создаём папку для логов если не существует
    QDir().mkpath(QStringLiteral("logs"));

    // ── 1. Виртуальный МКУ (ведомое устройство) ──────────────────────────────
    // slaveId=1, dt=0.02с (50 Гц симуляция)
    auto *mcu = new VirtualMCU(nullptr, 1, 0.02);

    // ── 2. Modbus slave (обрабатывает входящие кадры от мастера) ─────────────
    auto *slave = new ModbusSlaveAdapter(1);

    // Двусторонняя связь MCU ↔ Slave:
    //   slave → MCU: команды (slave::handleWrite → mcu::enqueueCommand)
    //   MCU → slave: телеметрия (mcu::sendTelemetry → slave::updateTelemetry)
    slave->setMCU(mcu);
    mcu->setSlaveAdapter(slave);

    // ── 3. Транспортный уровень ───────────────────────────────────────────────
    // EmulatedModbusTransport: мастер и slave соединены напрямую (без COM-порта).
    // Для реального железа замените на:
    //   auto *transport = new SerialModbusTransport("/dev/ttyUSB0", 115200);
    //   transport->open();
    auto *transport = new EmulatedModbusTransport(slave);

    // ── 4. Modbus master (опрашивает slave каждые 50 мс) ─────────────────────
    auto *master = new ModbusMasterAdapter(transport, {1});

    // ── 5. Высокоуровневый контроллер (API для GUI) ───────────────────────────
    auto *controller = new HighLevelController(master, 1);

    // ── 6. Хранение данных ────────────────────────────────────────────────────
    auto *logger  = new DataLogger(QStringLiteral("logs"));
    auto *history = new SessionHistory(QStringLiteral("logs"));

    // ── 7. Запуск потоков (порядок критичен!) ─────────────────────────────────
    // slave запускается первым — MCU сразу может слать телеметрию
    slave->start();
    // MCU запускается вторым — slave уже готов принять данные
    mcu->start();
    // master запускается последним — начинает опрашивать уже работающий slave
    master->start();

    // ── 8. Главное окно ───────────────────────────────────────────────────────
    MainWindow win(controller, master, logger, history);
    win.show();

    // ── Цикл событий Qt ───────────────────────────────────────────────────────
    const int ret = app.exec();

    // ── Корректное завершение (обратный порядок запуска!) ────────────────────
    // 1. Останавливаем мастер — больше не посылаем запросы
    master->stop();
    // 2. Останавливаем MCU — больше не обрабатываем команды и физику
    mcu->stopMCU();
    // 3. Останавливаем slave — больше не обрабатываем Modbus-кадры
    slave->stopAdapter();

    // Ждём реального завершения потоков (максимум 3 секунды каждый)
    mcu->wait(3000);
    slave->wait(3000);

    // Освобождаем память
    delete controller;
    delete master;
    delete transport;
    delete slave;
    delete mcu;
    delete logger;
    delete history;

    return ret;
}
