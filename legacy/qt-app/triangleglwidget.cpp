#include "triangleglwidget.h"

TriangleGLWidget::TriangleGLWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
}

TriangleGLWidget::~TriangleGLWidget() {}

// Sets up OpenGL settings and runs once when the widget is shown
void TriangleGLWidget::initializeGL()
{
    initializeOpenGLFunctions(); // This does some graphical magic (for rendering OpenGL stuff)

    // Set background color from clear to cyan
    glClearColor(0.5, 1, 1, 1);
}

// Renders the actual triangle which is called when requested by Qt to update upon each frame
void TriangleGLWidget::paintGL()
{
    // Set every pixel in the frame buffer to the current clear color
    glClear(GL_COLOR_BUFFER_BIT);

    // Start issuing GL to draw triangle
    glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.0f, 0.0f); // First vertex is red at the top of the triangle anc in the center
        glVertex3f(0.0f, 0.707f, 0.0f);

        glColor3f(0.0f, 1.0f, 0.0f); // Second vertex is green in the bottom left corner
        glVertex3f(-0.5f, -0.5f, 0.0f);

        glColor3f(0.0f, 0.0f, 1.0f); // Thirs vertex is blue in the bottom right corner
        glVertex3f(0.5f, -0.5f, 0.0f);
    glEnd();
}
