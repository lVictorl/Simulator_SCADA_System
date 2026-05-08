#pragma once
/**
 * modbus_slave_adapter.h — Modbus RTU slave.
 *
 * - Принимает кадры из очереди запросов (req_queue).
 * - Проверяет CRC, сравнивает адрес.
 * - 0x10 (Write Multiple Registers): декодирует MCUCommand → очередь MCU.
 * - 0x03 (Read Holding Registers):   формирует ответ из последней телеметрии.
 * - Работает в отдельном QThread.
 */

#include <QtCore/QThread>
#include <QtCore/QQueue>
#include <QtCore/QMutex>
#include "datatypes.h"

class VirtualMCU;

class ModbusSlaveAdapter : public QThread
{
    Q_OBJECT
public:
    ModbusSlaveAdapter(int slaveAddr = 1, QObject *parent = nullptr);
    ~ModbusSlaveAdapter() override;

    void setMCU(VirtualMCU *mcu) { m_mcu = mcu; }

    /** Вызывается из VirtualMCU — обновляет последнюю телеметрию. */
    void updateTelemetry(const MCUTelemetry &tele);

    /** Подать входящий кадр (от мастера). */
    void receiveFrame(const QByteArray &frame);

    /** Получить следующий кадр ответа (для мастера). */
    bool takeResponseFrame(QByteArray &out);

    void stopAdapter();

protected:
    void run() override;

private:
    void processFrame(const QByteArray &frame);
    void handleWrite(quint8 addr, const QByteArray &payload); // 0x10
    void handleRead (quint8 addr, const QByteArray &payload); // 0x03
    QByteArray buildTelemetryRegisters() const;

    MCUCommand decodeCommand(const QByteArray &regs) const;

    int         m_slave_addr;
    VirtualMCU *m_mcu = nullptr;

    mutable QMutex   m_tele_mutex;
    MCUTelemetry     m_last_tele;
    bool             m_tele_valid = false;

    QMutex           m_req_mutex;
    QQueue<QByteArray> m_req_queue;

    QMutex           m_resp_mutex;
    QQueue<QByteArray> m_resp_queue;

    QAtomicInt m_stop_flag{0};
};
