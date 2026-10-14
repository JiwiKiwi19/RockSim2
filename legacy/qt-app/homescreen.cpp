#include "homescreen.h"
#include "ui_homescreen.h"
#include "mainwindow.h"
#include "newsimulation.h"

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
#include <iostream>
#include <QFileDialog>
#include <QPixmap>
#include <QApplication>

homescreen::homescreen(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::homescreen)
{
    ui->setupUi(this);

    this->showMaximized();
    setWindowTitle("ROCsim");

    // Show ROCsim picture
    QPixmap pic(":/picture/ROCsim_logo.png");
    ui->rocsim_pic->setPixmap(pic.scaled(500, 500, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->rocsim_pic->setAlignment(Qt::AlignCenter);

    // Link New Particle Simulation button to MainWindow class
    connect(ui->pushParticleModel, &QPushButton::clicked, this, [this]()
            {
        MainWindow *w = new MainWindow();
        w->setSimulationType(SimulationType::Particles);
        w->show();
        this->close(); });

    // Link New Spring Simulation button to MainWindow class
    connect(ui->pushSpringModel, &QPushButton::clicked, this, [this]()
            {
        MainWindow *w = new MainWindow();
        w->setSimulationType(SimulationType::Springs);
        w->show();
        this->close(); });

    // Link Load Simulation button to open JSON file
    connect(ui->pushLoadSimulation, &QPushButton::clicked, this, [this]()
            {
        QString filePath = QFileDialog::getOpenFileName(
            this,
            tr("Open Simulation"),
            QDir::homePath(),
            tr("JSON Files (*.json);;All Files (*.*)") // file filter
            );

        if (!filePath.isEmpty()) {
            qDebug() << "Selected JSON file:" << filePath;

            // Create the MainWindow
            MainWindow *w = new MainWindow();

            // Load the selected simulation JSON
            w->loadSimulation(filePath);

            w->show();
            this->close();
        } });

    /* Menu Bar Connection */
    // Exit
    connect(ui->actionExit, &QAction::triggered,
            qApp, &QApplication::quit);

    // About ROCsim
    connect(ui->actionAbout_ROCsim, &QAction::triggered, this, []()
            { QDesktopServices::openUrl(
                  QUrl("https://github.com/Fall-2025-CS-3307/oop-assignment-3-rockers")); });

    // Exit Program
    connect(ui->pushExit, &QPushButton::clicked, qApp, &QApplication::quit);
}

homescreen::~homescreen()
{
    delete ui;
}