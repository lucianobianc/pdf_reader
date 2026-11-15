#include "reader_window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Vireo");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setOrganizationName("Vireo");
    QCommandLineParser parser;
    parser.setApplicationDescription("A PDF reader with vi navigation and separate annotations.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"l", "language"}, "Interface language (en or pt_BR).", "locale"});
    parser.addPositionalArgument("pdf", "PDF document to open.", "[pdf]");
    parser.process(app);
    const QLocale locale(parser.isSet("language") ? parser.value("language") : QLocale::system().name());
    QLocale::setDefault(locale);
    QTranslator applicationTranslation;
    QTranslator qtTranslation;
    if (qtTranslation.load(locale, "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslation);
    for (const auto &directory : {QCoreApplication::applicationDirPath() + "/translations",
            QDir(QCoreApplication::applicationDirPath()).filePath("../share/vireo/translations")}) {
        if (applicationTranslation.load(locale, "vireo", "_", directory)) {
            app.installTranslator(&applicationTranslation);
            break;
        }
    }
    ReaderWindow window;
    window.show();
    if (!parser.positionalArguments().isEmpty()) window.openDocument(parser.positionalArguments().first());
    return app.exec();
}
