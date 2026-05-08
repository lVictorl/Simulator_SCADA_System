#include "emulated_modbus_transport.h"
#include "modbus_slave_adapter.h"
#include <QtCore/QThread>
#include <QtCore/QElapsedTimer>

EmulatedModbusTransport::EmulatedModbusTransport(ModbusSlaveAdapter *slave, QObject *parent)
    : IModbusTransport(parent), m_slave(slave)
{}

void EmulatedModbusTransport::sendFrame(const QByteArray &frame)
{
    if (m_slave) m_slave->receiveFrame(frame);
}

bool EmulatedModbusTransport::receiveFrame(QByteArray &out, int timeoutMs)
{
    if (!m_slave) return false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (m_slave->takeResponseFrame(out)) return true;
        QThread::msleep(2);
    }
    return false;
}
