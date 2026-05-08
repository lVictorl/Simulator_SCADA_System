#pragma once
/**
 * serial_modbus_transport.h — транспорт на базе QSerialPort.
 * Компилируется только если Qt6SerialPort найден (HAVE_SERIAL_PORT).
 * При переходе на реальное железо — замените EmulatedModbusTransport
 * этим классом, не меняя остальной код.
 */

#ifdef HAVE_SERIAL_PORT

#include "emulated_modbus_transport.h"
#include <QtSerialPort/QSerialPort>

class SerialModbusTransport : public IModbusTransport
{
    Q_OBJECT
public:
    SerialModbusTransport(const QString &portName,
                          qint32 baudRate = 115200,
                          QObject *parent = nullptr);
    ~SerialModbusTransport() override;

    bool open();
    void close();
    bool isOpen() const;

    void sendFrame(const QByteArray &frame)                  override;
    bool receiveFrame(QByteArray &out, int timeoutMs = 500)  override;

private:
    QSerialPort m_port;
};

#endif // HAVE_SERIAL_PORT
