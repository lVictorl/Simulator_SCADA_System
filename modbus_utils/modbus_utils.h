#pragma once
#include <QtCore/QByteArray>
#include <cstdint>

namespace ModbusUtils {
    quint16 calcCRC16(const QByteArray &data);
    void packScaledInt32(double value, quint8 *buf);
    double unpackScaledInt32(const quint8 *buf);
    QByteArray buildRequestFrame(quint8 slaveAddr, quint8 funcCode, const QByteArray &data);
    bool parseResponseFrame(const QByteArray &frame, quint8 &addr, quint8 &func, QByteArray &payload);
}
