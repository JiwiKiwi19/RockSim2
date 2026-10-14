// simulation.h
#pragma once

#include <QJsonObject>
#include <QString>

class Simulation {
public:
    virtual ~Simulation() = default;

    virtual QString type() const = 0;

    virtual QJsonObject toJson() const = 0;
    virtual void fromJson(const QJsonObject& obj) = 0;

    virtual void reset() = 0;
};
