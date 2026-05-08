#include "modbus_master_adapter.h"
#include "emulated_modbus_transport.h"
#include <QtCore/QThread>
#include <QtCore/QCoreApplication>
#include <stdexcept>

ModbusMasterAdapter::ModbusMasterAdapter(IModbusTransport *transport,
                                          QList<int> slaveIds,
                                          QObject *parent)
    : QObject(parent)
    , m_transport(transport)
    , m_slave_ids(slaveIds)
{
    for (int id : slaveIds) {
        m_fail_counts[id]    = 0;
        m_connection_ok[id]  = true;
    }
}

ModbusMasterAdapter::~ModbusMasterAdapter()
{
    stop();
}

void ModbusMasterAdapter::start()
{
    m_worker_thread = new QThread(this);

    m_poll_timer = new QTimer;
    m_poll_timer->setInterval(POLL_INTERVAL_MS);
    m_poll_timer->moveToThread(m_worker_thread);

    // moveToThread(this) — запрещённый паттерн (объект имеет родителя).
    // Вместо этого перемещаем только таймер; pollAll() вызывается через
    // Qt::AutoConnection — исполняется в потоке, из которого пришёл сигнал таймера.
    connect(m_worker_thread, &QThread::started,
            m_poll_timer, qOverload<>(&QTimer::start));
    connect(m_poll_timer, &QTimer::timeout,
            this, &ModbusMasterAdapter::pollAll, Qt::DirectConnection);
    connect(m_worker_thread, &QThread::finished,
            m_poll_timer, &QObject::deleteLater);

    m_worker_thread->start();
}

void ModbusMasterAdapter::stop()
{
    if (m_poll_timer) {
        QMetaObject::invokeMethod(m_poll_timer, "stop", Qt::BlockingQueuedConnection);
    }
    if (m_worker_thread) {
        m_worker_thread->quit();
        m_worker_thread->wait(2000);
    }
}

void ModbusMasterAdapter::pollAll()
{
    for (int slaveId : m_slave_ids) {
        try {
            MCUTelemetry tele = readTelemetry(slaveId);
            m_fail_counts[slaveId] = 0;
            if (!m_connection_ok[slaveId]) {
                m_connection_ok[slaveId] = true;
                emit connectionRestored(slaveId);
            }
            emit telemetryReady(tele);
        } catch (...) {
            m_fail_counts[slaveId]++;
            if (m_fail_counts[slaveId] >= WATCHDOG_MAX_FAIL && m_connection_ok[slaveId]) {
                m_connection_ok[slaveId] = false;
                emit connectionLost(slaveId);
            }
        }
    }
}

void ModbusMasterAdapter::sendCommand(const MCUCommand &cmd)
{
    QByteArray frame = buildCommandFrame(cmd);
    QMutexLocker lock(&m_transport_mutex);
    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        m_transport->sendFrame(frame);
        QByteArray resp;
        if (m_transport->receiveFrame(resp, 500)) return;
    }
    throw std::runtime_error("Нет ответа от устройства после повторных попыток");
}

MCUTelemetry ModbusMasterAdapter::readTelemetry(int slaveId)
{
    QByteArray data(4, '\0');
    data[0] = (TELE_START_REG >> 8) & 0xFF;
    data[1] =  TELE_START_REG       & 0xFF;
    data[2] = (TELE_REG_COUNT >> 8) & 0xFF;
    data[3] =  TELE_REG_COUNT       & 0xFF;

    QByteArray frame = ModbusUtils::buildRequestFrame(
        static_cast<quint8>(slaveId), 0x03, data);

    QMutexLocker lock(&m_transport_mutex);
    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        m_transport->sendFrame(frame);
        QByteArray resp;
        if (m_transport->receiveFrame(resp, 500)) {
            quint8 addr, func;
            QByteArray payload;
            if (ModbusUtils::parseResponseFrame(resp, addr, func, payload))
                return decodeTelemetry(payload, slaveId);
        }
    }
    throw std::runtime_error("Не удалось получить телеметрию");
}

// ─── Сборка кадра команды (0x10, 32 регистра) ───────────────────────────────
QByteArray ModbusMasterAdapter::buildCommandFrame(const MCUCommand &cmd) const
{
    static const QStringList CMD_IDS = {
        QStringLiteral("start"),          // 1
        QStringLiteral("stop"),           // 2
        QStringLiteral("emergency_stop"), // 3
        QStringLiteral("next_stage"),     // 4
        QStringLiteral("reset_emergency"),// 5
        QStringLiteral("set_throttle"),   // 6
        QStringLiteral("set_mode"),       // 7
        QStringLiteral("inject_fault"),   // 8
        QStringLiteral("clear_fault"),    // 9
    };

    const int cmdIdx = CMD_IDS.indexOf(cmd.command_id) + 1; // 1-based

    QByteArray regs(CMD_REG_COUNT * 2, '\0');
    auto *b = reinterpret_cast<quint8*>(regs.data());

    // reg 0: command_id enum
    b[0] = 0; b[1] = static_cast<quint8>(cmdIdx);
    // reg 1: mode
    b[2] = 0; b[3] = static_cast<quint8>(cmd.mode);
    // reg 2–3: duration_sec
    ModbusUtils::packScaledInt32(cmd.duration_sec, b + 4);
    // reg 4–5: warmup_duration_sec
    ModbusUtils::packScaledInt32(cmd.warmup_duration_sec, b + 8);
    // reg 6–7: target_rpm
    ModbusUtils::packScaledInt32(cmd.target_rpm, b + 12);
    // reg 8–9: target_torque
    ModbusUtils::packScaledInt32(cmd.target_torque, b + 16);
    // reg 10–11: max_engine_temp
    ModbusUtils::packScaledInt32(cmd.limits.max_engine_temp, b + 20);
    // reg 12–13: min_oil_pressure
    ModbusUtils::packScaledInt32(cmd.limits.min_oil_pressure, b + 24);
    // reg 14–15: max_oil_pressure
    ModbusUtils::packScaledInt32(cmd.limits.max_oil_pressure, b + 28);
    // reg 16–17: min_fuel_pressure
    ModbusUtils::packScaledInt32(cmd.limits.min_fuel_pressure, b + 32);
    // reg 18–19: max_fuel_pressure
    ModbusUtils::packScaledInt32(cmd.limits.max_fuel_pressure, b + 36);
    // reg 20–21: max_boost_pressure
    ModbusUtils::packScaledInt32(cmd.limits.max_boost_pressure, b + 40);
    // reg 22–23: max_engine_rpm
    ModbusUtils::packScaledInt32(cmd.limits.max_engine_rpm, b + 44);
    // reg 24–25: max_dyno_motor_temp
    ModbusUtils::packScaledInt32(cmd.limits.max_dyno_motor_temp, b + 48);
    // reg 26–27: max_resistor_temp
    ModbusUtils::packScaledInt32(cmd.limits.max_resistor_temp, b + 52);
    // reg 28: throttle_value ×10
    quint16 thr = static_cast<quint16>(cmd.throttle_value * 10.0);
    b[56] = (thr >> 8) & 0xFF; b[57] = thr & 0xFF;
    // reg 29: manual_throttle
    b[58] = 0; b[59] = cmd.manual_throttle ? 1 : 0;
    // reg 30: fault_sensor_idx
    b[60] = 0; b[61] = static_cast<quint8>(cmd.fault_sensor_idx);
    // reg 31: fault_type
    b[62] = 0; b[63] = static_cast<quint8>(cmd.fault_type);

    // Заголовок 0x10: [startReg_Hi][startReg_Lo][qty_Hi][qty_Lo][byteCount][regs...]
    QByteArray header(5, '\0');
    header[0] = (CMD_START_REG >> 8) & 0xFF;
    header[1] =  CMD_START_REG       & 0xFF;
    header[2] = (CMD_REG_COUNT >> 8) & 0xFF;
    header[3] =  CMD_REG_COUNT       & 0xFF;
    header[4] = static_cast<char>(CMD_REG_COUNT * 2);

    return ModbusUtils::buildRequestFrame(
        static_cast<quint8>(cmd.slave_id), 0x10, header + regs);
}

// ─── Декодирование ответа 0x03 ───────────────────────────────────────────────
MCUTelemetry ModbusMasterAdapter::decodeTelemetry(const QByteArray &payload, int slaveId) const
{
    // payload: [byteCount][data...]
    MCUTelemetry tele;
    tele.slave_id = slaveId;
    if (payload.size() < 1) return tele;
    const int byteCount = static_cast<quint8>(payload[0]);
    if (payload.size() < 1 + byteCount || byteCount < TELE_REG_COUNT * 2) return tele;

    const auto *b = reinterpret_cast<const quint8*>(payload.constData() + 1);

    tele.sensors.timestamp       = ModbusUtils::unpackScaledInt32(b + 0);
    tele.sensors.engine_rpm      = ModbusUtils::unpackScaledInt32(b + 4);
    tele.sensors.torque          = ModbusUtils::unpackScaledInt32(b + 8);
    tele.sensors.engine_temp     = ModbusUtils::unpackScaledInt32(b + 12);
    tele.sensors.oil_pressure    = ModbusUtils::unpackScaledInt32(b + 16);
    tele.sensors.fuel_pressure   = ModbusUtils::unpackScaledInt32(b + 20);
    tele.sensors.boost_pressure  = ModbusUtils::unpackScaledInt32(b + 24);
    tele.sensors.dyno_motor_temp = ModbusUtils::unpackScaledInt32(b + 28);
    tele.sensors.resistor_temp   = ModbusUtils::unpackScaledInt32(b + 32);
    tele.sensors.oil_level       = ModbusUtils::unpackScaledInt32(b + 36);
    tele.sensors.fuel_level      = ModbusUtils::unpackScaledInt32(b + 40);
    tele.sensors.fault_mask      = (quint16(b[44]) << 8) | b[45];
    tele.state       = static_cast<BreakInState>((quint16(b[46]) << 8) | b[47]);
    tele.mode        = static_cast<BreakInMode> ((quint16(b[48]) << 8) | b[49]);
    tele.throttle_pct= static_cast<double>((quint16(b[50]) << 8) | b[51]) / 10.0;
    tele.dyno_mode   = static_cast<DynoMotorMode>((quint16(b[52]) << 8) | b[53]);
    tele.dyno_speed_or_torque = ModbusUtils::unpackScaledInt32(b + 54);
    tele.engine_fan   = b[59] != 0;
    tele.dyno_fan     = b[61] != 0;
    tele.resistor_fan = b[63] != 0;
    return tele;
}
