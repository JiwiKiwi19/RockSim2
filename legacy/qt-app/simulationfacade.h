#ifndef SIMULATIONFACADE_H
#define SIMULATIONFACADE_H

#pragma once
#include "openglwidget.h"
#include <QJsonObject>

// Use facade pattern to separate UI from the simulation
class SimulationFacade {
public:
    SimulationFacade(OpenGLWidget* glWidget);

    void setSimulationType(SimulationType type);
    void toggleGrid(bool enable);
    void setParticleMass(float mass);
    void setSimulationSpeed(int speed);
    void setDamping(float factor);
    void toggleCollisions(bool enable);
    void setGravity(bool enable);
    void togglePause();

    QJsonObject save() const;
    void load(const QJsonObject& obj);

private:
    // OpenGLWidget* openGLWidget = OpenGLWidget::getInstance();
    OpenGLWidget* openGLWidget;
    SimulationType currentType = SimulationType::Particles;
};

#endif // SIMULATIONFACADE_H
