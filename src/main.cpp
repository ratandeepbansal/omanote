#include <QFont>
#include <QFontDatabase>
#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QUrl>
#include <QWindow>
#include <QFile>

#include "backend.h"
#include "notesmodel.h"
#include <QCommandLineParser>
#include <QSettings>
#include "systemtheme.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omanote"));
    app.setDesktopFileName(QStringLiteral("omanote"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("omanote")));

    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-Italic.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-Bold.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/iAWriterMonoS-BoldItalic.ttf"));
    app.setOrganizationName(QStringLiteral("Omacom"));
    app.setOrganizationDomain(QStringLiteral("omacom.io"));

    QQuickStyle::setStyle(QStringLiteral("Material"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Markdown notes with a sidebar"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Markdown file to open"));
    const QCommandLineOption notesDirOption(
        QStringLiteral("notes-dir"), QStringLiteral("Folder that holds the notes"),
        QStringLiteral("dir"));
    parser.addOption(notesDirOption);
    parser.process(app);

    Backend backend(&app);
    NotesModel notesModel(&app);
    {
        QSettings settings;
        QString notesDir = parser.value(notesDirOption);
        if (notesDir.isEmpty())
            notesDir = settings.value(QStringLiteral("notes/folder")).toString();
        if (notesDir.isEmpty())
            notesDir = Backend::defaultNotesDir();
        if (!settings.contains(QStringLiteral("notes/folder")))
            settings.setValue(QStringLiteral("notes/folder"), notesDir);
        backend.setNotesDir(notesDir);
        notesModel.setNotesDir(notesDir);
    }
    QObject::connect(&backend, &Backend::fileSaved, &notesModel,
                     [&notesModel](const QString &) { notesModel.refresh(); });
    SystemTheme systemTheme(&app);
    backend.setDarkMode(systemTheme.darkMode());
    QObject::connect(&systemTheme, &SystemTheme::darkModeChanged, &backend,
                     &Backend::setDarkMode);

    // Carry the desktop's text scale into the default font, so the chrome that
    // inherits it (dialog titles, buttons) grows along with the writing area.
    const QFont interfaceFont(QStringLiteral("iA Writer Mono S"));
    const qreal basePointSize = interfaceFont.pointSizeF() > 0
        ? interfaceFont.pointSizeF()
        : app.font().pointSizeF();
    const auto applyInterfaceFont = [&app, interfaceFont, basePointSize](qreal textScale) {
        QFont scaled = interfaceFont;
        scaled.setPointSizeF(basePointSize * textScale);
        app.setFont(scaled);
    };
    applyInterfaceFont(systemTheme.textScale());

    backend.setTextScale(systemTheme.textScale());
    QObject::connect(&systemTheme, &SystemTheme::textScaleChanged, &backend,
                     [&backend, applyInterfaceFont](qreal textScale) {
        applyInterfaceFont(textScale);
        backend.setTextScale(textScale);
    });

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            qWarning().noquote() << warning.toString();
    });
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("notesModel"), &notesModel);

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Could not load the Omanote interface; resource available:"
                    << QFile::exists(QStringLiteral(":/Main.qml"));
        return -1;
    }

    backend.setParentWindow(qobject_cast<QWindow *>(engine.rootObjects().constFirst()));

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty() && !backend.modified()) {
        backend.open(QUrl::fromLocalFile(args.at(0)));
    } else if (!backend.modified() && !backend.fileUrl().isValid()) {
        // Land on the most recent note so the app opens into the library.
        // An empty library gets a starter note so typing autosaves right away.
        QString first = notesModel.pathAt(0);
        if (first.isEmpty())
            first = notesModel.createNote();
        if (!first.isEmpty())
            backend.open(QUrl::fromLocalFile(first));
    }

    return app.exec();
}
