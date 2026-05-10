#include "high_level_controller.h"
#include "modbus_master_adapter.h"

HighLevelController::HighLevelController(ModbusMasterAdapter *master, int defaultSlave)
    : m_master(master), m_default_slave(defaultSlave)
{}

void HighLevelController::start(BreakInMode mode, double duration, double target,
                                 const CriticalLimits &limits, double warmup, int slaveId)
{
    MCUCommand cmd;
    cmd.command_id          = QStringLiteral("start");
    cmd.mode                = mode;
    cmd.duration_sec        = duration;
    cmd.warmup_duration_sec = (mode != BreakInMode::COLD) ? warmup : 0.0;
    cmd.limits              = limits;
    if (mode == BreakInMode::HOT_LOAD) {
        cmd.target_rpm    = 1500.0;
        cmd.target_torque = target;
    } else {
        cmd.target_rpm    = target;
        cmd.target_torque = 0.0;
    }
    send(cmd, slaveId);
}

void HighLevelController::stop(int slaveId)
{
    MCUCommand cmd; cmd.command_id = QStringLiteral("stop");
    send(cmd, slaveId);
}

void HighLevelController::emergencyStop(int slaveId)
{
    MCUCommand cmd; cmd.command_id = QStringLiteral("emergency_stop");
    send(cmd, slaveId);
}

void HighLevelController::nextStage(int slaveId)
{
    MCUCommand cmd; cmd.command_id = QStringLiteral("next_stage");
    send(cmd, slaveId);
}

void HighLevelController::resetEmergency(int slaveId)
{
    MCUCommand cmd; cmd.command_id = QStringLiteral("reset_emergency");
    send(cmd, slaveId);
}

void HighLevelController::setThrottleManual(double value, int slaveId)
{
    MCUCommand cmd;
    cmd.command_id    = QStringLiteral("set_throttle");
    cmd.manual_throttle = true;
    cmd.throttle_value  = value;
    send(cmd, slaveId);
}

void HighLevelController::setThrottleAuto(int slaveId)
{
    MCUCommand cmd;
    cmd.command_id    = QStringLiteral("set_throttle");
    cmd.manual_throttle = false;
    send(cmd, slaveId);
}

void HighLevelController::injectFault(int sensorIdx, SensorFaultType type, int slaveId)
{
    MCUCommand cmd;
    cmd.command_id     = QStringLiteral("inject_fault");
    cmd.fault_sensor_idx = sensorIdx;
    cmd.fault_type     = type;
    send(cmd, slaveId);
}

void HighLevelController::clearFault(int sensorIdx, int slaveId)
{
    MCUCommand cmd;
    cmd.command_id     = QStringLiteral("clear_fault");
    cmd.fault_sensor_idx = sensorIdx;
    send(cmd, slaveId);
}

void HighLevelController::send(MCUCommand cmd, int slaveId)
{
    cmd.slave_id = resolve(slaveId);
    m_master->sendCommand(cmd);
}
