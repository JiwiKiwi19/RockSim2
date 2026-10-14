#ifndef OPENGLWIDGET_H
#define OPENGLWIDGET_H

#include <QVBoxLayout>
#include <QWidget>
#include <QVBoxLayout>
#include "particlesglwidget.h"
#include "springmassglwidget.h"
#include "simulationtype.h" // use the shared SimulationType definition

// Forward declarations
class ParticlesGLWidget;
class SpringMassGLWidget;

class OpenGLWidget : public QWidget
{
    Q_OBJECT

public:
    static OpenGLWidget *getInstance(QWidget *parent = nullptr);

    OpenGLWidget(const OpenGLWidget &) = delete;
    OpenGLWidget &operator=(const OpenGLWidget &) = delete;
    ~OpenGLWidget();

    int particle_count = 1000;

    // Switch simulation type at runtime
    void setSimulationType(SimulationType type);

    // Accessors
    ParticlesGLWidget *getParticlesWidget() const { return particlesWidget; }
    SpringMassGLWidget *getSpringWidget() const { return springDemoWidget; } // Added this getter
    QWidget *getCurrentWidget() const { return currentWidget; }

    void reset();

signals:
    void statsChanged(float fps, int objects);

private:
    explicit OpenGLWidget(QWidget *parent = nullptr);

    void clearLayout();
    void createSimulationWidget();

    static OpenGLWidget *instance;

    QVBoxLayout *layout;
    QWidget *currentWidget = nullptr;
    ParticlesGLWidget *particlesWidget = nullptr;
    SpringMassGLWidget *springDemoWidget = nullptr;
    SimulationType currentType = SimulationType::Particles;
};

#endif // OPENGLWIDGET_H