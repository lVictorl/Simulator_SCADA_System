#include "engine_model.h"
#include <QtCore/QElapsedTimer>
#include <cmath>
#include <algorithm>

static QElapsedTimer s_timer;
namespace { struct TimerInit { TimerInit() { s_timer.start(); } } s_init; }

EngineModel::EngineModel(double ambientTemp)
    : m_ambient(ambientTemp)
    , m_engine_temp(ambientTemp)
    , m_dyno_motor_temp(ambientTemp)
    , m_resistor_temp(ambientTemp)
{}

double EngineModel::clamp(double v, double lo, double hi) noexcept
{
    return std::max(lo, std::min(hi, v));
}
