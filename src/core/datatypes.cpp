#include "core/datatypes.h"

QString breakInModeDisplayName(BreakInMode mode) {
    switch (mode) {
        case BreakInMode::COLD: return QStringLiteral("Холодная обкатка");
        case BreakInMode::HOT_NOLOAD: return QStringLiteral("Горячая без нагрузки");
        case BreakInMode::HOT_LOAD: return QStringLiteral("Горячая под нагрузкой");
    }
    return {};
}
QString breakInStateDisplayName(BreakInState state) {
    switch (state) {
        case BreakInState::IDLE: return QStringLiteral("Ожидание");
        case BreakInState::COLD_BREAKIN: return QStringLiteral("Холодная обкатка");
        case BreakInState::WARMUP: return QStringLiteral("Прогрев");
        case BreakInState::HOT_NOLOAD: return QStringLiteral("Горячая без нагрузки");
        case BreakInState::HOT_LOAD: return QStringLiteral("Горячая с нагрузкой");
        case BreakInState::STOPPED: return QStringLiteral("Завершено");
        case BreakInState::EMERGENCY: return QStringLiteral("АВАРИЯ");
    }
    return {};
}
void registerMetaTypes() {
    qRegisterMetaType<BreakInMode>("BreakInMode");
    qRegisterMetaType<BreakInState>("BreakInState");
    qRegisterMetaType<DynoMotorMode>("DynoMotorMode");
    qRegisterMetaType<SensorFaultType>("SensorFaultType");
    // ... регистрация структур будет добавляться по мере создания
}

bool validateMCUCommand(const MCUCommand &cmd, QString &error)
{
    static const QStringList cmdIds = {
        "start", "stop", "emergency_stop", "next_stage", "reset_emergency",
        "set_throttle", "set_mode", "inject_fault", "clear_fault"
    };
    if (!cmdIds.contains(cmd.command_id)) {
        error = "Неизвестная команда"; return false;
    }
    if (cmd.slave_id < 1 || cmd.slave_id > 247) {
        error = "slave_id вне диапазона"; return false;
    }
    if (cmd.command_id == "start") {
        if (cmd.duration_sec <= 0) { error = "Некорректная длительность"; return false; }
        if (cmd.mode == BreakInMode::COLD && cmd.target_rpm <= 0) {
            error = "Нужны обороты"; return false;
        }
        if (cmd.mode == BreakInMode::HOT_LOAD && cmd.target_torque < 0) {
            error = "Нужен крутящий момент"; return false;
        }
    }
    return true;
}
