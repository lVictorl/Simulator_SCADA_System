#pragma once
#include "datatypes.h"
class ModbusMasterAdapter;

class HighLevelController {
public:
    explicit HighLevelController(ModbusMasterAdapter *master, int defaultSlave = 1);

    void start(BreakInMode mode, double duration, double target,
               const CriticalLimits &limits, double warmup = 5.0, int slaveId = -1);
    void stop(int slaveId = -1);
    void emergencyStop(int slaveId = -1);
    void nextStage(int slaveId = -1);
    void resetEmergency(int slaveId = -1);
    void setThrottleManual(double value, int slaveId = -1);
    void setThrottleAuto(int slaveId = -1);
    void injectFault(int sensorIdx, SensorFaultType type, int slaveId = -1);
    void clearFault(int sensorIdx, int slaveId = -1);

private:
    void send(MCUCommand cmd, int slaveId);
    int resolve(int slaveId) const;
    ModbusMasterAdapter *m_master;
    int m_default_slave;
};
