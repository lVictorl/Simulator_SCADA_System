#include "modbus_slave_adapter.h"
#include "virtual_mcu.h"
#include "modbus_utils.h"
#include <QtCore/QThread>

// ─── Константы карты регистров ───────────────────────────────────────────────
static constexpr int TELE_START_REG = 100;
static constexpr int TELE_REG_COUNT = 33;
static constexpr int CMD_START_REG  =   0;
static constexpr int CMD_REG_COUNT  =  32;

ModbusSlaveAdapter::ModbusSlaveAdapter(int slaveAddr, QObject *parent)
    : QThread(parent), m_slave_addr(slaveAddr)
{}

ModbusSlaveAdapter::~ModbusSlaveAdapter()
{
    stopAdapter();
    wait(3000);
}

void ModbusSlaveAdapter::updateTelemetry(const MCUTelemetry &tele)
{
    QMutexLocker lock(&m_tele_mutex);
    m_last_tele  = tele;
    m_tele_valid = true;
}

void ModbusSlaveAdapter::receiveFrame(const QByteArray &frame)
{
    QMutexLocker lock(&m_req_mutex);
    m_req_queue.enqueue(frame);
}

bool ModbusSlaveAdapter::takeResponseFrame(QByteArray &out)
{
    QMutexLocker lock(&m_resp_mutex);
    if (m_resp_queue.isEmpty()) return false;
    out = m_resp_queue.dequeue();
    return true;
}

void ModbusSlaveAdapter::stopAdapter()
{
    m_stop_flag.storeRelaxed(1);
    requestInterruption();
}

void ModbusSlaveAdapter::run()
{
    while (!isInterruptionRequested() && !m_stop_flag.loadRelaxed()) {
        QByteArray frame;
        {
            QMutexLocker lock(&m_req_mutex);
            if (!m_req_queue.isEmpty())
                frame = m_req_queue.dequeue();
        }
        if (!frame.isEmpty())
            processFrame(frame);
        else
            msleep(5);
    }
}

void ModbusSlaveAdapter::processFrame(const QByteArray &frame)
{
    quint8 addr, func;
    QByteArray payload;
    if (!ModbusUtils::parseResponseFrame(frame, addr, func, payload)) return;
    if (addr != static_cast<quint8>(m_slave_addr))                    return;

    if (func == 0x10) handleWrite(addr, payload);
    else if (func == 0x03) handleRead(addr, payload);
}

void ModbusSlaveAdapter::handleWrite(quint8 addr, const QByteArray &payload)
{
    // payload: [startReg_Hi][startReg_Lo][qty_Hi][qty_Lo][byteCount][data...]
    if (payload.size() < 5) return;
    const quint16 startReg = (quint8(payload[0]) << 8) | quint8(payload[1]);
    // const quint16 qty = (quint8(payload[2]) << 8) | quint8(payload[3]);
    const int byteCount = quint8(payload[4]);
    if (payload.size() < 5 + byteCount) return;
    if (startReg != CMD_START_REG) return;

    const QByteArray regs = payload.mid(5, byteCount);
    MCUCommand cmd = decodeCommand(regs);
    if (m_mcu) m_mcu->enqueueCommand(cmd);

    // Ответ 0x10: [addr][0x10][startReg_Hi][startReg_Lo][qty_Hi][qty_Lo]
    QByteArray data;
    data.append(payload[0]); data.append(payload[1]);
    data.append(payload[2]); data.append(payload[3]);
    QMutexLocker lock(&m_resp_mutex);
    m_resp_queue.enqueue(ModbusUtils::buildRequestFrame(addr, 0x10, data));
}

void ModbusSlaveAdapter::handleRead(quint8 addr, const QByteArray &payload)
{
    // payload: [startReg_Hi][startReg_Lo][qty_Hi][qty_Lo]
    if (payload.size() < 4) return;
    const quint16 startReg = (quint8(payload[0]) << 8) | quint8(payload[1]);
    const quint16 qty      = (quint8(payload[2]) << 8) | quint8(payload[3]);

    if (startReg != TELE_START_REG || qty != TELE_REG_COUNT) return;

    const QByteArray regData = buildTelemetryRegisters();
    QByteArray respData;
    respData.append(static_cast<char>(regData.size()));
    respData.append(regData);

    QMutexLocker lock(&m_resp_mutex);
    m_resp_queue.enqueue(ModbusUtils::buildRequestFrame(addr, 0x03, respData));
}

// ─── Кодирование телеметрии в регистры ──────────────────────────────────────
// Регистры 100..132 (33 регистра × 2 байта = 66 байт)
// Формат описан в ТЗ (разделы 4.2)
QByteArray ModbusSlaveAdapter::buildTelemetryRegisters() const
{
    QMutexLocker lock(&m_tele_mutex);
    if (!m_tele_valid) return QByteArray(TELE_REG_COUNT * 2, '\0');

    const MCUTelemetry &t = m_last_tele;
    const SensorData   &s = t.sensors;

    QByteArray buf(TELE_REG_COUNT * 2, '\0');
    auto *b = reinterpret_cast<quint8*>(buf.data());

    // 0–1 (reg100–101): timestamp, int32 ×1000 → 4 байта
    ModbusUtils::packScaledInt32(s.timestamp, b + 0);
    // 2–3: engine_rpm
    ModbusUtils::packScaledInt32(s.engine_rpm, b + 4);
    // 4–5: torque
    ModbusUtils::packScaledInt32(s.torque, b + 8);
    // 6–7: engine_temp
    ModbusUtils::packScaledInt32(s.engine_temp, b + 12);
    // 8–9: oil_pressure
    ModbusUtils::packScaledInt32(s.oil_pressure, b + 16);
    // 10–11: fuel_pressure
    ModbusUtils::packScaledInt32(s.fuel_pressure, b + 20);
    // 12–13: boost_pressure
    ModbusUtils::packScaledInt32(s.boost_pressure, b + 24);
    // 14–15: dyno_motor_temp
    ModbusUtils::packScaledInt32(s.dyno_motor_temp, b + 28);
    // 16–17: resistor_temp
    ModbusUtils::packScaledInt32(s.resistor_temp, b + 32);
    // 18–19: oil_level
    ModbusUtils::packScaledInt32(s.oil_level, b + 36);
    // 20–21: fuel_level
    ModbusUtils::packScaledInt32(s.fuel_level, b + 40);
    // 22: fault_mask (uint16)
    b[44] = (s.fault_mask >> 8) & 0xFF;
    b[45] =  s.fault_mask       & 0xFF;
    // 23: state
    b[46] = 0; b[47] = static_cast<quint8>(t.state);
    // 24: mode
    b[48] = 0; b[49] = static_cast<quint8>(t.mode);
    // 25: throttle_pct ×10
    quint16 thr = static_cast<quint16>(t.throttle_pct * 10.0);
    b[50] = (thr >> 8) & 0xFF; b[51] = thr & 0xFF;
    // 26: dyno_mode
    b[52] = 0; b[53] = static_cast<quint8>(t.dyno_mode);
    // 27–28: dyno_speed_or_torque int32 ×1000
    ModbusUtils::packScaledInt32(t.dyno_speed_or_torque, b + 54);
    // 29: engine_fan
    b[58] = 0; b[59] = t.engine_fan ? 1 : 0;
    // 30: dyno_fan
    b[60] = 0; b[61] = t.dyno_fan ? 1 : 0;
    // 31: resistor_fan
    b[62] = 0; b[63] = t.resistor_fan ? 1 : 0;
    // 32: error_mask (первые 8 ошибок → биты)
    quint16 errMask = 0;
    for (int i = 0; i < qMin(t.errors.size(), 8); ++i)
        errMask |= (1u << i);
    b[64] = (errMask >> 8) & 0xFF; b[65] = errMask & 0xFF;

    return buf;
}

// ─── Декодирование команды из регистров ─────────────────────────────────────
// Формат описан в ТЗ (раздел 4.1)
MCUCommand ModbusSlaveAdapter::decodeCommand(const QByteArray &regs) const
{
    MCUCommand cmd;
    cmd.slave_id = m_slave_addr;

    if (regs.size() < CMD_REG_COUNT * 2) return cmd;
    const auto *b = reinterpret_cast<const quint8*>(regs.constData());

    static const QStringList CMD_IDS = {
        QStringLiteral("start"),         // 1
        QStringLiteral("stop"),          // 2
        QStringLiteral("emergency_stop"),// 3
        QStringLiteral("next_stage"),    // 4
        QStringLiteral("reset_emergency"),// 5
        QStringLiteral("set_throttle"),  // 6
        QStringLiteral("set_mode"),      // 7
        QStringLiteral("inject_fault"),  // 8
        QStringLiteral("clear_fault"),   // 9
    };

    const quint16 cmdIdx = (quint16(b[0]) << 8) | b[1];
    if (cmdIdx >= 1 && cmdIdx <= 9)
        cmd.command_id = CMD_IDS[cmdIdx - 1];

    cmd.mode                   = static_cast<BreakInMode>((quint16(b[2]) << 8) | b[3]);
    cmd.duration_sec           = ModbusUtils::unpackScaledInt32(b +  4);
    cmd.warmup_duration_sec    = ModbusUtils::unpackScaledInt32(b +  8);
    cmd.target_rpm             = ModbusUtils::unpackScaledInt32(b + 12);
    cmd.target_torque          = ModbusUtils::unpackScaledInt32(b + 16);
    cmd.limits.max_engine_temp = ModbusUtils::unpackScaledInt32(b + 20);
    cmd.limits.min_oil_pressure= ModbusUtils::unpackScaledInt32(b + 24);
    cmd.limits.max_oil_pressure= ModbusUtils::unpackScaledInt32(b + 28);
    cmd.limits.min_fuel_pressure=ModbusUtils::unpackScaledInt32(b + 32);
    cmd.limits.max_fuel_pressure=ModbusUtils::unpackScaledInt32(b + 36);
    cmd.limits.max_boost_pressure=ModbusUtils::unpackScaledInt32(b + 40);
    cmd.limits.max_engine_rpm  = ModbusUtils::unpackScaledInt32(b + 44);
    cmd.limits.max_dyno_motor_temp=ModbusUtils::unpackScaledInt32(b + 48);
    cmd.limits.max_resistor_temp=ModbusUtils::unpackScaledInt32(b + 52);

    const quint16 thrRaw = (quint16(b[56]) << 8) | b[57];
    cmd.throttle_value    = thrRaw / 10.0;
    cmd.manual_throttle   = ((quint16(b[58]) << 8) | b[59]) != 0;
    cmd.fault_sensor_idx  = (quint16(b[60]) << 8) | b[61];
    cmd.fault_type        = static_cast<SensorFaultType>((quint16(b[62]) << 8) | b[63]);

    return cmd;
}
