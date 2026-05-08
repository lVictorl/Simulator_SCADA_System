#pragma once
/**
 * virtual_mcu.h — виртуальный микроконтроллер (эмуляция прошивки STM32).
 *
 * Реализует конечный автомат этапов обкатки дизельного двигателя.
 * Работает в отдельном QThread с периодом dt (по умолчанию 20 мс).
 */

#include <QtCore/QThread>
#include <QtCore/QQueue>
#include <QtCore/QMutex>
#include <QtCore/QElapsedTimer>

#include "datatypes.h"
#include "engine_model.h"
#include "actuator_driver.h"
#include "sensor_simulator.h"
#include "pi_controller.h"

class ModbusSlaveAdapter;

class VirtualMCU : public QThread
{
    Q_OBJECT
public:
    explicit VirtualMCU(QObject *parent = nullptr,
                        int slaveId     = 1,
                        double dt       = 0.02);
    ~VirtualMCU() override;

    void setSlaveAdapter(ModbusSlaveAdapter *adapter);
    void enqueueCommand(const MCUCommand &cmd);
    void stopMCU();

protected:
    void run() override;

private:
    // Обработка команд
    void processCommand(const MCUCommand &cmd);
    void handleStart(const MCUCommand &cmd);
    void performNextStage();

    // Логика управления
    void updateActuatorsLogic();
    std::pair<QStringList, QStringList> checkLimits(const SensorData &s) const;

    // Построение телеметрии
    MCUTelemetry buildTelemetry(const QStringList &errors,
                                const QStringList &warnings,
                                const ActuatorFeedback &fb) const;
    void sendTelemetry(const MCUTelemetry &tele);

    // Компоненты
    EngineModel     m_engine;
    SensorSimulator m_sensors;
    ActuatorDriver  m_actuator_driver;
    PIController    m_pi_controller;

    ModbusSlaveAdapter *m_slave_adapter = nullptr;

    // Состояние автомата
    BreakInState  m_state      = BreakInState::IDLE;
    BreakInMode   m_mode       = BreakInMode::COLD;
    CriticalLimits m_limits;

    double m_dt;
    int    m_slave_id;

    // Параметры текущего этапа
    double m_phase_start        = 0.0; ///< монотонное время начала этапа
    double m_duration           = 0.0;
    double m_warmup_duration    = 5.0;
    double m_target_rpm         = 0.0;
    double m_target_torque      = 0.0;

    // Состояния актуаторов
    double        m_throttle    = 0.0;
    DynoMotorMode m_dyno_mode   = DynoMotorMode::SPIN;
    bool          m_engine_fan  = false;
    bool          m_dyno_fan    = false;
    bool          m_resistor_fan= false;
    bool          m_manual_throttle  = false;
    bool          m_force_next_stage = false;

    // Очередь команд (thread-safe)
    QMutex        m_cmd_mutex;
    QQueue<MCUCommand> m_cmd_queue;

    QElapsedTimer m_elapsed_timer;
    QAtomicInt    m_stop_flag{0};
};
