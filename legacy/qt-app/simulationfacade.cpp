#include "simulationfacade.h"
#include <QDebug>

/*
 * We use the facade structural design pattern
 * it seperates the UI and simulation layers to
 * create a middle man of sorts. The mainwindow
 * is too tightly coupled for more modular
 * improvemnets
 */

SimulationFacade::SimulationFacade(OpenGLWidget* glWidget)
    : openGLWidget(glWidget)
{
}

// Set simulation type at runtime
void SimulationFacade::setSimulationType(SimulationType type)
{
    currentType = type;
    openGLWidget->setSimulationType(type);
}

void SimulationFacade::toggleGrid(bool enable) {
    if (currentType != SimulationType::Particles) return;
    if (auto pw = openGLWidget->getParticlesWidget())
        pw->toggleGridDisplay(enable);
}

void SimulationFacade::setParticleMass(float mass) {
    if (currentType != SimulationType::Particles) return;
    if (auto pw = openGLWidget->getParticlesWidget())
        pw->setAllParticlesMass(mass);
}

void SimulationFacade::setSimulationSpeed(int speed) {
    if (currentType != SimulationType::Particles) return;
    if (auto pw = openGLWidget->getParticlesWidget())
        pw->setSimulationSpeed(speed);
}

void SimulationFacade::setDamping(float factor) {
    if (currentType != SimulationType::Particles) return;
    if (auto pw = openGLWidget->getParticlesWidget())
        pw->setDamping(factor);
}

void SimulationFacade::toggleCollisions(bool enable) {
    if (currentType != SimulationType::Particles) return;
    if (auto pw = openGLWidget->getParticlesWidget())
        pw->setCollisionsStatus(enable);
}

void SimulationFacade::setGravity(bool enable) {
    if (currentType != SimulationType::Particles) return;
    if (auto pw = openGLWidget->getParticlesWidget())
        pw->setDownwardsGravity(enable);
}

void SimulationFacade::togglePause() {
    if (currentType != SimulationType::Particles) return;
    if (auto pw = openGLWidget->getParticlesWidget())
        pw->togglePause();
}

// Save simulation state
QJsonObject SimulationFacade::save() const
{
    QJsonObject root;
    root["version"] = 1;

    QJsonObject sim;

    if (currentType == SimulationType::Particles) {
        if (auto pw = openGLWidget->getParticlesWidget())
            sim = pw->toJson();
    }
    else if (currentType == SimulationType::Springs) {
        sim["type"] = "springs";
        // Placeholder, only saves type JSONM
    }

    root["simulation"] = sim;
    return root;
}

// Load simulation from JSON
void SimulationFacade::load(const QJsonObject& root)
{
    QJsonObject simObj = root["simulation"].toObject();
    QString typeStr = simObj["type"].toString();

    if (typeStr == "particles") {
        currentType = SimulationType::Particles;
        openGLWidget->setSimulationType(SimulationType::Particles);

        if (auto pw = openGLWidget->getParticlesWidget())
            pw->fromJson(simObj);
    }
    else if (typeStr == "springs") {
        // TEMP: using TriangleGLWidget as a placeholder for our spring model
        currentType = SimulationType::Springs;
        openGLWidget->setSimulationType(SimulationType::Springs);
    }
}


