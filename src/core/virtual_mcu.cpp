/**
 * @file virtual_mcu.cpp
 * @brief Виртуальный микроконтроллер — эмуляция прошивки STM32.
 *
 * Реализует конечный автомат обкатки дизельного двигателя.
 * Работает в отдельном QThread с периодом dt = 20 мс.
 *
 * Архитектура главного цикла (run):
 *
 *   ┌─ каждые 20 мс ──────────────────────────────────────────────────┐
 *   │ 1. Обработать очередь команд (потокобезопасно через мьютекс)    │
 *   │ 2. Пассивные состояния → только телеметрия, пропустить шаг      │
 *   │ 3. Принудительный переход этапа (если next_stage)               │
 *   │ 4. Обновить логику дросселя (ПИ) и вентиляторов                 │
 *   │ 5. Применить уставки к ActuatorDriver (инерционность)           │
 *   │ 6. Шаг физики с ФАКТИЧЕСКИМИ значениями обратной связи          │
 *   │ 7. Проверить временны́е переходы (elapsed >= duration)           │
 *   │ 8. Проверить критические пределы (аварийный останов)            │
 *   │ 9. Отправить телеметрию в ModbusSlaveAdapter                    │
 *   └─────────────────────────────────────────────────────────────────┘
 */

#include "virtual_mcu.h"
#include "modbus_slave_adapter.h"
#include <QtCore/QElapsedTimer>

VirtualMCU::VirtualMCU(QObject *parent, int slaveId, double dt)
    : QThread(parent)
    , m_engine(20.0)          // начальная температура окружающей среды = 20°C
    , m_sensors(m_engine)     // симулятор берёт данные из модели
    , m_slave_id(slaveId)
    , m_dt(dt)
{
    // Запускаем таймер сразу — время начинает идти с создания объекта
    m_elapsed_timer.start();
}

VirtualMCU::~VirtualMCU()
{
    // Корректное завершение: сначала сигнализируем об остановке,
    // затем ждём завершения потока максимум 3 секунды
    stopMCU();
    wait(3000);
}

void VirtualMCU::setSlaveAdapter(ModbusSlaveAdapter *adapter)
{
    m_slave_adapter = adapter;
    // Вызывается до start() — безопасно без мьютекса
}

void VirtualMCU::enqueueCommand(const MCUCommand &cmd)
{
    // Вызывается из потока ModbusSlaveAdapter → нужна защита мьютексом
    QMutexLocker lock(&m_cmd_mutex);
    m_cmd_queue.enqueue(cmd);
}

void VirtualMCU::stopMCU()
{
    // QAtomicInt — атомарная запись, видима из другого потока
    m_stop_flag.storeRelaxed(1);
    // requestInterruption — стандартный механизм QThread для мягкой остановки
    requestInterruption();
}

// ── Главный цикл (выполняется в рабочем потоке) ───────────────────────────
void VirtualMCU::run()
{
    m_elapsed_timer.restart();

    while (!isInterruptionRequested() && !m_stop_flag.loadRelaxed()) {

        // ── Шаг 1: Обработка накопившихся команд ────────────────────────────
        // Берём все команды за один захват мьютекса (минимизируем время блокировки)
        {
            QMutexLocker lock(&m_cmd_mutex);
            while (!m_cmd_queue.isEmpty())
                processCommand(m_cmd_queue.dequeue());
        }

        // ── Шаг 2: Пассивные состояния ──────────────────────────────────────
        // В IDLE/STOPPED/EMERGENCY физика не работает, но телеметрия идёт.
        // Это нужно для отображения состояния в GUI.
        if (m_state == BreakInState::IDLE    ||
            m_state == BreakInState::STOPPED ||
            m_state == BreakInState::EMERGENCY)
        {
            sendTelemetry(buildTelemetry({}, {}, m_actuator_driver.feedback()));
            msleep(static_cast<unsigned long>(m_dt * 1000));
            continue;
        }

        // ── Шаг 3: Принудительный переход этапа ─────────────────────────────
        if (m_force_next_stage)
            performNextStage();

        // ── Шаг 4: Логика управления ─────────────────────────────────────────
        updateActuatorsLogic();

        // ── Шаг 5: Формирование уставок и применение к драйверу ─────────────
        ActuatorSetpoints sp;
        sp.throttle       = m_throttle;
        // target_speed используется только в холодном режиме
        sp.target_speed   = (m_mode == BreakInMode::COLD) ? m_target_rpm : 0.0;
        // target_torque используется только в нагрузочном режиме
        sp.target_torque  = (m_mode == BreakInMode::HOT_LOAD) ? m_target_torque : 0.0;
        sp.dyno_mode      = m_dyno_mode;
        // Двигатель "запущен" только в горячих фазах
        sp.engine_running = (m_state != BreakInState::IDLE         &&
                             m_state != BreakInState::COLD_BREAKIN  &&
                             m_state != BreakInState::STOPPED        &&
                             m_state != BreakInState::EMERGENCY);
        sp.engine_fan     = m_engine_fan;
        sp.dyno_motor_fan = m_dyno_fan;
        sp.resistor_fan   = m_resistor_fan;

        // ApplySetpoints рассчитывает инерционность и возвращает ActuatorFeedback
        m_actuator_driver.applySetpoints(sp, m_dt);
        const ActuatorFeedback &fb = m_actuator_driver.feedback();

        // ── Шаг 6: Шаг физики с ФАКТИЧЕСКИМИ значениями ─────────────────────
        // Важно: передаём fb, а не sp — модель видит реальное положение
        // механизмов, а не заданное. Это корректная обратная связь.
        ActuatorSetpoints effective = sp;
        effective.throttle       = fb.throttle_actual;
        effective.target_speed   = fb.dyno_speed_actual;
        effective.target_torque  = fb.dyno_torque_actual;
        effective.engine_fan     = fb.engine_fan_actual;
        effective.dyno_motor_fan = fb.dyno_motor_fan_actual;
        effective.resistor_fan   = fb.resistor_fan_actual;
        m_engine.step(m_dt, effective);

        // ── Шаг 7: Переходы по времени ──────────────────────────────────────
        const double elapsed = m_elapsed_timer.elapsed() / 1000.0 - m_phase_start;

        if (m_state == BreakInState::COLD_BREAKIN && elapsed >= m_duration)
            m_state = BreakInState::STOPPED;
        else if (m_state == BreakInState::WARMUP && elapsed >= m_warmup_duration) {
            // Прогрев завершён — переходим к основному этапу
            m_state = (m_mode == BreakInMode::HOT_NOLOAD)
                      ? BreakInState::HOT_NOLOAD : BreakInState::HOT_LOAD;
            m_phase_start = m_elapsed_timer.elapsed() / 1000.0;
            m_pi_controller.reset(); // сброс интегральной составляющей при смене этапа
        }
        else if (m_state == BreakInState::HOT_NOLOAD && elapsed >= m_duration)
            m_state = BreakInState::STOPPED;
        else if (m_state == BreakInState::HOT_LOAD  && elapsed >= m_duration)
            m_state = BreakInState::STOPPED;

        // ── Шаг 8: Проверка критических пределов ────────────────────────────
        // Читаем данные через SensorSimulator (с шумом + возможными отказами)
        const SensorData sens = m_sensors.read();
        auto [errors, warnings] = checkLimits(sens);
        // Любая ошибка → немедленный аварийный останов
        if (!errors.isEmpty())
            m_state = BreakInState::EMERGENCY;

        // ── Шаг 9: Отправка телеметрии ──────────────────────────────────────
        sendTelemetry(buildTelemetry(errors, warnings, fb));

        // Пауза до следующего шага
        msleep(static_cast<unsigned long>(m_dt * 1000));
    }
}

// ── Обработка одной команды ───────────────────────────────────────────────
void VirtualMCU::processCommand(const MCUCommand &cmd)
{
    // Игнорируем команды не для нашего адреса
    if (cmd.slave_id != m_slave_id) return;

    if      (cmd.command_id == QStringLiteral("start"))           handleStart(cmd);
    else if (cmd.command_id == QStringLiteral("stop"))            m_state = BreakInState::IDLE;
    else if (cmd.command_id == QStringLiteral("emergency_stop"))  m_state = BreakInState::EMERGENCY;
    else if (cmd.command_id == QStringLiteral("next_stage"))      m_force_next_stage = true;
    else if (cmd.command_id == QStringLiteral("reset_emergency")) {
        if (m_state == BreakInState::EMERGENCY) {
            m_state = BreakInState::IDLE;
            m_force_next_stage = false;
            m_manual_throttle  = false;
        }
    }
    else if (cmd.command_id == QStringLiteral("set_throttle")) {
        m_manual_throttle = cmd.manual_throttle;
        if (m_manual_throttle)
            m_throttle = qBound(0.0, cmd.throttle_value, 100.0);
    }
    else if (cmd.command_id == QStringLiteral("set_mode")) {
        m_mode   = cmd.mode;
        m_limits = cmd.limits;
    }
    else if (cmd.command_id == QStringLiteral("inject_fault"))
        m_sensors.setFault(cmd.fault_sensor_idx, cmd.fault_type);
    else if (cmd.command_id == QStringLiteral("clear_fault"))
        m_sensors.clearFault(cmd.fault_sensor_idx);
}

// ── Запуск процесса обкатки ───────────────────────────────────────────────
void VirtualMCU::handleStart(const MCUCommand &cmd)
{
    m_mode             = cmd.mode;
    m_duration         = cmd.duration_sec;
    m_warmup_duration  = cmd.warmup_duration_sec > 0 ? cmd.warmup_duration_sec : 5.0;
    m_target_rpm       = cmd.target_rpm;
    m_target_torque    = cmd.target_torque;
    m_limits           = cmd.limits;

    m_pi_controller.reset();
    m_manual_throttle  = false;
    m_force_next_stage = false;

    switch (m_mode) {
    case BreakInMode::COLD:
        m_state     = BreakInState::COLD_BREAKIN;
        m_dyno_mode = DynoMotorMode::SPIN;   // ЭД прокручивает вал
        break;
    case BreakInMode::HOT_NOLOAD:
        m_state     = BreakInState::WARMUP;
        m_dyno_mode = DynoMotorMode::SPIN;   // ЭД не создаёт нагрузки
        break;
    case BreakInMode::HOT_LOAD:
        m_state     = BreakInState::WARMUP;
        m_dyno_mode = DynoMotorMode::BRAKE;  // ЭД тормозит вал
        break;
    }
    // Фиксируем момент начала этапа
    m_phase_start = m_elapsed_timer.elapsed() / 1000.0;
}

// ── Принудительный переход на следующий этап ──────────────────────────────
void VirtualMCU::performNextStage()
{
    m_force_next_stage = false;
    switch (m_state) {
    case BreakInState::COLD_BREAKIN:
        m_state = BreakInState::STOPPED;
        break;
    case BreakInState::WARMUP:
        // Прогрев → основной горячий этап
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

// ── Логика управления актуаторами (ПИ-регулятор + вентиляторы) ────────────
void VirtualMCU::updateActuatorsLogic()
{
    // Читаем «истинное» состояние (без шума) для управления
    const SensorData s = m_engine.state();

    // Вентиляторы включаются превентивно — за 10°C до критической температуры
    m_engine_fan   = s.engine_temp     > (m_limits.max_engine_temp - 10.0);
    m_dyno_fan     = s.dyno_motor_temp > 80.0;
    m_resistor_fan = s.resistor_temp   > 100.0;

    // ПИ-регулятор дросселя — только в горячих режимах и при авторежиме
    if (!m_manual_throttle &&
        (m_state == BreakInState::WARMUP    ||
         m_state == BreakInState::HOT_NOLOAD ||
         m_state == BreakInState::HOT_LOAD))
    {
        // На прогреве держим холостые (800 об/мин), на основном этапе — заданные
        const double target = (m_state == BreakInState::WARMUP) ? 800.0 : m_target_rpm;
        m_throttle = m_pi_controller.update(target, s.engine_rpm, m_dt);
    } else if (m_state == BreakInState::COLD_BREAKIN) {
        m_throttle = 0.0; // при холодной прокрутке дроссель закрыт
    }
}

// ── Проверка критических пределов ─────────────────────────────────────────
std::pair<QStringList, QStringList>
VirtualMCU::checkLimits(const SensorData &s) const
{
    QStringList errors, warnings;
    // Ошибки → EMERGENCY
    if (s.engine_temp    > m_limits.max_engine_temp)     errors << QStringLiteral("Критическая температура ДВС");
    if (s.oil_pressure   < m_limits.min_oil_pressure ||
        s.oil_pressure   > m_limits.max_oil_pressure)    errors << QStringLiteral("Давление масла вне допуска");
    if (s.engine_rpm     > m_limits.max_engine_rpm)      errors << QStringLiteral("Превышение оборотов");
    if (s.dyno_motor_temp> m_limits.max_dyno_motor_temp) errors << QStringLiteral("Перегрев электродвигателя");
    if (s.resistor_temp  > m_limits.max_resistor_temp)   errors << QStringLiteral("Перегрев резистора");
    // Предупреждения → только в журнал
    if (s.oil_level  < 10.0) warnings << QStringLiteral("Низкий уровень масла");
    if (s.fuel_level <  5.0) warnings << QStringLiteral("Низкий уровень топлива");
    return {errors, warnings};
}

// ── Формирование пакета телеметрии ────────────────────────────────────────
MCUTelemetry VirtualMCU::buildTelemetry(const QStringList &errors,
                                         const QStringList &warnings,
                                         const ActuatorFeedback &fb) const
{
    MCUTelemetry tele;
    tele.sensors    = m_sensors.read();  // с шумом (реалистичные данные)
    tele.state      = m_state;
    tele.mode       = m_mode;
    tele.throttle_pct = fb.throttle_actual;  // фактический дроссель
    tele.dyno_mode  = m_dyno_mode;
    // В зависимости от режима передаём скорость или момент
    tele.dyno_speed_or_torque = (m_dyno_mode == DynoMotorMode::SPIN)
                                ? fb.dyno_speed_actual : fb.dyno_torque_actual;
    tele.engine_fan   = fb.engine_fan_actual;
    tele.dyno_fan     = fb.dyno_motor_fan_actual;
    tele.resistor_fan = fb.resistor_fan_actual;
    tele.errors       = errors;
    tele.warnings     = warnings;
    tele.slave_id     = m_slave_id;
    tele.actuator_feedback = fb;
    return tele;
}

// ── Отправка телеметрии в слой Modbus ────────────────────────────────────
void VirtualMCU::sendTelemetry(const MCUTelemetry &tele)
{
    // ModbusSlaveAdapter::updateTelemetry потокобезопасна (мьютекс внутри)
    if (m_slave_adapter)
        m_slave_adapter->updateTelemetry(tele);
}
