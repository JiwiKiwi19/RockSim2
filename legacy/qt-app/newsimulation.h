#ifndef NEWSIMULATION_H
#define NEWSIMULATION_H

#include <QDialog>

namespace Ui {
class newsimulation;
}

class newsimulation : public QDialog
{
    Q_OBJECT

public:
    explicit newsimulation(QWidget *parent = nullptr);
    ~newsimulation();

signals:
    void simulationSelected(const QString& filePath);
    void newParticleSimulation();
    void newSpringSimulation();

private slots:
    void on_pushButton_2_clicked();
    void onPushButtonNewSimulationClicked();

private:
    Ui::newsimulation *ui;

    void showFiles();
};

#endif // NEWSIMULATION_H
