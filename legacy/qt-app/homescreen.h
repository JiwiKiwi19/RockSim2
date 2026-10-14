#ifndef HOMESCREEN_H
#define HOMESCREEN_H

#include <QMainWindow>

namespace Ui {
class homescreen;
}

class homescreen : public QMainWindow
{
    Q_OBJECT

public:
    explicit homescreen(QWidget *parent = nullptr);
    ~homescreen();

private:
    Ui::homescreen *ui;
};

#endif // HOMESCREEN_H
