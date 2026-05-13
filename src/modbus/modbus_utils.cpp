#include "modbus_utils.h"
#include <cstring>
#include <cstdint>

namespace ModbusUtils {

quint16 calcCRC16(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (unsigned char byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001u;
            else
                crc >>= 1;
        }
    }
    return crc;
}

void packFloat32(float value, quint8 *buf)
{
    quint32 raw;
    std::memcpy(&raw, &value, 4);
    buf[0] = (raw >> 24) & 0xFF;
    buf[1] = (raw >> 16) & 0xFF;
    buf[2] = (raw >>  8) & 0xFF;
    buf[3] =  raw        & 0xFF;
}

float unpackFloat32(const quint8 *buf)
{
    quint32 raw = (quint32(buf[0]) << 24) |
                  (quint32(buf[1]) << 16) |
                  (quint32(buf[2]) <<  8) |
                   quint32(buf[3]);
    float value;
    std::memcpy(&value, &raw, 4);
    return value;
}

void packScaledInt32(double value, quint8 *buf)
{
    qint32 scaled = static_cast<qint32>(value * 1000.0);
    buf[0] = (scaled >> 24) & 0xFF;
    buf[1] = (scaled >> 16) & 0xFF;
    buf[2] = (scaled >>  8) & 0xFF;
    buf[3] =  scaled        & 0xFF;
}

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
    frame.append(static_cast<char>(crc & 0xFF));         // младший байт первым (LE)
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

bool parseResponseFrame(const QByteArray &frame, quint8 &addr, quint8 &func,
                        QByteArray &payload)
{
    if (frame.size() < 4) return false;

    // CRC хранится в little-endian в последних 2 байтах
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

} // namespace ModbusUtils
