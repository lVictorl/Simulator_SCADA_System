#pragma once
/**
 * modbus_master_adapter.h — Modbus RTU мастер.
 *
 * - Отправка MCUCommand (функция 0x10).
 * - Периодическое чтение телеметрии (функция 0x03).
 * - Watchdog: обнаружение потери связи.
 * - Сигнал telemetryReady(MCUTelemetry) для GUI (Qt::QueuedConnection).
 */

#include <QtCore/QObject>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <QtCore/QMap>
#include "datatypes.h"
#include "modbus_utils.h"

class IModbusTransport;

class ModbusMasterAdapter : public QObject
{
    Q_OBJECT
public:
    explicit ModbusMasterAdapter(IModbusTransport *transport,
                                 QList<int> slaveIds = {1},
                                 QObject *parent     = nullptr);
    ~ModbusMasterAdapter() override;

    void start();
    void stop();

    /** Отправить команду (потокобезопасно, с повторами). */
    void sendCommand(const MCUCommand &cmd);

    /** Запросить телеметрию от одного устройства (потокобезопасно). */
    MCUTelemetry readTelemetry(int slaveId);

    int defaultSlave() const { return m_slave_ids.isEmpty() ? 1 : m_slave_ids.first(); }

signals:
    void telemetryReady(const MCUTelemetry &tele);
    void connectionLost(int slaveId);
    void connectionRestored(int slaveId);

private slots:
    void pollAll();

private:
    QByteArray   buildCommandFrame(const MCUCommand &cmd) const;
    MCUTelemetry decodeTelemetry(const QByteArray &payload, int slaveId) const;

    IModbusTransport *m_transport;
    QList<int>        m_slave_ids;
    QTimer           *m_poll_timer    = nullptr;
    QThread          *m_worker_thread = nullptr;
    QMutex            m_transport_mutex; ///< защищает sendFrame/receiveFrame

    QMap<int, int>  m_fail_counts;
    QMap<int, bool> m_connection_ok;

    static constexpr int MAX_RETRIES       = 3;
    static constexpr int WATCHDOG_MAX_FAIL = 3;
    static constexpr int POLL_INTERVAL_MS  = 50;

    static constexpr int TELE_START_REG = 100;
    static constexpr int TELE_REG_COUNT =  33;
    static constexpr int CMD_START_REG  =   0;
    static constexpr int CMD_REG_COUNT  =  32;
};
