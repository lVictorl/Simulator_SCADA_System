#include "modbus_utils.h"

namespace ModbusUtils {

void packScaledInt32(double value, quint8 *buf)
{
    qint32 scaled = static_cast<qint32>(value * 1000.0);
    buf[0] = (scaled >> 24) & 0xFF;
    buf[1] = (scaled >> 16) & 0xFF;
    buf[2] = (scaled >>  8) & 0xFF;
    buf[3] =  scaled        & 0xFF;
}

} // namespace ModbusUtils

double unpackScaledInt32(const quint8 *buf)
{
    qint32 scaled = (qint32(buf[0]) << 24) |
                    (qint32(buf[1]) << 16) |
                    (qint32(buf[2]) <<  8) |
                     qint32(buf[3]);
    return static_cast<double>(scaled) / 1000.0;
}

QByteArray buildRequestFrame(quint8 slaveAddr, quint8 funcCode, const QByteArray &data)
{
    QByteArray frame;
    frame.append(static_cast<char>(slaveAddr));
    frame.append(static_cast<char>(funcCode));
    frame.append(data);

    quint16 crc = calcCRC16(frame);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

bool parseResponseFrame(const QByteArray &frame, quint8 &addr, quint8 &func,
                        QByteArray &payload)
{
    if (frame.size() < 4) return false;

    const quint16 crcReceived =
        (quint16(static_cast<quint8>(frame[frame.size()-1])) << 8) |
         quint16(static_cast<quint8>(frame[frame.size()-2]));

    const QByteArray body = frame.left(frame.size() - 2);
    if (calcCRC16(body) != crcReceived) return false;

    addr    = static_cast<quint8>(frame[0]);
    func    = static_cast<quint8>(frame[1]);
    payload = frame.mid(2, frame.size() - 4);
    return true;
}
