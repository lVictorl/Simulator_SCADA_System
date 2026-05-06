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
