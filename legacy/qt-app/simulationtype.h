// simulationtype.h
#pragma once
#include <QString>

enum class SimulationType {
    Particles,
    Springs
};

inline SimulationType simulationTypeFromString(const QString& s) {
    if (s == "particles") return SimulationType::Particles;
    if (s == "springs")   return SimulationType::Springs;
    return SimulationType::Particles; // default
}

inline QString simulationTypeToString(SimulationType t) {
    switch (t) {
    case SimulationType::Particles: return "particles";
    case SimulationType::Springs:   return "springs";
    }
    return "particles";
}
