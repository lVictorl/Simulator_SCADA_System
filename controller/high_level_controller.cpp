#include "high_level_controller.h"
#include "modbus_master_adapter.h"

HighLevelController::HighLevelController(ModbusMasterAdapter *master, int defaultSlave)
    : m_master(master), m_default_slave(defaultSlave)
{}

int HighLevelController::resolve(int slaveId) const
{
    return slaveId < 0 ? m_default_slave : slaveId;
}

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

void HighLevelController::send(MCUCommand cmd, int slaveId)
{
    cmd.slave_id = resolve(slaveId);
    m_master->sendCommand(cmd);
}
