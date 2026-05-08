#pragma once
/**
 * emulated_modbus_transport.h — эмулированный транспорт Modbus (без реального COM-порта).
 * Связывает мастер и ведомый через пары очередей (in-process).
 */

#include <QtCore/QObject>
#include <QtCore/QByteArray>

class ModbusSlaveAdapter;

/**
 * Интерфейс транспорта Modbus.
 * Реализации: EmulatedModbusTransport (для тестов) и SerialModbusTransport.
 */
class IModbusTransport : public QObject
{
    Q_OBJECT
public:
    explicit IModbusTransport(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~IModbusTransport() = default;

    virtual void sendFrame(const QByteArray &frame) = 0;

    /** Блокирующее ожидание ответа. timeout — мс. Возвращает false при тайм-ауте. */
    virtual bool receiveFrame(QByteArray &out, int timeoutMs = 500) = 0;
};

/**
 * Эмулированный транспорт: мастер и ведомый обмениваются через один SlaveAdapter.
 */
class EmulatedModbusTransport : public IModbusTransport
{
    Q_OBJECT
public:
    explicit EmulatedModbusTransport(ModbusSlaveAdapter *slave, QObject *parent = nullptr);

    void sendFrame(const QByteArray &frame)                      override;
    bool receiveFrame(QByteArray &out, int timeoutMs = 500)      override;

private:
    ModbusSlaveAdapter *m_slave;
};
