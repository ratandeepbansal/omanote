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
#include <QDate>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>
#include <QStandardPaths>
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
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Markdown file to open (or a title with --new)"));
    const QCommandLineOption notesDirOption(
        QStringLiteral("notes-dir"), QStringLiteral("Folder that holds the notes"),
        QStringLiteral("dir"));
    parser.addOption(notesDirOption);
    const QCommandLineOption newOption(
        QStringLiteral("new"), QStringLiteral("Create a note in the running window; a positional argument is its title"));
    const QCommandLineOption dailyOption(QStringLiteral("daily"), QStringLiteral("Open today's note in Daily/"));
    const QCommandLineOption searchOption(
        QStringLiteral("search"), QStringLiteral("Focus the sidebar search with this text"), QStringLiteral("text"));
    const QCommandLineOption newWindowOption(
        QStringLiteral("new-window"), QStringLiteral("Always start a separate window"));
    parser.addOption(newOption);
    parser.addOption(dailyOption);
    parser.addOption(searchOption);
    parser.addOption(newWindowOption);
    parser.process(app);

    // Commands from the shell (omanote --new, --daily, --search, a file path)
    // are handed to an already running window over a local socket, so a
    // global hotkey feels instant instead of spawning a second instance.
    QJsonObject command;
    if (parser.isSet(newOption))
        command.insert(QStringLiteral("new"), parser.positionalArguments().join(QLatin1Char(' ')));
    if (parser.isSet(dailyOption))
        command.insert(QStringLiteral("daily"), true);
    if (parser.isSet(searchOption))
        command.insert(QStringLiteral("search"), parser.value(searchOption));
    if (!parser.isSet(newOption) && !parser.positionalArguments().isEmpty())
        command.insert(QStringLiteral("open"), QDir::current().absoluteFilePath(parser.positionalArguments().first()));

    const QString socketName = QStringLiteral("omanote-%1")
        .arg(qEnvironmentVariable("USER", QStringLiteral("user")));
    if (!parser.isSet(newWindowOption) && !parser.isSet(notesDirOption)) {
        QLocalSocket client;
        client.connectToServer(socketName);
        if (client.waitForConnected(300)) {
            client.write(QJsonDocument(command).toJson(QJsonDocument::Compact));
            client.flush();
            client.waitForBytesWritten(1000);
            return 0;
        }
    }

    Backend backend(&app);
    NotesModel notesModel(&app);
    {
        QSettings settings;
        // A --notes-dir override is for this run only; it must never be
        // written into the config, or a one-off test folder becomes permanent.
        QString notesDir = parser.value(notesDirOption);
        if (notesDir.isEmpty()) {
            notesDir = settings.value(QStringLiteral("notes/folder")).toString();
            if (notesDir.isEmpty()) {
                notesDir = Backend::defaultNotesDir();
                settings.setValue(QStringLiteral("notes/folder"), notesDir);
            }
        }
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

    QObject *root = engine.rootObjects().constFirst();
    backend.setParentWindow(qobject_cast<QWindow *>(root));

    const auto runCommand = [&backend, &notesModel, root](const QJsonObject &cmd) {
        if (backend.modified() && backend.autosaveActive())
            backend.saveNow();
        if (cmd.contains(QStringLiteral("new"))) {
            const QString title = cmd.value(QStringLiteral("new")).toString();
            const QString path = title.isEmpty() ? notesModel.createNote() : notesModel.createNoteTitled(title);
            if (!path.isEmpty())
                backend.openPath(path);
        } else if (cmd.value(QStringLiteral("daily")).toBool()) {
            const QString path = notesModel.dailyNotePath(QDate::currentDate());
            if (!path.isEmpty())
                backend.openPath(path);
        } else if (cmd.contains(QStringLiteral("open"))) {
            backend.open(QUrl::fromLocalFile(cmd.value(QStringLiteral("open")).toString()));
        }
        if (cmd.contains(QStringLiteral("search")))
            QMetaObject::invokeMethod(root, "searchNotes", Q_ARG(QVariant, cmd.value(QStringLiteral("search")).toString()));
        backend.raiseWindow();
        if (!cmd.contains(QStringLiteral("search")))
            QMetaObject::invokeMethod(root, "focusEditor");
    };

    QLocalServer server(&app);
    if (!parser.isSet(newWindowOption) && !parser.isSet(notesDirOption)) {
        QLocalServer::removeServer(socketName);
        if (server.listen(socketName)) {
            QObject::connect(&server, &QLocalServer::newConnection, &app, [&server, runCommand]() {
                while (QLocalSocket *socket = server.nextPendingConnection()) {
                    QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, runCommand]() {
                        const QJsonObject cmd = QJsonDocument::fromJson(socket->readAll()).object();
                        socket->deleteLater();
                        runCommand(cmd);
                    });
                    QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                }
            });
        }
    }

    const QStringList args = parser.positionalArguments();
    if (!command.isEmpty() && !backend.modified()) {
        runCommand(command);
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
