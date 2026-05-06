#pragma once
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <optional>

enum class BreakInMode : quint16 { COLD = 0, HOT_NOLOAD = 1, HOT_LOAD = 2 };
enum class BreakInState : quint16 { IDLE = 0, COLD_BREAKIN = 1, WARMUP = 2, HOT_NOLOAD = 3, HOT_LOAD = 4, STOPPED = 5, EMERGENCY = 6 };
enum class DynoMotorMode : quint16 { SPIN = 0, BRAKE = 1 };
enum class SensorFaultType : quint16 { NONE = 0, OPEN_CIRCUIT = 1, SHORT_CIRCUIT = 2, OUT_OF_RANGE = 3 };
// ... (остальные структуры будут дописаны позже)
