#include "virtual_mcu.h"
#include "modbus_slave_adapter.h"
#include <QtCore/QElapsedTimer>

VirtualMCU::VirtualMCU(QObject *parent, int slaveId, double dt)
    : QThread(parent)
    , m_engine(20.0)
    , m_sensors(m_engine)
    , m_slave_id(slaveId)
    , m_dt(dt)
{
    m_elapsed_timer.start();
}

VirtualMCU::~VirtualMCU()
{
    stopMCU();
    wait(3000);
}

void VirtualMCU::setSlaveAdapter(ModbusSlaveAdapter *adapter)
{
    m_slave_adapter = adapter;
}

void VirtualMCU::enqueueCommand(const MCUCommand &cmd)
{
    QMutexLocker lock(&m_cmd_mutex);
    m_cmd_queue.enqueue(cmd);
}

void VirtualMCU::stopMCU()
{
    m_stop_flag.storeRelaxed(1);
    requestInterruption();
}

void VirtualMCU::run()
{
    m_elapsed_timer.restart();

    while (!isInterruptionRequested() && !m_stop_flag.loadRelaxed()) {

        // 1. Обработка всех накопившихся команд
        {
            QMutexLocker lock(&m_cmd_mutex);
            while (!m_cmd_queue.isEmpty())
                processCommand(m_cmd_queue.dequeue());
        }

        // Пассивные состояния — только телеметрия
        if (m_state == BreakInState::IDLE    ||
            m_state == BreakInState::STOPPED ||
            m_state == BreakInState::EMERGENCY)
        {
            sendTelemetry(buildTelemetry({}, {}, m_actuator_driver.feedback()));
            msleep(static_cast<unsigned long>(m_dt * 1000));
            continue;
        }

        // 2. Принудительный переход этапа
        if (m_force_next_stage)
            performNextStage();

        // 3. Логика управления вентиляторами и дросселем
        updateActuatorsLogic();

        // 4. Применение уставок к драйверу актуаторов
        ActuatorSetpoints sp;
        sp.throttle       = m_throttle;
        sp.target_speed   = (m_mode == BreakInMode::COLD) ? m_target_rpm : 0.0;
        sp.target_torque  = (m_mode == BreakInMode::HOT_LOAD) ? m_target_torque : 0.0;
        sp.dyno_mode      = m_dyno_mode;
        sp.engine_running = (m_state != BreakInState::IDLE &&
                              m_state != BreakInState::COLD_BREAKIN &&
                              m_state != BreakInState::STOPPED &&
                              m_state != BreakInState::EMERGENCY);
        sp.engine_fan     = m_engine_fan;
        sp.dyno_motor_fan = m_dyno_fan;
        sp.resistor_fan   = m_resistor_fan;

        m_actuator_driver.applySetpoints(sp, m_dt);
        const ActuatorFeedback &fb = m_actuator_driver.feedback();

        // 5. Шаг модели двигателя с фактическими значениями обратной связи
        ActuatorSetpoints effective = sp;
        effective.throttle      = fb.throttle_actual;
        effective.target_speed  = fb.dyno_speed_actual;
        effective.target_torque = fb.dyno_torque_actual;
        effective.engine_fan    = fb.engine_fan_actual;
        effective.dyno_motor_fan= fb.dyno_motor_fan_actual;
        effective.resistor_fan  = fb.resistor_fan_actual;
        m_engine.step(m_dt, effective);

        // 6. Проверка переходов по времени
        const double elapsed = m_elapsed_timer.elapsed() / 1000.0 - m_phase_start;

        if (m_state == BreakInState::COLD_BREAKIN && elapsed >= m_duration)
            m_state = BreakInState::STOPPED;
        else if (m_state == BreakInState::WARMUP && elapsed >= m_warmup_duration) {
            m_state = (m_mode == BreakInMode::HOT_NOLOAD)
                      ? BreakInState::HOT_NOLOAD : BreakInState::HOT_LOAD;
            m_phase_start = m_elapsed_timer.elapsed() / 1000.0;
            m_pi_controller.reset();
        }
        else if (m_state == BreakInState::HOT_NOLOAD && elapsed >= m_duration)
            m_state = BreakInState::STOPPED;
        else if (m_state == BreakInState::HOT_LOAD  && elapsed >= m_duration)
            m_state = BreakInState::STOPPED;

        // 7. Проверка критических пределов
        const SensorData sens = m_sensors.read();
        auto [errors, warnings] = checkLimits(sens);
        if (!errors.isEmpty())
            m_state = BreakInState::EMERGENCY;

        // 8. Телеметрия
        sendTelemetry(buildTelemetry(errors, warnings, fb));

        msleep(static_cast<unsigned long>(m_dt * 1000));
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void VirtualMCU::processCommand(const MCUCommand &cmd)
{
    if (cmd.slave_id != m_slave_id) return;

    if (cmd.command_id == QStringLiteral("start")) {
        handleStart(cmd);
    } else if (cmd.command_id == QStringLiteral("stop")) {
        m_state = BreakInState::IDLE;
    } else if (cmd.command_id == QStringLiteral("emergency_stop")) {
        m_state = BreakInState::EMERGENCY;
    } else if (cmd.command_id == QStringLiteral("next_stage")) {
        m_force_next_stage = true;
    } else if (cmd.command_id == QStringLiteral("reset_emergency")) {
        if (m_state == BreakInState::EMERGENCY) {
            m_state = BreakInState::IDLE;
            m_force_next_stage = false;
            m_manual_throttle  = false;
        }
    } else if (cmd.command_id == QStringLiteral("set_throttle")) {
        m_manual_throttle = cmd.manual_throttle;
        if (m_manual_throttle)
            m_throttle = qBound(0.0, cmd.throttle_value, 100.0);
    } else if (cmd.command_id == QStringLiteral("set_mode")) {
        m_mode   = cmd.mode;
        m_limits = cmd.limits;
    } else if (cmd.command_id == QStringLiteral("inject_fault")) {
        m_sensors.setFault(cmd.fault_sensor_idx, cmd.fault_type);
    } else if (cmd.command_id == QStringLiteral("clear_fault")) {
        m_sensors.clearFault(cmd.fault_sensor_idx);
    }
}

void VirtualMCU::handleStart(const MCUCommand &cmd)
{
    m_mode              = cmd.mode;
    m_duration          = cmd.duration_sec;
    m_warmup_duration   = cmd.warmup_duration_sec > 0 ? cmd.warmup_duration_sec : 5.0;
    m_target_rpm        = cmd.target_rpm;
    m_target_torque     = cmd.target_torque;
    m_limits            = cmd.limits;

    m_pi_controller.reset();
    m_manual_throttle   = false;
    m_force_next_stage  = false;

    switch (m_mode) {
    case BreakInMode::COLD:
        m_state     = BreakInState::COLD_BREAKIN;
        m_dyno_mode = DynoMotorMode::SPIN;
        break;
    case BreakInMode::HOT_NOLOAD:
        m_state     = BreakInState::WARMUP;
        m_dyno_mode = DynoMotorMode::SPIN;
        break;
    case BreakInMode::HOT_LOAD:
        m_state     = BreakInState::WARMUP;
        m_dyno_mode = DynoMotorMode::BRAKE;
        break;
    }
    m_phase_start = m_elapsed_timer.elapsed() / 1000.0;
}

void VirtualMCU::performNextStage()
{
    m_force_next_stage = false;
    switch (m_state) {
    case BreakInState::COLD_BREAKIN:
        m_state = BreakInState::STOPPED;
        break;
    case BreakInState::WARMUP:
        m_state = (m_mode == BreakInMode::HOT_NOLOAD)
                  ? BreakInState::HOT_NOLOAD : BreakInState::HOT_LOAD;
        m_phase_start = m_elapsed_timer.elapsed() / 1000.0;
        m_pi_controller.reset();
        break;
    case BreakInState::HOT_NOLOAD:
    case BreakInState::HOT_LOAD:
        m_state = BreakInState::STOPPED;
        break;
    default: break;
    }
}

void VirtualMCU::updateActuatorsLogic()
{
    const SensorData s = m_engine.state();

    // Вентиляторы включаются при приближении к критическим температурам
    m_engine_fan   = s.engine_temp    > (m_limits.max_engine_temp     - 10.0);
    m_dyno_fan     = s.dyno_motor_temp > 80.0;
    m_resistor_fan = s.resistor_temp  > 100.0;

    // ПИ-регулятор дросселя (только в горячих режимах, только без ручного управления)
    if (!m_manual_throttle &&
        (m_state == BreakInState::WARMUP   ||
         m_state == BreakInState::HOT_NOLOAD ||
         m_state == BreakInState::HOT_LOAD))
    {
        const double target = (m_state == BreakInState::WARMUP) ? 800.0 : m_target_rpm;
        m_throttle = m_pi_controller.update(target, s.engine_rpm, m_dt);
    } else if (m_state == BreakInState::COLD_BREAKIN) {
        m_throttle = 0.0;
    }
}

std::pair<QStringList, QStringList>
VirtualMCU::checkLimits(const SensorData &s) const
{
    QStringList errors, warnings;
    if (s.engine_temp    > m_limits.max_engine_temp)    errors << QStringLiteral("Критическая температура ДВС");
    if (s.oil_pressure   < m_limits.min_oil_pressure ||
        s.oil_pressure   > m_limits.max_oil_pressure)   errors << QStringLiteral("Давление масла вне допуска");
    if (s.engine_rpm     > m_limits.max_engine_rpm)     errors << QStringLiteral("Превышение оборотов");
    if (s.dyno_motor_temp> m_limits.max_dyno_motor_temp)errors << QStringLiteral("Перегрев электродвигателя");
    if (s.resistor_temp  > m_limits.max_resistor_temp)  errors << QStringLiteral("Перегрев резистора");
    if (s.oil_level  < 10.0) warnings << QStringLiteral("Низкий уровень масла");
    if (s.fuel_level <  5.0) warnings << QStringLiteral("Низкий уровень топлива");
    return {errors, warnings};
}

MCUTelemetry VirtualMCU::buildTelemetry(const QStringList &errors,
                                         const QStringList &warnings,
                                         const ActuatorFeedback &fb) const
{
    MCUTelemetry tele;
    tele.sensors    = m_sensors.read();
    tele.state      = m_state;
    tele.mode       = m_mode;
    tele.throttle_pct = fb.throttle_actual;
    tele.dyno_mode  = m_dyno_mode;
    tele.dyno_speed_or_torque = (m_dyno_mode == DynoMotorMode::SPIN)
                                ? fb.dyno_speed_actual : fb.dyno_torque_actual;
    tele.engine_fan  = fb.engine_fan_actual;
    tele.dyno_fan    = fb.dyno_motor_fan_actual;
    tele.resistor_fan= fb.resistor_fan_actual;
    tele.errors      = errors;
    tele.warnings    = warnings;
    tele.slave_id    = m_slave_id;
    tele.actuator_feedback = fb;
    return tele;
}

void VirtualMCU::sendTelemetry(const MCUTelemetry &tele)
{
    if (m_slave_adapter)
        m_slave_adapter->updateTelemetry(tele);
}
