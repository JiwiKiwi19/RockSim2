#ifndef PARTICLESGLWIDGET_H
#define PARTICLESGLWIDGET_H

#include "particlekernel.h"

#include <QElapsedTimer>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPainter>
#include <QTimer>
#include <vector>
#include <QJsonObject>
#include <QJsonArray>

// Particle simulation widget that handles physics, rendering, and collision detection
class ParticlesGLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{

    Q_OBJECT

public:
    ParticlesGLWidget(QWidget *parent = nullptr, int intitialParticleCount = 1000); // Initialize particle widget, timers, and simulation state
    ~ParticlesGLWidget();

    // Toggle simulation animation
    void togglePause();
    void pauseSimulation();
    void resumeSimulation();
    bool isPaused() const { return paused; } // Pausing simulation using QTimer
    void setSimulationSpeed(int speed);

    // testing features
    void toggleGridDisplay(bool enable) { showGrid = enable; }

    // Getters and setters for collision detection, gravity, and damping
    bool getCollisionsStatus();
    void setCollisionsStatus(bool newStatus);
    bool getDownwardsGravity();
    void setDownwardsGravity(bool newStatus);
    float getDamping();
    void setDamping(float newStatus);

    // Update all particles to have same mass
    void setAllParticlesMass(float newMass);
    static constexpr float BASE_MASS = 1.0f;
    static constexpr float BASE_RADIUS = 0.01f;

    // Convert simulation config data into JSON
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &obj);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    // statsUpdated signal
signals:
    void statsUpdated(float fps, int objectCount);

private slots:
    void updateSimulation();

private:
    void initializeParticles(); // Creates particles with random positions
    void updateParticles();
    void updateParticlesCPU();
    void updateParticlesCPUWithCollisions();
    void renderText(const QString &text);

    // Simulation pause/play
    bool paused = false;

    // testing features
    bool showGrid = false;
    void drawGrid();

    // Spatial grid collision detection methods
    void clearGrid();
    void populateGrid();
    void handleParticleCollisions();
    int getGridIndex(float x, float y);
    void getNeighborCells(int gridX, int gridY, std::vector<int> &neighbors);
    bool checkCollision(const Particle &a, const Particle &b);
    void resolveCollision(Particle &a, Particle &b);

    std::vector<Particle> particles;
    QTimer *updateTimer;

    // Spatial grid for collision detection
    std::vector<GridCell> grid;
    int gridWidth, gridHeight;
    float cellSize;

    static constexpr int PARTICLE_COUNT = 100;
    static constexpr float PARTICLE_SIZE = 3.0f;
    static constexpr float PARTICLE_RADIUS = 0.015f;
    static constexpr float BOUNDARY = 1.0f;
    static constexpr float GRID_CELL_SIZE = PARTICLE_RADIUS * 3.0f;

    // Track FPS
    int frameCount;
    QElapsedTimer fpsTimer;
    float currentFPS;

#ifdef HIP_ENABLED
    ParticleKernel particleKernel;
#endif

    bool useHIP;
    bool enableCollisions;
    bool enableDownwardsGravity;
    float damping;

    // Mass scaling calculation
    float calculateRadiusFromMass(float mass) const;
};

#endif // PARTICLESGLWIDGET_H