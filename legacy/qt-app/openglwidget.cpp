#include "openglwidget.h"
#include "particlesglwidget.h"
#include "springmassglwidget.h"

#include <QDebug>

// Singleton instance
OpenGLWidget *OpenGLWidget::instance = nullptr;

OpenGLWidget::OpenGLWidget(QWidget *parent)
    : QWidget(parent)
{
    qDebug() << "OpenGLWidget instance created";

    layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    createSimulationWidget(); // default = particles
}

OpenGLWidget::~OpenGLWidget()
{
    qDebug() << "OpenGLWidget instance destroyed";
}

OpenGLWidget *OpenGLWidget::getInstance(QWidget *parent)
{
    if (!instance)
    {
        instance = new OpenGLWidget(parent);
    }
    return instance;
}

void OpenGLWidget::setSimulationType(SimulationType type)
{
    if (type == currentType)
        return;

    currentType = type;
    clearLayout();
    createSimulationWidget();
}

void OpenGLWidget::reset()
{
    clearLayout();
    currentType = SimulationType::Particles;
    createSimulationWidget();
}

void OpenGLWidget::clearLayout()
{
    if (currentWidget)
    {
        currentWidget->deleteLater();
        currentWidget = nullptr;
    }

    particlesWidget = nullptr;
    springDemoWidget = nullptr;
}

void OpenGLWidget::createSimulationWidget()
{
    switch (currentType)
    {
    case SimulationType::Particles:
    {
        particlesWidget = new ParticlesGLWidget(this, this->particle_count);
        currentWidget = particlesWidget;

        connect(particlesWidget, &ParticlesGLWidget::statsUpdated,
                this, &OpenGLWidget::statsChanged);
        break;
    }

    case SimulationType::Springs:
    {
        // Show SpringMassGLWidget for springs demo
        SpringMassGLWidget *springWidget = new SpringMassGLWidget(this);
        springDemoWidget = springWidget;
        currentWidget = springWidget;

        // Also connect stats if needed
        // Note: SpringMassGLWidget doesn't emit statsUpdated, but we could add it
        break;
    }
    }

    layout->addWidget(currentWidget);
}