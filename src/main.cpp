#include "reader_window.h"
#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Nightreader");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setOrganizationName("Nightreader");
    QCommandLineParser parser;
    parser.setApplicationDescription("A PDF reader with vi navigation and separate annotations.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("pdf", "PDF document to open.", "[pdf]");
    parser.process(app);
    ReaderWindow window;
    window.show();
    if (!parser.positionalArguments().isEmpty()) window.openDocument(parser.positionalArguments().first());
    return app.exec();
}
