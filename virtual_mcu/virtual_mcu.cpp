#include "virtual_mcu.h"

VirtualMCU::VirtualMCU(QObject *parent, int slaveId, double dt)
    : QThread(parent), m_slave_id(slaveId), m_dt(dt)
{}

VirtualMCU::~VirtualMCU()
{
    stopMCU();
    wait(3000);
}
