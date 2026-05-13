/**
 * @file modbus_utils.cpp
 * @brief Утилиты протокола Modbus RTU.
 *
 * Modbus RTU — промышленный протокол последовательной связи.
 * Структура кадра RTU:
 *   [Адрес устройства (1 байт)]
 *   [Код функции (1 байт)]
 *   [Данные (N байт)]
 *   [CRC-16 (2 байта, little-endian)]
 *
 * Используемые коды функций:
 *   0x03 — Read Holding Registers (чтение телеметрии)
 *   0x10 — Write Multiple Registers (запись команды)
 */

#include "modbus_utils.h"
#include <cstring>
#include <cstdint>

namespace ModbusUtils {

/**
 * @brief Вычисление CRC-16 по алгоритму Modbus.
 *
 * Полином: 0xA001 (обратный порядок бит от 0x8005).
 * Начальное значение: 0xFFFF.
 * Алгоритм: последовательное XOR каждого байта с регистром,
 *            8 раз сдвигаем вправо с применением полинома при LSB=1.
 */
quint16 calcCRC16(const QByteArray &data)
{
    quint16 crc = 0xFFFF;  // начальное значение по стандарту Modbus
    for (unsigned char byte : data) {
        crc ^= byte;       // XOR с текущим байтом
        for (int i = 0; i < 8; ++i) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001u;  // LSB=1: сдвиг + полином
            else
                crc >>= 1;                    // LSB=0: только сдвиг
        }
    }
    return crc;
}

/**
 * @brief Упаковка float в 4 байта big-endian (IEEE 754).
 *
 * Big-endian: старший байт первым.
 * Используется memcpy для корректной работы с любым представлением float.
 */
void packFloat32(float value, quint8 *buf)
{
    quint32 raw;
    std::memcpy(&raw, &value, 4);  // битовое копирование float → uint32
    buf[0] = (raw >> 24) & 0xFF;   // старший байт
    buf[1] = (raw >> 16) & 0xFF;
    buf[2] = (raw >>  8) & 0xFF;
    buf[3] =  raw        & 0xFF;   // младший байт
}

/// Распаковка float из 4 байт big-endian.
float unpackFloat32(const quint8 *buf)
{
    quint32 raw = (quint32(buf[0]) << 24) |
                  (quint32(buf[1]) << 16) |
                  (quint32(buf[2]) <<  8) |
                   quint32(buf[3]);
    float value;
    std::memcpy(&value, &raw, 4);  // битовое копирование uint32 → float
    return value;
}

/**
 * @brief Упаковка double в int32 с масштабированием ×1000 → 4 байта big-endian.
 *
 * Масштабирование позволяет передавать числа с точностью до 0.001
 * через 32-битный Modbus-регистр без потери дробной части.
 *
 * Пример: 1234.567 → 1234567 (int32) → [0x00][0x12][0xD6][0x87]
 */
void packScaledInt32(double value, quint8 *buf)
{
    qint32 scaled = static_cast<qint32>(value * 1000.0);
    buf[0] = (scaled >> 24) & 0xFF;
    buf[1] = (scaled >> 16) & 0xFF;
    buf[2] = (scaled >>  8) & 0xFF;
    buf[3] =  scaled        & 0xFF;
}

/// Распаковка int32 ×1000 из 4 байт big-endian → double.
double unpackScaledInt32(const quint8 *buf)
{
    // Знаковое расширение: buf[0] приводим к qint32 для сохранения знака
    qint32 scaled = (qint32(buf[0]) << 24) |
                    (qint32(buf[1]) << 16) |
                    (qint32(buf[2]) <<  8) |
                     qint32(buf[3]);
    return static_cast<double>(scaled) / 1000.0;
}

/**
 * @brief Сборка полного кадра Modbus RTU.
 *
 * Формат: [slaveAddr][funcCode][data...][CRC_lo][CRC_hi]
 * CRC хранится в little-endian (младший байт первым) — стандарт Modbus RTU.
 */
QByteArray buildRequestFrame(quint8 slaveAddr, quint8 funcCode, const QByteArray &data)
{
    QByteArray frame;
    frame.append(static_cast<char>(slaveAddr));
    frame.append(static_cast<char>(funcCode));
    frame.append(data);

    quint16 crc = calcCRC16(frame);
    // Little-endian CRC: сначала младший байт
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

/**
 * @brief Разбор и верификация кадра Modbus RTU.
 *
 * Проверяет CRC и извлекает поля.
 * @return false если кадр слишком короткий или CRC не совпадает.
 */
bool parseResponseFrame(const QByteArray &frame, quint8 &addr, quint8 &func,
                        QByteArray &payload)
{
    if (frame.size() < 4) return false;  // минимум: addr + func + CRC(2)

    // Читаем CRC из конца кадра (little-endian: [N-2]=lo, [N-1]=hi)
    const quint16 crcReceived =
        (quint16(static_cast<quint8>(frame[frame.size()-1])) << 8) |
         quint16(static_cast<quint8>(frame[frame.size()-2]));

    // Вычисляем CRC по телу (без последних 2 байт)
    const QByteArray body = frame.left(frame.size() - 2);
    if (calcCRC16(body) != crcReceived) return false;  // ошибка CRC

    addr    = static_cast<quint8>(frame[0]);    // адрес устройства
    func    = static_cast<quint8>(frame[1]);    // код функции
    payload = frame.mid(2, frame.size() - 4);  // данные (без addr, func, CRC)
    return true;
}

} // namespace ModbusUtils
