#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QFrame>
#include <QList>
#include <QProcess>
#include <QTimer>
#include <QSpinBox>
#include <QSlider>
#include <QLabel>
#include "simulationfacade.h"
#include "springmassglwidget.h"

// Forward declarations
class TopToolbar;
class OpenGLWidget;

QT_BEGIN_NAMESPACE
namespace Ui
{
    class MainWindow;
}
QT_END_NAMESPACE

// Accordion Right Bar Section that is collapsible when button is clicked
class AccordionSection : public QObject
{
    Q_OBJECT
public:
    AccordionSection(QPushButton *headerButton, QFrame *contentFrame, QObject *parent = nullptr)
        : QObject(parent), m_headerButton(headerButton), m_contentFrame(contentFrame)
    {
        m_contentFrame->setVisible(false);
        connect(m_headerButton, &QPushButton::clicked, this, &AccordionSection::toggle);
    }

private slots:
    void toggle()
    {
        m_contentFrame->setVisible(!m_contentFrame->isVisible());
    }

private:
    QPushButton *m_headerButton;
    QFrame *m_contentFrame;
};

// MainWindow Class
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void loadSimulation(const QString &filePath);
    void setSimulationType(SimulationType type);

private slots:
    void on_EnableDowardsGravity_clicked(bool checked);
    void on_enableHIP_clicked(bool checked);
    void on_horizontalSlider_valueChanged(int value);
    void onStatsChanged(float fps, int objects);
    void on_terminateButton_pressed();
    void on_particleCountBox_valueChanged(int arg1);

    // Spring control slots
    void onSpringMassChanged(int value);
    void onGravityChanged(int value);
    void onStiffnessChanged(int value);
    void onResetSpringClicked();

private:
    void setupUI();
    void testHIP();
    qint64 getProcessMemoryUsage();
    void updateMemoryStats();
    void createSpringControls();
    void showSpringControls(bool show);

    Ui::MainWindow *ui;
    TopToolbar *topToolbar;
    OpenGLWidget *openGLWidget;

    QProcess *processInfo;
    QTimer *memoryTimer;

    QList<AccordionSection *> sections;
    SimulationFacade *simulation = nullptr;

    // Spring control widgets
    QLabel *springMassLabel = nullptr;
    QSpinBox *springMassSpinBox = nullptr;
    QLabel *gravityLabel = nullptr;
    QSlider *gravitySlider = nullptr;
    QLabel *gravityValueLabel = nullptr;
    QLabel *stiffnessLabel = nullptr;
    QSlider *stiffnessSlider = nullptr;
    QLabel *stiffnessValueLabel = nullptr;
    QPushButton *resetSpringButton = nullptr;

    // Store current values for display
    float lastFps = 0.0f;
    int lastObjects = 0;
    qint64 lastMemUsageKB = 0;
};

#endif // MAINWINDOW_H
