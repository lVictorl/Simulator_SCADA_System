#include "high_level_controller.h"
#include "modbus_master_adapter.h"

HighLevelController::HighLevelController(ModbusMasterAdapter *master, int defaultSlave)
    : m_master(master), m_default_slave(defaultSlave)
{}

int HighLevelController::resolve(int slaveId) const
{
    return slaveId < 0 ? m_default_slave : slaveId;
}
