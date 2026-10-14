#include "newsimulation.h"
#include "ui_newsimulation.h"

#include <QFileDialog>
#include <QPixmap>
#include <QUrl>
#include <iostream>

newsimulation::newsimulation(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::newsimulation)
{
    ui->setupUi(this);
    this->setFixedSize(750, 500);


    // Show ROCsim picture
    QPixmap pic(":/picture/ROCsim_logo.png");
    int w = ui->rocsim_pic->width();
    int h = ui->rocsim_pic->height();
    ui->rocsim_pic->setPixmap(pic.scaled(500,500,Qt::KeepAspectRatio));
    connect(ui->pushButton, &QPushButton::clicked, this, &newsimulation::onPushButtonNewSimulationClicked);
    connect(ui->pushButton_2, &QPushButton::clicked, this, &newsimulation::on_pushButton_2_clicked);
}

newsimulation::~newsimulation()
{
    delete ui;
}

void newsimulation::onPushButtonNewSimulationClicked() {
    emit newParticleSimulation();
    accept();  // Close dialog
}

// Open file browser dialog to let user select a JSON file
void newsimulation::showFiles() {
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open Simulation"),
        QDir::homePath(),
        tr("JSON Files (*.json);;All Files (*.*)") // File filters
        );

    if (!filePath.isEmpty()) {
        std::cout << "File selected: " << filePath.toStdString() << std::endl;

        // Emit the signal to notify other parts of the program
        emit simulationSelected(filePath);

        accept();
    }
}


void newsimulation::on_pushButton_2_clicked()
{
    showFiles();
}

