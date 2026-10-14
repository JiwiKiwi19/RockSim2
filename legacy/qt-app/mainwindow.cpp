// Files
#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "openglwidget.h"
#include "newsimulation.h"
#include "simulationfacade.h"

// Libraries
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QDialog>
#include <QSoundEffect>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QFileDialog>
#include <QFile>
#include <QJsonDocument>
#include <iostream>
#include <QMessageBox> // added for user feedback
#include <type_traits>

namespace
{
    // Helper: return QObject* if T derives from QObject, otherwise return nullptr.
    template <typename T>
    typename std::enable_if<std::is_base_of<QObject, T>::value, QObject *>::type asQObject(T *t)
    {
        return static_cast<QObject *>(t);
    }
    template <typename T>
    typename std::enable_if<!std::is_base_of<QObject, T>::value, QObject *>::type asQObject(T *)
    {
        return nullptr;
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), processInfo(new QProcess(this)), memoryTimer(new QTimer(this)),
      simulation(nullptr)
{
    ui->setupUi(this);

    // Window is maximized on startup
    this->showMaximized();
    setWindowTitle("ROCsim");

    setupUI();

    // Menu buttons actions
    connect(ui->actionExit, &QAction::triggered, this, &QMainWindow::close);
    connect(ui->actionAbout_ROCsim, &QAction::triggered, this, []()
            { QDesktopServices::openUrl(QUrl("https://github.com/Fall-2025-CS-3307/oop-assignment-3-rockers")); });

    connect(ui->actionNewSimulation, &QAction::triggered, this, [this]()
            {
        auto dialog = new newsimulation(this);
        connect(dialog, &newsimulation::simulationSelected,
                this, &MainWindow::loadSimulation);
        dialog->exec(); });
}

MainWindow::~MainWindow()
{
    memoryTimer->stop();
    delete processInfo;
    delete memoryTimer;
    delete simulation;
    delete ui;
}

void MainWindow::setupUI()
{
    // Create OpenGL widget for particle rendering
    openGLWidget = OpenGLWidget::getInstance();

    // Control simulation from UI - Create only once
    if (!simulation)
    {
        simulation = new SimulationFacade(openGLWidget);
    }

    // Grid toggle
    connect(ui->enableGrids, &QCheckBox::toggled, this, [this](bool checked)
            {
        if (simulation) simulation->toggleGrid(checked); });

    // Particle mass (size) change
    connect(ui->inputSize, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value)
            {
                if (simulation) simulation->setParticleMass(static_cast<float>(value)); });

    // Simulation speed
    connect(ui->sliderSimulationSpeed, &QSlider::valueChanged,
            this, [this](int value)
            {
                if (simulation) simulation->setSimulationSpeed(value); });

    // Collisions on/off
    connect(ui->enableCollisions, &QCheckBox::toggled,
            this, [this](bool checked)
            {
                if (simulation) simulation->toggleCollisions(checked); });

    // Downward gravity
    connect(ui->EnableDowardsGravity, &QCheckBox::toggled,
            this, [this](bool checked)
            {
                if (simulation) simulation->setGravity(checked); });

    // Pause / Play
    connect(ui->pausedButton, &QPushButton::clicked, this, [this]()
            {
        if (simulation) {
            simulation->togglePause();

            // Update button visuals
            if (openGLWidget->getCurrentWidget()) {
                if (auto pw = openGLWidget->getParticlesWidget()) {
                    bool paused = pw->isPaused();
                    ui->pausedButton->setText(paused ? "Play Simulation" : "Pause Simulation");
                    ui->pausedButton->setStyleSheet(paused
                                                        ? "QPushButton { background-color: #4CAF50; color: white; border-radius: 6px; }"
                                                          "QPushButton:hover { background-color: #45A049; }"
                                                        : "QPushButton { background-color: #E53935; color: white; border-radius: 6px; }"
                                                          "QPushButton:hover { background-color: #D32F2F; }");
                }
            }
        } });

    // Updating FPS and Objects real-time
    if (openGLWidget)
    {
        connect(openGLWidget, &OpenGLWidget::statsChanged, this, [this](float fps, int objects)
                {
                    ui->labelFPS->setText(QString("FPS: %1").arg(fps, 0, 'f', 2));
                    ui->labelObjects->setText(QString("Objects: %1").arg(objects)); });
    }

    QVBoxLayout *layout = new QVBoxLayout(ui->openGLContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(openGLWidget);

    // Set initial widths to be 900 and 200
    QList<int> sizes;
    sizes << 900 << 200;
    ui->splitter->setSizes(sizes);

    // Accordion setup
    sections.append(new AccordionSection(ui->QenvScene, ui->Fenv, this));
    sections.append(new AccordionSection(ui->QobjInspector, ui->Fobj, this));
    sections.append(new AccordionSection(ui->Qconfig, ui->Fconfig, this));

    QVBoxLayout *rightLayout = qobject_cast<QVBoxLayout *>(ui->rightBarWidget->layout());
    if (rightLayout)
    {
        rightLayout->setAlignment(Qt::AlignTop);
        rightLayout->addStretch();
    }

    // Save Configuration
    connect(ui->pushButton, &QPushButton::clicked, this, [this]()
            {
        if (!simulation) return;

        QString file = QFileDialog::getSaveFileName(
            this, "Save Simulation", QDir::homePath(), "JSON (*.json)");

        if (file.isEmpty()) return;

        QJsonObject root = simulation->save();
        QFile f(file);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(root).toJson());
        } });

    // Create spring controls programmatically
    createSpringControls();

    // Selecting the types of Physics Models
    ui->sceneTreeWidget->clear();

    QTreeWidgetItem *envItem = new QTreeWidgetItem(ui->sceneTreeWidget);
    envItem->setText(0, "Environment");

    QStringList objectNames = {"Particle Model", "Spring Model"};
    for (const QString &name : objectNames)
    {
        QTreeWidgetItem *child = new QTreeWidgetItem(envItem);
        child->setText(0, name);
    }

    envItem->setExpanded(true);

    // Connect tree widget to switch between simulations
    connect(ui->sceneTreeWidget, &QTreeWidget::itemClicked,
            this, [this](QTreeWidgetItem *item, int)
            {

                if (!item || !simulation)
                    return;

                if (item->text(0) == "Particle Model") {
                    openGLWidget->setSimulationType(SimulationType::Particles);
                    simulation->setSimulationType(SimulationType::Particles);

                    // Show particle controls, hide spring controls
                    showSpringControls(false);

                    // Show particle controls
                    ui->enableCollisions->setVisible(true);
                    ui->EnableDowardsGravity->setVisible(true);
                    ui->enableGrids->setVisible(true);
                    ui->inputSize->setVisible(true);

                    // Reset pause button for particle simulation
                    auto particlesWidget = openGLWidget->getParticlesWidget();
                    if (particlesWidget) {
                        bool paused = particlesWidget->isPaused();
                        ui->pausedButton->setText(paused ? "Play Simulation" : "Pause Simulation");
                        ui->pausedButton->setStyleSheet(paused
                                                            ? "QPushButton { background-color: #4CAF50; color: white; border-radius: 6px; }"
                                                              "QPushButton:hover { background-color: #45A049; }"
                                                            : "QPushButton { background-color: #E53935; color: white; border-radius: 6px; }"
                                                              "QPushButton:hover { background-color: #D32F2F; }");
                    }
                }
                else if (item->text(0) == "Spring Model") {
                    openGLWidget->setSimulationType(SimulationType::Springs);
                    simulation->setSimulationType(SimulationType::Springs);

                    // Show spring controls, hide particle controls
                    showSpringControls(true);

                    // Hide particle controls
                    ui->enableCollisions->setVisible(false);
                    ui->EnableDowardsGravity->setVisible(false);
                    ui->enableGrids->setVisible(false);
                    ui->inputSize->setVisible(false);

                    // Start the spring simulation automatically
                    auto springWidget = openGLWidget->getSpringWidget();
                    if (springWidget) {
                        springWidget->resumeSimulation();

                        // Update UI controls with current spring values
                        if (springMassSpinBox) {
                            springMassSpinBox->setValue(static_cast<int>(springWidget->getHangingMass()));
                        }
                        if (gravitySlider) {
                            gravitySlider->setValue(static_cast<int>(springWidget->getGravity()));
                        }
                        if (gravityValueLabel) {
                            gravityValueLabel->setText(QString::number(static_cast<int>(springWidget->getGravity())));
                        }
                        if (stiffnessSlider) {
                            stiffnessSlider->setValue(static_cast<int>(springWidget->getDefaultStiffness() * 100));
                        }
                        if (stiffnessValueLabel) {
                            stiffnessValueLabel->setText(QString::number(static_cast<int>(springWidget->getDefaultStiffness() * 100)) + "%");
                        }

                        // Update pause button for spring simulation
                        bool paused = springWidget->isPaused();
                        ui->pausedButton->setText(paused ? "Play Simulation" : "Pause Simulation");
                        ui->pausedButton->setStyleSheet(paused
                                                            ? "QPushButton { background-color: #4CAF50; color: white; border-radius: 6px; }"
                                                              "QPushButton:hover { background-color: #45A049; }"
                                                            : "QPushButton { background-color: #E53935; color: white; border-radius: 6px; }"
                                                              "QPushButton:hover { background-color: #D32F2F; }");
                    }
                } });
}

void MainWindow::createSpringControls()
{
    // Get the layout of the Fconfig frame
    QVBoxLayout *configLayout = qobject_cast<QVBoxLayout *>(ui->Fconfig->layout());
    if (!configLayout)
    {
        configLayout = new QVBoxLayout(ui->Fconfig);
        configLayout->setAlignment(Qt::AlignTop);
    }

    // Add a spacer to separate from existing controls
    QLabel *springSectionLabel = new QLabel("Spring Controls:", ui->Fconfig);
    springSectionLabel->setStyleSheet("QLabel { font-weight: bold; color: #4CAF50; }");
    configLayout->addWidget(springSectionLabel);

    // Spring Mass Control
    QHBoxLayout *massLayout = new QHBoxLayout();
    springMassLabel = new QLabel("Mass Weight:", ui->Fconfig);
    springMassSpinBox = new QSpinBox(ui->Fconfig);
    springMassSpinBox->setRange(1, 50);
    springMassSpinBox->setValue(6);
    springMassSpinBox->setToolTip("Weight of the hanging mass (1-50)");
    massLayout->addWidget(springMassLabel);
    massLayout->addWidget(springMassSpinBox);
    configLayout->addLayout(massLayout);

    // Gravity Control
    QVBoxLayout *gravityLayout = new QVBoxLayout();
    gravityLabel = new QLabel("Gravity Strength:", ui->Fconfig);
    gravitySlider = new QSlider(Qt::Horizontal, ui->Fconfig);
    gravitySlider->setRange(500, 5000);
    gravitySlider->setValue(1800);
    gravitySlider->setToolTip("Strength of gravity (500-5000)");
    gravityValueLabel = new QLabel("1800", ui->Fconfig);
    gravityValueLabel->setAlignment(Qt::AlignCenter);
    gravityLayout->addWidget(gravityLabel);
    gravityLayout->addWidget(gravitySlider);
    gravityLayout->addWidget(gravityValueLabel);
    configLayout->addLayout(gravityLayout);

    // Stiffness Control
    QVBoxLayout *stiffnessLayout = new QVBoxLayout();
    stiffnessLabel = new QLabel("Spring Stiffness:", ui->Fconfig);
    stiffnessSlider = new QSlider(Qt::Horizontal, ui->Fconfig);
    stiffnessSlider->setRange(1, 100);
    stiffnessSlider->setValue(35);
    stiffnessSlider->setToolTip("Stiffness of the spring (1-100%)");
    stiffnessValueLabel = new QLabel("35%", ui->Fconfig);
    stiffnessValueLabel->setAlignment(Qt::AlignCenter);
    stiffnessLayout->addWidget(stiffnessLabel);
    stiffnessLayout->addWidget(stiffnessSlider);
    stiffnessLayout->addWidget(stiffnessValueLabel);
    configLayout->addLayout(stiffnessLayout);

    // Reset Button
    resetSpringButton = new QPushButton("Reset Spring System", ui->Fconfig);
    resetSpringButton->setStyleSheet(
        "QPushButton { background-color: #2196F3; color: white; padding: 5px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #1976D2; }");
    configLayout->addWidget(resetSpringButton);

    // Add stretch to push everything to top
    configLayout->addStretch();

    // Connect signals
    connect(springMassSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onSpringMassChanged);
    connect(gravitySlider, &QSlider::valueChanged,
            this, &MainWindow::onGravityChanged);
    connect(stiffnessSlider, &QSlider::valueChanged,
            this, &MainWindow::onStiffnessChanged);
    connect(resetSpringButton, &QPushButton::clicked,
            this, &MainWindow::onResetSpringClicked);
}

void MainWindow::showSpringControls(bool show)
{
    if (springMassLabel)
        springMassLabel->setVisible(show);
    if (springMassSpinBox)
        springMassSpinBox->setVisible(show);
    if (gravityLabel)
        gravityLabel->setVisible(show);
    if (gravitySlider)
        gravitySlider->setVisible(show);
    if (gravityValueLabel)
        gravityValueLabel->setVisible(show);
    if (stiffnessLabel)
        stiffnessLabel->setVisible(show);
    if (stiffnessSlider)
        stiffnessSlider->setVisible(show);
    if (stiffnessValueLabel)
        stiffnessValueLabel->setVisible(show);
    if (resetSpringButton)
        resetSpringButton->setVisible(show);
}

void MainWindow::onSpringMassChanged(int value)
{
    auto springWidget = openGLWidget->getSpringWidget();
    if (springWidget)
    {
        springWidget->setMass(static_cast<float>(value));
    }
}

void MainWindow::onGravityChanged(int value)
{
    auto springWidget = openGLWidget->getSpringWidget();
    if (springWidget)
    {
        springWidget->setGravity(static_cast<float>(value));
        if (gravityValueLabel)
        {
            gravityValueLabel->setText(QString::number(value));
        }
    }
}

void MainWindow::onStiffnessChanged(int value)
{
    auto springWidget = openGLWidget->getSpringWidget();
    if (springWidget)
    {
        // Map 1-100 to stiffness
        float stiffness = 0.1f + (static_cast<float>(value) - 1.0f) * (10.0f - 0.1f) / 99.0f;

        springWidget->setSpringStiffness(stiffness);
        if (stiffnessValueLabel)
        {
            stiffnessValueLabel->setText(QString::number(value) + "%");
        }

        qDebug() << "Stiffness:" << stiffness << "from slider:" << value;
    }
}
void MainWindow::onResetSpringClicked()
{
    auto springWidget = openGLWidget->getSpringWidget();
    if (springWidget)
    {
        springWidget->resetToInitialState();

        // Reset UI controls to default values
        if (springMassSpinBox)
            springMassSpinBox->setValue(6);
        if (gravitySlider)
            gravitySlider->setValue(1800);
        if (gravityValueLabel)
            gravityValueLabel->setText("1800");
        if (stiffnessSlider)
            stiffnessSlider->setValue(35);
        if (stiffnessValueLabel)
            stiffnessValueLabel->setText("35%");
    }
}

void MainWindow::setSimulationType(SimulationType type)
{
    if (openGLWidget)
    {
        openGLWidget->setSimulationType(type);
    }
    if (simulation)
    {
        simulation->setSimulationType(type);
    }
}

// Load Simulation from JSON file
void MainWindow::loadSimulation(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
    {
        std::cerr << "Failed to open file: " << filePath.toStdString() << std::endl;
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    if (simulation)
    {
        simulation->load(doc.object());

        auto simObj = doc.object()["simulation"].toObject();

        if (simObj.contains("damping"))
        {
            float damping = simObj["damping"].toDouble();
            ui->horizontalSlider->setValue(int((1.0 - damping) * 100));
        }

        if (simObj.contains("enableCollisions"))
        {
            bool collisions = simObj["enableCollisions"].toBool();
            ui->enableCollisions->setChecked(collisions);
        }

        if (simObj.contains("enableGravity"))
        {
            bool gravity = simObj["enableGravity"].toBool();
            ui->EnableDowardsGravity->setChecked(gravity);
        }

        // Set particle mass if particles exist
        if (simObj.contains("particles"))
        {
            QJsonArray particles = simObj["particles"].toArray();
            if (!particles.isEmpty())
            {
                float mass = float(particles[0].toObject()["mass"].toDouble());
                ui->inputSize->setValue(mass);
            }
        }
    }
}

// Slot implementation to update footer labels with live stats
void MainWindow::onStatsChanged(float fps, int objects)
{
    // Update UI if needed
}

qint64 MainWindow::getProcessMemoryUsage()
{
    qint64 memoryUsage = 0;

#ifdef Q_OS_MAC
    // macOS: Use ps command to get RSS memory
    processInfo->start("ps", QStringList() << "-o" << "rss=" << "-p" << QString::number(QCoreApplication::applicationPid()));
#elif defined(Q_OS_WIN)
    // Windows: Use tasklist command to get memory usage
    processInfo->start("tasklist", QStringList() << "/FI" << QString("PID eq %1").arg(QCoreApplication::applicationPid()) << "/FO" << "CSV" << "/NH");
#endif

    processInfo->waitForFinished();
    QString output = processInfo->readAllStandardOutput();

#ifdef Q_OS_MAC
    // On macOS, output is just the RSS value in KB
    memoryUsage = output.trimmed().toLongLong();
#elif defined(Q_OS_WIN)
    // On Windows, we need to parse the CSV output
    // Format: "applicationName.exe","PID","Session Name","Session#","Mem Usage"
    QStringList parts = output.split(",");
    if (parts.size() >= 5)
    {
        // Memory is in the format "123,456 K" - we need to extract just the number
        QString memStr = parts[4].replace("\"", "").replace(" K", "").replace(",", "");
        memoryUsage = memStr.toLongLong();
    }
#endif

    return memoryUsage;
}

// Update status bar to display memory usage
void MainWindow::updateMemoryStats()
{
    qint64 memUsage = getProcessMemoryUsage();
    float memUsageMB = memUsage / 1024.0; // Convert KB to MB

    if (ui && ui->labelMemory)
        ui->labelMemory->setText(QString("Memory: %1 MB").arg(memUsageMB, 0, 'f', 1));
}

// Slot for enable/disable gravity when checkbox is clicked
void MainWindow::on_EnableDowardsGravity_clicked(bool checked)
{
    auto particlesWidget = openGLWidget->getParticlesWidget();
    if (particlesWidget)
    {
        particlesWidget->setDownwardsGravity(checked);
    }
}

// Slot for enabling HIP GPU via button
void MainWindow::on_enableHIP_clicked(bool checked)
{
    // Try to set a common HIP environment variable so libraries that check it at startup
    // can pick it up. Setting to "0" will prefer device 0; empty unsets.
    if (checked)
    {
        qputenv("HIP_VISIBLE_DEVICES", QByteArray("0"));
    }
    else
    {
        qputenv("HIP_VISIBLE_DEVICES", QByteArray());
    }

    bool handled = false;

    // If SimulationFacade inherits QObject, attempt to call common method names via meta-object
    if (simulation)
    {
        if (QObject *obj = asQObject(simulation))
        {
            handled = QMetaObject::invokeMethod(obj, "enableHIP", Qt::DirectConnection, Q_ARG(bool, checked));
            if (!handled)
            {
                handled = QMetaObject::invokeMethod(obj, "setUseHIP", Qt::DirectConnection, Q_ARG(bool, checked));
            }
            if (!handled)
            {
                handled = QMetaObject::invokeMethod(obj, "setEnableHIP", Qt::DirectConnection, Q_ARG(bool, checked));
            }
        }
        // If SimulationFacade does NOT derive from QObject we can't use QMetaObject;
        // fall through and inform the user that the env var was set (backend may need restart).
    }

    if (handled)
    {
        statusBar()->showMessage(QString("HIP GPU %1").arg(checked ? "enabled" : "disabled"), 3000);
    }
    else
    {
        QMessageBox::information(this, "HIP Toggle",
                                 QString("HIP GPU %1.\nIf the simulation backend supports HIP it will use the setting on next startup.")
                                     .arg(checked ? "enabled" : "disabled"));
    }
}

// Slot for updating particle damping factor
void MainWindow::on_horizontalSlider_valueChanged(int value)
{
    auto particlesWidget = openGLWidget->getParticlesWidget();
    if (particlesWidget)
    {
        float newDampFactor = 1 - ((float)value) / 100;
        particlesWidget->setDamping(newDampFactor);
    }
}

// Slot for resetting simulation by recreating fresh instance with receonnected signals
void MainWindow::on_terminateButton_pressed()
{
    // Stop the simulation timer first
    auto particlesWidget = openGLWidget->getParticlesWidget();
    if (particlesWidget)
    {
        particlesWidget->pauseSimulation();
        ui->pausedButton->setText("Play Simulation");
        ui->pausedButton->setStyleSheet(
            "QPushButton { background-color: #4CAF50; color: white; border-radius: 6px; }"
            "QPushButton:hover { background-color: #45A049; }");
    }

    // Reset internal state
    openGLWidget->reset();

    qDebug() << "Simulation restarted";
}

void MainWindow::on_particleCountBox_valueChanged(int arg1)
{
    auto particlesWidget = openGLWidget->getParticlesWidget();
    if (particlesWidget)
    {
        this->on_terminateButton_pressed();
        openGLWidget->particle_count = arg1;
        openGLWidget->reset();
    }
}
