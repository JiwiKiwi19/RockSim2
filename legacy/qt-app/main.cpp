#include "mainwindow.h"
#include "homescreen.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QFile>
#include <QStyleFactory>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon("ROCsim_logo.png"));

    // Force Fusion style to ensure QSS works correctly
    app.setStyle(QStyleFactory::create("Fusion"));
    QFile styleSheetFile("style.qss");
    if (styleSheetFile.open(QFile::ReadOnly))
    {
        // Read file into string and apply as stylesheet
        QString styleSheet = QLatin1String(styleSheetFile.readAll());
        app.setStyleSheet(styleSheet);
        qDebug() << "Successfully loaded style.qss";
    }
    else
    {
        qDebug() << "Failed to load style.qss";
    }

    // Translation system for multi-language support
    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages)
    {
        const QString baseName = "HelloWorld_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName))
        {
            app.installTranslator(&translator);
            break;
        }
    }

    // Show homescreen first (user can create simulation or load)
    homescreen h;
    h.show();
    return app.exec();
}
