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
