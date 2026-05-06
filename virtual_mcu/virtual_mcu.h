#pragma once
#include <QtCore/QThread>
#include <QtCore/QQueue>
#include <QtCore/QMutex>
#include "datatypes.h"

class ModbusSlaveAdapter;
class EngineModel;
class SensorSimulator;
class ActuatorDriver;
class PIController;

class VirtualMCU : public QThread {
    Q_OBJECT
public:
    explicit VirtualMCU(QObject *parent = nullptr, int slaveId = 1, double dt = 0.02);
    ~VirtualMCU() override;

    void setSlaveAdapter(ModbusSlaveAdapter *adapter);
    void enqueueCommand(const MCUCommand &cmd);
    void stopMCU();

protected:
    void run() override;

private:
    // компоненты (будут добавлены позже)
    ModbusSlaveAdapter *m_slave_adapter = nullptr;

    int m_slave_id;
    double m_dt;
    QMutex m_cmd_mutex;
    QQueue<MCUCommand> m_cmd_queue;
    QAtomicInt m_stop_flag{0};

    void sendTelemetry(const MCUTelemetry &tele);
};
