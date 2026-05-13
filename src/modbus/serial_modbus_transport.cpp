#include "serial_modbus_transport.h"

#ifdef HAVE_SERIAL_PORT

#include <QtCore/QElapsedTimer>

SerialModbusTransport::SerialModbusTransport(const QString &portName,
                                              qint32 baudRate,
                                              QObject *parent)
    : IModbusTransport(parent)
{
    m_port.setPortName(portName);
    m_port.setBaudRate(baudRate);
    m_port.setDataBits(QSerialPort::Data8);
    m_port.setParity(QSerialPort::NoParity);
    m_port.setStopBits(QSerialPort::OneStop);
    m_port.setFlowControl(QSerialPort::NoFlowControl);
}

SerialModbusTransport::~SerialModbusTransport() { close(); }
bool SerialModbusTransport::open()   { return m_port.open(QIODevice::ReadWrite); }
void SerialModbusTransport::close()  { if (m_port.isOpen()) m_port.close(); }
bool SerialModbusTransport::isOpen() const { return m_port.isOpen(); }

void SerialModbusTransport::sendFrame(const QByteArray &frame)
{
    if (m_port.isOpen()) m_port.write(frame);
}

bool SerialModbusTransport::receiveFrame(QByteArray &out, int timeoutMs)
{
    QElapsedTimer timer; timer.start();
    out.clear();
    while (timer.elapsed() < timeoutMs) {
        if (m_port.waitForReadyRead(20)) out.append(m_port.readAll());
        if (out.size() >= 4) return true;
    }
    return !out.isEmpty();
}

#endif // HAVE_SERIAL_PORT
