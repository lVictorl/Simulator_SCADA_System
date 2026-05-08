#pragma once
/**
 * modbus_utils.h — базовые утилиты протокола Modbus RTU.
 *
 * - CRC-16 (полином 0xA001)
 * - Упаковка/распаковка float32 (IEEE 754, big-endian)
 * - Упаковка/распаковка int32 ×1000 (масштабирование)
 * - Формирование и проверка кадров Modbus RTU
 */

#include <QtCore/QByteArray>
#include <cstdint>

namespace ModbusUtils {

/** Вычисление CRC-16 (Modbus RTU, полином 0xA001). */
quint16 calcCRC16(const QByteArray &data);

/** Упаковывает float → 4 байта big-endian. */
void packFloat32(float value, quint8 *buf);

/** Распаковывает float из 4 байт big-endian. */
float unpackFloat32(const quint8 *buf);

/**
 * Упаковывает double в int32 (масштабирование ×1000) → 4 байта big-endian.
 * Потеря дробей менее 0.001.
 */
void packScaledInt32(double value, quint8 *buf);

/** Распаковывает int32 ×1000 из 4 байт big-endian → double. */
double unpackScaledInt32(const quint8 *buf);

/**
 * Собирает полный кадр Modbus RTU:
 *   [slaveAddr(1)] [funcCode(1)] [data(N)] [CRC(2, little-endian)]
 */
QByteArray buildRequestFrame(quint8 slaveAddr, quint8 funcCode, const QByteArray &data);

/**
 * Разбирает кадр Modbus RTU, проверяет CRC.
 * Возвращает true при успехе.
 * @param frame  входной кадр
 * @param addr   [out] адрес устройства
 * @param func   [out] код функции
 * @param payload [out] полезная нагрузка (без адреса, функции и CRC)
 */
bool parseResponseFrame(const QByteArray &frame, quint8 &addr, quint8 &func,
                        QByteArray &payload);

} // namespace ModbusUtils
