#pragma once
/**
 * high_level_controller.h — контроллер верхнего уровня.
 * GUI вызывает методы этого класса; он формирует MCUCommand и отправляет через мастер.
 */
#include "datatypes.h"

class ModbusMasterAdapter;

class HighLevelController
{
public:
    explicit HighLevelController(ModbusMasterAdapter *master, int defaultSlave = 1);

    void start(BreakInMode mode, double duration, double target,
               const CriticalLimits &limits, double warmup = 5.0,
               int slaveId = -1);
    void stop           (int slaveId = -1);
    void emergencyStop  (int slaveId = -1);
    void nextStage      (int slaveId = -1);
    void resetEmergency (int slaveId = -1);
    void setThrottleManual(double value, int slaveId = -1);
    void setThrottleAuto  (int slaveId = -1);
    void injectFault(int sensorIdx, SensorFaultType type, int slaveId = -1);
    void clearFault (int sensorIdx, int slaveId = -1);

private:
    void send(MCUCommand cmd, int slaveId);
    int resolve(int slaveId) const { return slaveId < 0 ? m_default_slave : slaveId; }

    ModbusMasterAdapter *m_master;
    int                  m_default_slave;
};
