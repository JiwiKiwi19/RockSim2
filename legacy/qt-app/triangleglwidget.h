#ifndef TRIANGLEGLWIDGET_H
#define TRIANGLEGLWIDGET_H

#include <QObject>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>

// Displays a single colored triangle
class TriangleGLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    TriangleGLWidget(QWidget *parent = nullptr);
    ~TriangleGLWidget();

protected:
    // Called automatically when OpenGL context is created to initialize GL state
    void initializeGL();

    // Called every time the widget must redraw to paint the triangle
    void paintGL();
};

#endif // TRIANGLEGLWIDGET_H
