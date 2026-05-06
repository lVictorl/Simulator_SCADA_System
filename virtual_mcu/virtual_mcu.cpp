#include "virtual_mcu.h"

VirtualMCU::VirtualMCU(QObject *parent, int slaveId, double dt)
    : QThread(parent), m_slave_id(slaveId), m_dt(dt)
{}

VirtualMCU::~VirtualMCU()
{
    stopMCU();
    wait(3000);
}

void VirtualMCU::run()
{
    while (!isInterruptionRequested() && !m_stop_flag.loadRelaxed()) {
        MCUTelemetry tele;
        tele.sensors.timestamp = 0.0;
        tele.state = BreakInState::IDLE;
        sendTelemetry(tele);
        msleep(static_cast<unsigned long>(m_dt * 1000));
    }
}

void VirtualMCU::sendTelemetry(const MCUTelemetry &tele)
{
    if (m_slave_adapter)
        m_slave_adapter->updateTelemetry(tele);
}

void VirtualMCU::enqueueCommand(const MCUCommand &cmd)
{
    QMutexLocker lock(&m_cmd_mutex);
    m_cmd_queue.enqueue(cmd);
}
