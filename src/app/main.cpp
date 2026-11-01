#include "app/mainwindow.h"
#include "ui/theme.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName(QStringLiteral("CalibStudio"));
    calib::theme::apply(a);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for(const QString& locale : uiLanguages)
    {
        const QString baseName = "CalibStudio_" + QLocale(locale).name();
        if(translator.load(":/i18n/" + baseName))
        {
            a.installTranslator(&translator);
            break;
        }
    }
    MainWindow w;
    w.show();
    return a.exec();
}
