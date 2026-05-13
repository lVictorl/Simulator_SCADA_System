#include "datatypes.h"

QString breakInModeDisplayName(BreakInMode mode)
{
    switch (mode) {
    case BreakInMode::COLD:       return QStringLiteral("Холодная обкатка");
    case BreakInMode::HOT_NOLOAD: return QStringLiteral("Горячая без нагрузки");
    case BreakInMode::HOT_LOAD:   return QStringLiteral("Горячая под нагрузкой");
    }
    return QStringLiteral("Неизвестный режим");
}

QString breakInStateDisplayName(BreakInState state)
{
    switch (state) {
    case BreakInState::IDLE:         return QStringLiteral("Ожидание");
    case BreakInState::COLD_BREAKIN: return QStringLiteral("Холодная обкатка");
    case BreakInState::WARMUP:       return QStringLiteral("Прогрев");
    case BreakInState::HOT_NOLOAD:   return QStringLiteral("Горячая без нагрузки");
    case BreakInState::HOT_LOAD:     return QStringLiteral("Горячая с нагрузкой");
    case BreakInState::STOPPED:      return QStringLiteral("Завершено");
    case BreakInState::EMERGENCY:    return QStringLiteral("АВАРИЯ");
    }
    return QStringLiteral("Неизвестное состояние");
}
