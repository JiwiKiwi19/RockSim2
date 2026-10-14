#include "particlesglwidget.h"

#include <cmath>
#include <random>
#include <iostream>

/*
 * Initialization/Constructor of class. We check is available to us in this module. Then
 * we start a timer to keep track of FPS.
 */
ParticlesGLWidget::ParticlesGLWidget(QWidget *parent, int initialParticleCount)
    : QOpenGLWidget(parent), updateTimer(nullptr), frameCount(0), currentFPS(0.0f), useHIP(false), enableCollisions(false), enableDownwardsGravity(false), damping(0.5f)
{
#ifdef HIP_ENABLED
    useHIP = particleKernel.isHIPAvailable();
    if (useHIP)
    {
        qDebug() << "Particle simulation using HIP GPU acceleration";
    }
    else
    {
        qDebug() << "Particle simulation using CPU (HIP not available)";
    }
#endif

    // Calculate grid dimensions
    float worldSize = BOUNDARY * 2.0f;
    cellSize = GRID_CELL_SIZE;
    gridWidth = static_cast<int>(std::ceil(worldSize / cellSize));
    gridHeight = static_cast<int>(std::ceil(worldSize / cellSize));
    grid.resize(gridWidth * gridHeight);

    qDebug() << "Spatial grid initialized:" << gridWidth << "x" << gridHeight << "cells";

    // Create timer for animation
    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &ParticlesGLWidget::updateSimulation);
    updateTimer->start(16); // ~60 FPS

    fpsTimer.start();

    pauseSimulation();
}

/*
 * Destructor method. It destroys the class after stopping the timer and deleting the timer object itself.
 */
ParticlesGLWidget::~ParticlesGLWidget()
{
    if (updateTimer)
    {
        updateTimer->stop();
        delete updateTimer;
    }
}

bool ParticlesGLWidget::getCollisionsStatus()
{
    return this->enableCollisions;
}

void ParticlesGLWidget::setCollisionsStatus(bool newStatus)
{
    this->enableCollisions = newStatus;
}

bool ParticlesGLWidget::getDownwardsGravity()
{
    return this->enableDownwardsGravity;
}

void ParticlesGLWidget::setDownwardsGravity(bool newStatus)
{
    this->enableDownwardsGravity = newStatus;
}

float ParticlesGLWidget::getDamping()
{
    return this->damping;
}

void ParticlesGLWidget::setDamping(float newStatus)
{
    this->damping = newStatus;
}

/*
 * An overriden method which is automatically called by Qt. It will first log the characteristics of OpenGL
 * and the device it is using. Then, it will set up the basic settings for the frame and objects.
 */
void ParticlesGLWidget::initializeGL()
{
    initializeOpenGLFunctions();

    QString glVendor = reinterpret_cast<const char *>(glGetString(GL_VENDOR));
    QString glRenderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    QString glVersion = reinterpret_cast<const char *>(glGetString(GL_VERSION));

    qDebug() << "OpenGL Vendor: " << glVendor;
    qDebug() << "OpenGL Renderer: " << glRenderer;
    qDebug() << "OpenGL Version: " << glVersion;

    // Set background to dark blue/black
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);

    // Enable point size control
    glEnable(GL_POINT_SMOOTH);
    glPointSize(PARTICLE_SIZE);

    // Enable blending for text rendering
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Initialize particles with random positions and velocities
    initializeParticles();
}

/*
 * Initialize each particle as per our count, giving them random attributes, and
 * loading those particles into our vector
 */
void ParticlesGLWidget::initializeParticles()
{
    particles.clear();
    particles.reserve(PARTICLE_COUNT);

    // Random number generators
    std::random_device rd;  // Random seed
    std::mt19937 gen(rd()); // Random numbers based on a deterministic seed
    // Uniform distributions of the potential values for each of these variables
    std::uniform_real_distribution<float> posDist(-BOUNDARY * 0.8f, BOUNDARY * 0.8f);
    std::uniform_real_distribution<float> velDist(-0.01f, 0.01f);
    std::uniform_real_distribution<float> colorDist(0.3f, 1.0f);

    // Create [PARTICLE_COUNT] particles
    for (int i = 0; i < PARTICLE_COUNT; ++i)
    {
        Particle p;
        p.x = posDist(gen);
        p.y = posDist(gen);
        p.vx = velDist(gen);
        p.vy = velDist(gen);
        p.radius = PARTICLE_RADIUS;

        // Initialize with a base mass and radius
        p.mass = BASE_MASS;
        p.radius = BASE_RADIUS;

        // Random colors (tend towards blues and purples)
        p.r = colorDist(gen) * 0.6f;
        p.g = colorDist(gen) * 0.4f;
        p.b = colorDist(gen);

        particles.push_back(p);
    }
}

// testing features
void ParticlesGLWidget::drawGrid()
{
    glColor3f(0.3f, 0.3f, 0.4f); // lighter color
    glLineWidth(1.5f);           // thicker lines

    glBegin(GL_LINES);

    float step = cellSize;
    for (int i = -gridWidth / 2; i <= gridWidth / 2; ++i)
    {
        float x = i * step - (step / 2);
        glVertex2f(x, -BOUNDARY);
        glVertex2f(x, BOUNDARY);
    }

    for (int j = -gridHeight / 2; j <= gridHeight / 2; ++j)
    {
        float y = j * step - (step / 2);
        glVertex2f(-BOUNDARY, y);
        glVertex2f(BOUNDARY, y);
    }

    glEnd();
}

float ParticlesGLWidget::calculateRadiusFromMass(float mass) const
{
    // r ∝ m^(1/3) - maintains constant density
    return BASE_RADIUS * std::pow(mass / BASE_MASS, 1.0f / 3.0f);
}

void ParticlesGLWidget::setAllParticlesMass(float newMass)
{
    if (newMass > 0)
    {
        for (auto &particle : particles)
        {
            particle.mass = newMass;
            particle.radius = calculateRadiusFromMass(newMass);
        }
    }
}

/*
 * An overriden function which is automatically called by Qt. This does the actual
 * rendering and painting of the particles. First clears the screen. Then it paints
 * the particles and then the FPS.
 */
void ParticlesGLWidget::paintGL()
{
    // Calculate FPS
    frameCount++;
    if (fpsTimer.elapsed() >= 1000)
    { // Update every second
        currentFPS = frameCount * 1000.0f / fpsTimer.elapsed();
        frameCount = 0;
        fpsTimer.restart();
        emit statsUpdated(currentFPS, particles.size());
    }

    // Clear the screen
    glClear(GL_COLOR_BUFFER_BIT);

    // testing features
    if (showGrid)
    {
        drawGrid();
    }

    // Draw all particles with individual sizing
    for (const auto &particle : particles)
    {
        // Scale visual size based on radius
        float visualSize = particle.radius * 500.0f;              // Scale factor for visibility
        glPointSize(std::max(1.0f, std::min(visualSize, 20.0f))); // Clamp between 1-20

        glBegin(GL_POINTS);
        glColor3f(particle.r, particle.g, particle.b);
        glVertex2f(particle.x, particle.y);
        glEnd();
    }
}

/*
 * This method is just for rendering the FPS counter text.
 */
void ParticlesGLWidget::renderText(const QString &text)
{
    // Save OpenGL state
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, width(), height(), 0, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    // Set text color to white
    glColor3f(1.0f, 1.0f, 1.0f);

    // Position for bottom-right corner
    int x = width() - 100; // 100 pixels from right
    int y = height() - 20; // 20 pixels from bottom

    // Draw text using QPainter
    QPainter painter(this);
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 14, QFont::Bold));
    painter.drawText(x, y, text);
    painter.end();

    // Restore OpenGL state
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

/*
 * An overriden method automatically called by Qt. Just used for sizing the frame.
 */
void ParticlesGLWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

/*
 * This is essentially what the timer calculates based on. First we update particle
 * attributes and then repaint.
 */
void ParticlesGLWidget::updateSimulation()
{
    updateParticles();
    update(); // Trigger repaint
}

/*
 * This class picks between whether the GPU or CPU should be used. GPU is typically used
 * if HIP is available and the device can be detected. In which case, the GPU is used to
 * update the particles. Otherwise the CPU method below this is used.
 */
void ParticlesGLWidget::updateParticles()
{

// Try GPU acceleration first if available
#ifdef HIP_ENABLED

    if (useHIP)
    {
        bool success = particleKernel.updateParticlesGPU(
            particles.data(),
            particles.size(),
            BOUNDARY,
            damping,
            enableDownwardsGravity);

        if (!success)
        {
            qDebug() << "HIP kernel failed, falling back to CPU";
            useHIP = false; // Disable HIP for future frames
            updateParticlesCPU();
        }
        // else {
        //     qDebug() << "HIP kernel succeeded!";
        // }
    }
    else
    {

        if (enableCollisions)
        {
            updateParticlesCPUWithCollisions();
        }
        else
        {
            updateParticlesCPU();
        }
    }

#else
    if (enableCollisions)
    {
        updateParticlesCPUWithCollisions();
    }
    else
    {
        updateParticlesCPU();
    }

#endif
}

/*
 * This method is used when the CPU is used for calculations. These are
 * the calculations done to determine the next position of the particles.
 * We make sure that they also lose energy over time and don't go out of bounds.
 */
void ParticlesGLWidget::updateParticlesCPU()
{

    for (auto &p : particles)
    {
        // Update positions
        p.x += p.vx;
        p.y += p.vy;

        // Boundary collision detection and response
        if (p.x < -BOUNDARY || p.x > BOUNDARY)
        {
            p.vx = -p.vx * damping;
            // Clamp position to boundary
            p.x = std::max(-BOUNDARY, std::min(BOUNDARY, p.x));
        }

        if (p.y < -BOUNDARY || p.y > BOUNDARY)
        {
            p.vy = -p.vy * damping;
            // Clamp position to boundary
            p.y = std::max(-BOUNDARY, std::min(BOUNDARY, p.y));
        }

        // Simple gravity effect
        if (enableDownwardsGravity)
        {
            p.vy -= 0.0001f * p.mass;
        }
    }
}

void ParticlesGLWidget::updateParticlesCPUWithCollisions()
{
    // Step 1: Update positions (integrator step)
    for (auto &p : particles)
    {
        p.x += p.vx;
        p.y += p.vy;

        // Apply gravity
        if (enableDownwardsGravity)
        {
            p.vy -= 0.0001f * p.mass;
        }
    }

    // Step 2: Handle boundary collisions
    for (auto &p : particles)
    {
        if (p.x - p.radius < -BOUNDARY || p.x + p.radius > BOUNDARY)
        {
            p.vx = -p.vx * damping;
            p.x = std::max(-BOUNDARY + p.radius, std::min(BOUNDARY - p.radius, p.x));
        }

        if (p.y - p.radius < -BOUNDARY || p.y + p.radius > BOUNDARY)
        {
            p.vy = -p.vy * damping;
            p.y = std::max(-BOUNDARY + p.radius, std::min(BOUNDARY - p.radius, p.y));
        }
    }

    // Step 3: Handle particle-particle collisions using spatial grid
    clearGrid();
    populateGrid();
    handleParticleCollisions();
}

void ParticlesGLWidget::clearGrid()
{
    for (auto &cell : grid)
    {
        cell.particleIndices.clear();
    }
}

void ParticlesGLWidget::populateGrid()
{
    for (int i = 0; i < particles.size(); ++i)
    {
        int gridIndex = getGridIndex(particles[i].x, particles[i].y);
        if (gridIndex >= 0 && gridIndex < grid.size())
        {
            grid[gridIndex].particleIndices.push_back(i);
        }
    }
}

int ParticlesGLWidget::getGridIndex(float x, float y)
{
    // Convert world coordinates to grid coordinates
    int gridX = static_cast<int>((x + BOUNDARY) / cellSize);
    int gridY = static_cast<int>((y + BOUNDARY) / cellSize);

    // Clamp to grid bounds
    gridX = std::max(0, std::min(gridWidth - 1, gridX));
    gridY = std::max(0, std::min(gridHeight - 1, gridY));

    return gridY * gridWidth + gridX;
}

void ParticlesGLWidget::getNeighborCells(int gridX, int gridY, std::vector<int> &neighbors)
{
    neighbors.clear();

    // Check 3x3 neighborhood around the cell
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int nx = gridX + dx;
            int ny = gridY + dy;

            if (nx >= 0 && nx < gridWidth && ny >= 0 && ny < gridHeight)
            {
                neighbors.push_back(ny * gridWidth + nx);
            }
        }
    }
}

void ParticlesGLWidget::handleParticleCollisions()
{
    std::vector<int> neighborCells;

    // Check each grid cell
    for (int cellIndex = 0; cellIndex < grid.size(); ++cellIndex)
    {
        const auto &cell = grid[cellIndex];
        if (cell.particleIndices.size() < 2)
            continue; // Skip cells with < 2 particles

        int gridX = cellIndex % gridWidth;
        int gridY = cellIndex / gridWidth;
        getNeighborCells(gridX, gridY, neighborCells);

        // Check collisions within this cell and neighboring cells
        for (int i = 0; i < cell.particleIndices.size(); ++i)
        {
            int particleA = cell.particleIndices[i];

            // Check against particles in neighboring cells
            for (int neighborIndex : neighborCells)
            {
                const auto &neighborCell = grid[neighborIndex];

                for (int particleB : neighborCell.particleIndices)
                {
                    if (particleA >= particleB)
                        continue; // Avoid duplicate checks

                    if (checkCollision(particles[particleA], particles[particleB]))
                    {
                        resolveCollision(particles[particleA], particles[particleB]);
                    }
                }
            }
        }
    }
}

bool ParticlesGLWidget::checkCollision(const Particle &a, const Particle &b)
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    float minDistance = a.radius + b.radius;

    return distance < minDistance;
}

void ParticlesGLWidget::resolveCollision(Particle &a, Particle &b)
{
    // Calculate collision normal
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float distance = std::sqrt(dx * dx + dy * dy);

    if (distance < 0.0001f)
        return; // Avoid division by zero

    // Normalize collision normal
    float nx = dx / distance;
    float ny = dy / distance;

    // Separate particles to prevent overlap using mass ratio
    float overlap = (a.radius + b.radius) - distance;
    // float separation = overlap * 0.5f;
    float totalMass = a.mass + b.mass; // NOW DECLARED
    float separationA = overlap * (b.mass / totalMass);
    float separationB = overlap * (a.mass / totalMass);

    // a.x -= nx * separation;
    // a.y -= ny * separation;
    // b.x += nx * separation;
    // b.y += ny * separation;
    a.x -= nx * separationA;
    a.y -= ny * separationA;
    b.x += nx * separationB;
    b.y += ny * separationB;

    // Calculate relative velocity
    float dvx = b.vx - a.vx;
    float dvy = b.vy - a.vy;

    // Calculate relative velocity along collision normal
    float dvn = dvx * nx + dvy * ny;

    // Do not resolve if velocities are separating
    if (dvn > 0)
        return;

    // Calculate collision impulse
    float impulse = -2.0f * dvn * damping;

    // Apply impulse (assuming equal mass)
    a.vx -= impulse * nx * 0.5f;
    a.vy -= impulse * ny * 0.5f;
    b.vx += impulse * nx * 0.5f;
    b.vy += impulse * ny * 0.5f;
}

/*
 * The following three methods handles the pause state
 * of our simulation. When toggled the simulation will
 * be paused or unpaused
 */
void ParticlesGLWidget::togglePause()
{
    if (paused)
        resumeSimulation();
    else
        pauseSimulation();
}

void ParticlesGLWidget::pauseSimulation()
{
    if (updateTimer && updateTimer->isActive())
    {
        updateTimer->stop();
        paused = true;
        qDebug() << "Simulation paused";
    }
}

void ParticlesGLWidget::resumeSimulation()
{
    if (updateTimer && !updateTimer->isActive())
    {
        updateTimer->start(16); // ~60 FPS
        paused = false;
        qDebug() << "Simulation resumed";
    }
}

// This method increases/decreases the simulation speed using a QSlider
void ParticlesGLWidget::setSimulationSpeed(int speed)
{
    if (!updateTimer)
        return;

    // Define mapping: slider 10–200 → interval 100 ms–5 ms
    // We'll normalize speed so that middle (≈105) gives ~16 ms (original)
    const int minSlider = 10;
    const int maxSlider = 200;
    const int midSlider = (minSlider + maxSlider) / 2; // 105

    // Original interval
    const double baseInterval = 16.0; // ~60 FPS

    // Compute scale factor: below mid slows down, above mid speeds up
    double factor;
    if (speed < midSlider)
        factor = 1.0 + (static_cast<double>(midSlider - speed) / (midSlider - minSlider)) * 4.0; // up to 5× slower
    else
        factor = 1.0 - (static_cast<double>(speed - midSlider) / (maxSlider - midSlider)) * 0.8; // up to 5× faster

    // Clamp and compute new interval
    factor = std::clamp(factor, 0.2, 5.0); // between 5× faster and 5× slower
    int interval = static_cast<int>(baseInterval * factor);

    bool wasActive = updateTimer->isActive();

    updateTimer->stop();
    updateTimer->start(interval);

    qDebug() << "Simulation speed changed: slider=" << speed
             << "interval=" << interval << "ms (factor" << factor << ")";

    if (!wasActive)
        updateTimer->stop(); // keep paused state
}

// JSON saving
QJsonObject ParticlesGLWidget::toJson() const
{
    QJsonObject root;
    root["type"] = "particles";
    root["enableCollisions"] = enableCollisions;
    root["enableGravity"] = enableDownwardsGravity;
    root["damping"] = damping;

    QJsonArray particleArray;
    for (const auto &p : particles)
    {
        QJsonObject jp;
        jp["x"] = p.x;
        jp["y"] = p.y;
        jp["vx"] = p.vx;
        jp["vy"] = p.vy;
        jp["mass"] = p.mass;
        jp["radius"] = p.radius;
        jp["r"] = p.r;
        jp["g"] = p.g;
        jp["b"] = p.b;
        particleArray.append(jp);
    }

    root["particles"] = particleArray;
    return root;
}

void ParticlesGLWidget::fromJson(const QJsonObject &root)
{
    particles.clear();

    enableCollisions = root["enableCollisions"].toBool();
    enableDownwardsGravity = root["enableGravity"].toBool();
    damping = static_cast<float>(root["damping"].toDouble());

    QJsonArray particleArray = root["particles"].toArray();
    for (const auto &val : particleArray)
    {
        QJsonObject jp = val.toObject();
        Particle p;
        p.x = jp["x"].toDouble();
        p.y = jp["y"].toDouble();
        p.vx = jp["vx"].toDouble();
        p.vy = jp["vy"].toDouble();
        p.mass = jp["mass"].toDouble();
        p.radius = jp["radius"].toDouble();
        p.r = jp["r"].toDouble();
        p.g = jp["g"].toDouble();
        p.b = jp["b"].toDouble();
        particles.push_back(p);
    }
}