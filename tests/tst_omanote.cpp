#include <QtTest>
#include <QFont>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickTextDocument>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "backend.h"
#include "markdownhighlighter.h"
#include "notesmodel.h"

class OmanoteTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QQuickStyle::setStyle(QStringLiteral("Material"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    void extractsNoteTitlesAndPreviews() {
        QCOMPARE(NotesModel::titleFor(QStringLiteral("# Groceries\n\n- milk"), QStringLiteral("a.md")),
                 QStringLiteral("Groceries"));
        QCOMPARE(NotesModel::previewFor(QStringLiteral("# Groceries\n\n- **milk**\n- eggs")),
                 QStringLiteral("milk"));
        QCOMPARE(NotesModel::titleFor(QStringLiteral("\n\n  \n"), QStringLiteral("Untitled 2.md")),
                 QStringLiteral("Untitled 2"));
        QCOMPARE(NotesModel::previewFor(QStringLiteral("Only a title")), QString());
        QCOMPARE(NotesModel::stripMarkdown(QStringLiteral("> see [docs](http://x) `now`")),
                 QStringLiteral("see docs now"));
    }

    void formatsDates() {
        const QDateTime now(QDate(2026, 9, 4), QTime(12, 0));
        QCOMPARE(NotesModel::dateLabel(QDateTime(QDate(2026, 9, 4), QTime(9, 0)), now),
                 QStringLiteral("9:00 AM"));
        QCOMPARE(NotesModel::dateLabel(QDateTime(QDate(2026, 9, 3), QTime(9, 0)), now),
                 QStringLiteral("Yesterday"));
        QCOMPARE(NotesModel::dateLabel(QDateTime(QDate(2026, 9, 1), QTime(9, 0)), now),
                 QStringLiteral("Tuesday"));
        QCOMPARE(NotesModel::dateLabel(QDateTime(QDate(2026, 8, 4), QTime(9, 0)), now),
                 QStringLiteral("Aug 4"));
        QCOMPARE(NotesModel::dateLabel(QDateTime(QDate(2025, 12, 25), QTime(9, 0)), now),
                 QStringLiteral("Dec 25, 2025"));
    }

    void listsSortsFiltersAndPinsNotes() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto write = [&dir](const QString &name, const QString &text) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            file.write(text.toUtf8());
        };
        write(QStringLiteral("old.md"), QStringLiteral("# Old note\nabout cats"));
        {
            QFile old(dir.filePath(QStringLiteral("old.md")));
            QVERIFY(old.open(QIODevice::ReadWrite));
            QVERIFY(old.setFileTime(QDateTime::currentDateTime().addDays(-2),
                                    QFileDevice::FileModificationTime));
        }
        write(QStringLiteral("new.md"), QStringLiteral("# New note\nabout dogs"));
        write(QStringLiteral("ignored.txt"), QStringLiteral("not markdown"));

        NotesModel model;
        model.setNotesDir(dir.path());
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0), NotesModel::TitleRole).toString(),
                 QStringLiteral("New note"));
        QCOMPARE(model.data(model.index(1), NotesModel::TitleRole).toString(),
                 QStringLiteral("Old note"));

        model.setPinned(dir.filePath(QStringLiteral("old.md")), true);
        QCOMPARE(model.data(model.index(0), NotesModel::TitleRole).toString(),
                 QStringLiteral("Old note"));
        QVERIFY(model.data(model.index(0), NotesModel::PinnedRole).toBool());

        NotesModel reloaded;
        reloaded.setNotesDir(dir.path());
        QVERIFY(reloaded.isPinned(dir.filePath(QStringLiteral("old.md"))));

        model.setPinned(dir.filePath(QStringLiteral("old.md")), false);
        model.setSortMode(QStringLiteral("title"));
        QCOMPARE(model.data(model.index(0), NotesModel::TitleRole).toString(),
                 QStringLiteral("New note"));
        QCOMPARE(model.data(model.index(1), NotesModel::TitleRole).toString(),
                 QStringLiteral("Old note"));
        model.setSortMode(QStringLiteral("bogus"));
        QCOMPARE(model.sortMode(), QStringLiteral("modified"));

        model.setFilter(QStringLiteral("dogs"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.pathAt(0), dir.filePath(QStringLiteral("new.md")));
        model.setFilter(QString());
        QCOMPARE(model.rowCount(), 2);
    }

    void createsRenamesAndRemovesNotes() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        NotesModel model;
        model.setNotesDir(dir.path());

        const QString first = model.createNote();
        QCOMPARE(QFileInfo(first).fileName(), QStringLiteral("Untitled.md"));
        const QString second = model.createNote();
        QCOMPARE(QFileInfo(second).fileName(), QStringLiteral("Untitled 2.md"));
        QCOMPARE(model.totalCount(), 2);

        QVERIFY(model.renameNote(second, QStringLiteral("ideas")));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("ideas.md"))));
        QVERIFY(!model.renameNote(first, QStringLiteral("ideas.md")));

        QSignalSpy removed(&model, &NotesModel::noteRemoved);
        QVERIFY(model.removeNote(first));
        QCOMPARE(removed.count(), 1);
        QVERIFY(!QFileInfo::exists(first));
        QCOMPARE(model.totalCount(), 1);
    }

    void organisesNotesIntoFolders() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto write = [&](const QString &name, const QString &text) {
            QDir().mkpath(QFileInfo(dir.filePath(name)).absolutePath());
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            file.write(text.toUtf8());
        };
        write(QStringLiteral("root.md"), QStringLiteral("# Root"));
        write(QStringLiteral("Work/plan.md"), QStringLiteral("# Plan"));
        write(QStringLiteral(".hidden/x.md"), QStringLiteral("# Hidden"));
        write(QStringLiteral("assets/img.md"), QStringLiteral("# Asset"));

        NotesModel model;
        model.setNotesDir(dir.path());
        QCOMPARE(model.folders(), QStringList{QStringLiteral("Work")});
        QCOMPARE(model.totalCount(), 2);
        QCOMPARE(model.folderOf(dir.filePath(QStringLiteral("Work/plan.md"))), QStringLiteral("Work"));

        model.setFolder(QStringLiteral("Work"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), NotesModel::TitleRole).toString(), QStringLiteral("Plan"));
        // New notes land in the selected folder.
        const QString created = model.createNote();
        QVERIFY(created.startsWith(dir.filePath(QStringLiteral("Work/"))));
        QCOMPARE(model.rowCount(), 2);
        model.setFolder(QString());

        QVERIFY(!model.createFolder(QStringLiteral(".secret")));
        QVERIFY(!model.createFolder(QStringLiteral("assets")));
        QVERIFY(model.createFolder(QStringLiteral("Home")));
        QCOMPARE(model.folders(), (QStringList{QStringLiteral("Home"), QStringLiteral("Work")}));

        const QString root = dir.filePath(QStringLiteral("root.md"));
        model.setPinned(root, true);
        QVERIFY(model.moveNote(root, QStringLiteral("Home")));
        const QString moved = dir.filePath(QStringLiteral("Home/root.md"));
        QVERIFY(QFileInfo::exists(moved));
        QVERIFY(model.isPinned(moved));
        QVERIFY(!model.moveNote(moved, QStringLiteral("Nope")));

        QVERIFY(model.renameFolder(QStringLiteral("Home"), QStringLiteral("Personal")));
        QVERIFY(model.isPinned(dir.filePath(QStringLiteral("Personal/root.md"))));
        QCOMPARE(model.folders(), (QStringList{QStringLiteral("Personal"), QStringLiteral("Work")}));

        QSignalSpy removed(&model, &NotesModel::noteRemoved);
        QVERIFY(model.removeFolder(QStringLiteral("Personal")));
        QCOMPARE(removed.count(), 1);
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("Personal"))));
        QCOMPARE(model.folders(), QStringList{QStringLiteral("Work")});
    }

    void extractsAndFiltersTags() {
        QCOMPARE(NotesModel::tagsFor(QStringLiteral("# Heading\nbuy #Milk and #eggs/free-range\n#123 no\n`#code` no\n```\n#fenced\n```\nemail@x.com #a_b")),
                 (QStringList{QStringLiteral("a_b"), QStringLiteral("eggs/free-range"), QStringLiteral("milk")}));
        QCOMPARE(NotesModel::tagsFor(QStringLiteral("---\ntitle: x\ntags: [Work, \"home\"]\n---\n# T")),
                 (QStringList{QStringLiteral("home"), QStringLiteral("work")}));
        QCOMPARE(NotesModel::tagsFor(QStringLiteral("---\ntags:\n  - one\n  - two\n---\n")),
                 (QStringList{QStringLiteral("one"), QStringLiteral("two")}));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto write = [&](const QString &name, const QString &text) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            file.write(text.toUtf8());
        };
        write(QStringLiteral("a.md"), QStringLiteral("# A\n#work #urgent"));
        write(QStringLiteral("b.md"), QStringLiteral("# B\n#work"));
        write(QStringLiteral("c.md"), QStringLiteral("# C\nnothing"));

        NotesModel model;
        model.setNotesDir(dir.path());
        QCOMPARE(model.tags(), (QStringList{QStringLiteral("urgent"), QStringLiteral("work")}));
        model.setTag(QStringLiteral("work"));
        QCOMPARE(model.rowCount(), 2);
        model.setFilter(QStringLiteral("tag:urgent"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), NotesModel::TitleRole).toString(), QStringLiteral("A"));
        model.setTag(QString());
        model.setFilter(QStringLiteral("#work B"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), NotesModel::TagsRole).toStringList(), QStringList{QStringLiteral("work")});
    }

    void resolvesWikiLinkTitles() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile file(dir.filePath(QStringLiteral("ideas.md")));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("# Big Ideas\n");
        file.close();

        NotesModel model;
        model.setNotesDir(dir.path());
        QCOMPARE(model.pathForTitle(QStringLiteral("big ideas")), dir.filePath(QStringLiteral("ideas.md")));
        QCOMPARE(model.pathForTitle(QStringLiteral("Ideas")), dir.filePath(QStringLiteral("ideas.md")));
        QCOMPARE(model.pathForTitle(QStringLiteral("nope")), QString());

        const QString created = model.createNoteTitled(QStringLiteral("Trip: Japan/2027"));
        QVERIFY(created.endsWith(QStringLiteral("Trip- Japan-2027.md")));
        QCOMPARE(model.pathForTitle(QStringLiteral("Trip: Japan/2027")), created);
        QVERIFY(model.createNoteTitled(QStringLiteral("Trip: Japan/2027")).endsWith(QStringLiteral(" 2.md")));
    }

    void convertsTables() {
        const QList<QStringList> fromTsv = Backend::tableRowsFromTsv(QStringLiteral("Name\tQty\nApples\t3\nPears\t12\n"));
        QCOMPARE(fromTsv.size(), 3);
        QCOMPARE(Backend::markdownTable(fromTsv),
                 QStringLiteral("| Name   | Qty |\n| ------ | --- |\n| Apples | 3   |\n| Pears  | 12  |\n"));
        QVERIFY(Backend::tableRowsFromTsv(QStringLiteral("just one line\twith tab")).isEmpty());
        QVERIFY(Backend::tableRowsFromTsv(QStringLiteral("a\nb\n")).isEmpty());

        const QList<QStringList> fromHtml = Backend::tableRowsFromHtml(
            QStringLiteral("<html><body><table><tr><th>A</th><th>B</th></tr><tr><td>1 &amp; 2</td><td><b>x</b></td></tr></table></body></html>"));
        QCOMPARE(fromHtml.size(), 2);
        QCOMPARE(fromHtml.at(1), (QStringList{QStringLiteral("1 & 2"), QStringLiteral("x")}));

        const QString markdown = QStringLiteral("| A | B |\n|---|:-:|\n| a\\|b | c |");
        const QList<QStringList> parsed = Backend::tableRowsFromMarkdown(markdown);
        QCOMPARE(parsed.size(), 2);
        QCOMPARE(parsed.at(1).at(0), QStringLiteral("a|b"));
        QCOMPARE(Backend::htmlTable(parsed),
                 QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\">\n<tr><th>A</th><th>B</th></tr>\n<tr><td>a|b</td><td>c</td></tr>\n</table>\n"));
    }

    void autosavesNotesInsideTheNotesFolder() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Backend backend;
        backend.setNotesDir(dir.path());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nTextEdit {}", QUrl());
        QScopedPointer<QObject> edit(component.create());
        QVERIFY2(edit, qPrintable(component.errorString()));
        QObject *quickDocument = edit->property("textDocument").value<QObject *>();
        QVERIFY(quickDocument);
        backend.attachDocument(quickDocument);
        auto *document = static_cast<QQuickTextDocument *>(quickDocument)->textDocument();

        QVERIFY(backend.isNotePath(dir.filePath(QStringLiteral("x.md"))));
        QVERIFY(!backend.isNotePath(QStringLiteral("/tmp/elsewhere.md")));

        const QString path = dir.filePath(QStringLiteral("note.md"));
        { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("# Hi\n"); }
        backend.open(QUrl::fromLocalFile(path));
        QVERIFY(backend.autosaveActive());

        QSignalSpy saved(&backend, &Backend::fileSaved);
        document->setPlainText(QStringLiteral("# Hi\nchanged"));
        QVERIFY(backend.editorTextChanged());
        QVERIFY(backend.modified());
        QVERIFY(saved.wait(3000));
        QVERIFY(!backend.modified());
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()), QStringLiteral("# Hi\nchanged"));
    }

    void drivesNotesSidebarThroughMainWindow() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        Backend backend;
        NotesModel model;
        backend.setNotesDir(dir.path());
        model.setNotesDir(dir.path());
        QObject::connect(&backend, &Backend::fileSaved, &model, [&model]() { model.refresh(); });

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("notesModel"), &model);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        // New note lands in the folder, becomes current, and autosaves typing.
        QVERIFY(QMetaObject::invokeMethod(window.data(), "newNote"));
        const QString first = dir.filePath(QStringLiteral("Untitled.md"));
        QVERIFY(QFileInfo::exists(first));
        QCOMPARE(backend.filePath(), first);
        QVERIFY(backend.autosaveActive());
        QSignalSpy saved(&backend, &Backend::fileSaved);
        editor->setProperty("text", QStringLiteral("# First note\nbody one"));
        QVERIFY(saved.wait(3000));
        QCOMPARE(model.data(model.index(0), NotesModel::TitleRole).toString(),
                 QStringLiteral("First note"));

        // Second note; switching back saves without prompting.
        QVERIFY(QMetaObject::invokeMethod(window.data(), "newNote"));
        const QString second = dir.filePath(QStringLiteral("Untitled 2.md"));
        QCOMPARE(backend.filePath(), second);
        editor->setProperty("text", QStringLiteral("# Second note\nbody two"));
        QVERIFY(backend.modified());
        QVERIFY(QMetaObject::invokeMethod(window.data(), "openNote", Q_ARG(QVariant, first)));
        QCOMPARE(backend.filePath(), first);
        QVERIFY(!backend.modified());
        QCOMPARE(editor->property("text").toString(), QStringLiteral("# First note\nbody one"));
        {
            QFile f(second);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(QString::fromUtf8(f.readAll()), QStringLiteral("# Second note\nbody two"));
        }

        // Pin the current note: it moves to the top and persists.
        QCOMPARE(model.pathAt(0), second);
        QVERIFY(QMetaObject::invokeMethod(window.data(), "togglePinCurrent"));
        QVERIFY(model.isPinned(first));
        QCOMPARE(model.pathAt(0), first);

        // Search filters by body text.
        model.setFilter(QStringLiteral("body two"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.pathAt(0), second);
        model.setFilter(QString());

        // Deleting the current note moves to the next one.
        QVERIFY(model.removeNote(first));
        QVERIFY(!QFileInfo::exists(first));
        QCOMPARE(backend.filePath(), second);
        QVERIFY(!model.isPinned(first));

        // Deleting the last note creates a fresh one so typing keeps autosaving.
        QVERIFY(model.removeNote(second));
        QCOMPARE(backend.filePath(), dir.filePath(QStringLiteral("Untitled.md")));
        QCOMPARE(editor->property("text").toString(), QString());
        QCOMPARE(model.totalCount(), 1);
        QVERIFY(backend.autosaveActive());

        // Sidebar toggle persists.
        QVERIFY(window->property("sidebarOpen").toBool());
        QVERIFY(QMetaObject::invokeMethod(window.data(), "toggleSidebar"));
        QVERIFY(!window->property("sidebarOpen").toBool());
        QVERIFY(!backend.setting(QStringLiteral("sidebar/open"), true).toBool());
    }

    void countsWords() {
        QCOMPARE(Backend::countWords(QStringLiteral("one two-three don't 42")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好 世界")), 2);
        QCOMPARE(Backend::countWords(QString()), 0);
    }

    void normalizesLinks() {
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("www.example.com/path")),
                 QStringLiteral("https://www.example.com/path"));
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("mailto:writer@example.com")),
                 QStringLiteral("mailto:writer@example.com"));
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("example.com")).isEmpty());
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("file:///tmp/private")).isEmpty());
    }

    void suggestsSafeNames() {
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("My first draft\nBody")),
                 QStringLiteral("My first draft.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("A/B")), QStringLiteral("A-B.md"));
        QCOMPARE(Backend::suggestedFileName(QString()), QStringLiteral("Untitled.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("Already.md")),
                 QStringLiteral("Already.md"));
    }

    void findsInlineMarkdownRanges() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic* and [site](https://example.com)"));
        QCOMPARE(markup.size(), 3);
        QCOMPARE(markup.at(0).content.start, 2);
        QCOMPARE(markup.at(0).content.length, 4);
        QCOMPARE(markup.at(2).content.length, 4);
        QCOMPARE(markup.at(2).markers[0].length, 1);
    }

    void loadsCurrentOmarchyTheme() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());

        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        const QString themeDirectory = homeDirectory.path()
            + QStringLiteral("/.local/state/omarchy/current/theme");
        QVERIFY(QDir().mkpath(themeDirectory));

        QFile colorsFile(themeDirectory + QStringLiteral("/colors.toml"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray palette(
            "mode = \"light\"\n"
            "accent = \"#112233\"\n"
            "selection = \"#445566\"\n"
            "background = \"#fefefe\"\n"
            "foreground = \"#101010\"\n");
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#fefefe"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#101010"));
        QCOMPARE(backend.themeAccent(), QStringLiteral("#112233"));
        QCOMPARE(backend.themeSelection(), QStringLiteral("#445566"));
        QVERIFY(!backend.darkMode());
    }

    void ignoresFileWatcherEventsForSavedContents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("first-save.md"));
        Backend backend;
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        backend.saveAs(QUrl::fromLocalFile(path));
        QVERIFY(QFileInfo::exists(path));

        QFile sameContents(path);
        QVERIFY(sameContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sameContents.close();
        QTest::qWait(100);
        QCOMPARE(externalChangeSpy.count(), 0);

        QFile changedContents(path);
        QVERIFY(changedContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(changedContents.write("changed elsewhere"), qint64(17));
        changedContents.close();
        QTRY_COMPARE(externalChangeSpy.count(), 1);
    }

    void keepsCursorAndSelectionStableAcrossInsertions() {
        const QString mutationsPath = QFINDTESTDATA("../src/EditorMutations.js");
        QVERIFY(!mutationsPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray harness = R"QML(
            import QtQuick
            import "EditorMutations.js" as EditorMutations

            TextEdit {
                property string insertionText
                property int insertionCursor
                property string wrappedText
                property int wrappedSelectionStart
                property int wrappedSelectionEnd

                Component.onCompleted: {
                    text = "alpha omega";
                    cursorPosition = 5;
                    EditorMutations.replaceRange(this, 5, 5, "one\r\ntwo");
                    insertionText = text;
                    insertionCursor = cursorPosition;

                    text = "alpha beta omega";
                    select(6, 10);
                    EditorMutations.replaceRange(this, selectionStart, selectionEnd,
                                                 "**beta**", 2, 6);
                    wrappedText = text;
                    wrappedSelectionStart = selectionStart;
                    wrappedSelectionEnd = selectionEnd;
                }
            }
        )QML";
        const QUrl harnessUrl = QUrl::fromLocalFile(
            QFileInfo(mutationsPath).absolutePath() + QStringLiteral("/MutationHarness.qml"));
        component.setData(harness, harnessUrl);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("insertionText").toString(),
                 QStringLiteral("alphaone\ntwo omega"));
        QCOMPARE(editor->property("insertionCursor").toInt(), 12);
        QCOMPARE(editor->property("wrappedText").toString(),
                 QStringLiteral("alpha **beta** omega"));
        QCOMPARE(editor->property("wrappedSelectionStart").toInt(), 8);
        QCOMPARE(editor->property("wrappedSelectionEnd").toInt(), 12);
    }

    void savesAndOpensFromFooterButtons() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        NotesModel model;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("notesModel"), &model);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QVERIFY(window->findChild<QObject *>(QStringLiteral("sourceEditor")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("renderedPreview")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("modeToggle")));

        QObject *saveButton = window->findChild<QObject *>(QStringLiteral("saveButton"));
        QObject *openButton = window->findChild<QObject *>(QStringLiteral("openButton"));
        QVERIFY(saveButton);
        QVERIFY(openButton);

        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
        QCOMPARE(saveDialogSpy.count(), 1);

        QSignalSpy openDialogSpy(&backend, &Backend::openDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
        QCOMPARE(openDialogSpy.count(), 1);
    }

    void scalesTextWithDesktopTextSize() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        NotesModel model;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("notesModel"), &model);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);

        // `omarchy display text size 16` sets the GNOME factor to 16/12.
        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 27);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);

        backend.setTextScale(9.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 15);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 15);
    }

    void remembersLastSaveDirectory() {
        QTemporaryDir saveDirectory;
        QVERIFY(saveDirectory.isValid());

        const QString savedPath = saveDirectory.filePath(QStringLiteral("first.md"));
        Backend savedDocument;
        savedDocument.saveAs(QUrl::fromLocalFile(savedPath));

        Backend nextDocument;
        QSignalSpy saveDialogSpy(&nextDocument, &Backend::saveDialogRequested);
        nextDocument.saveAsDialog();
        QCOMPARE(saveDialogSpy.count(), 1);

        const QUrl suggestedUrl = saveDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).absolutePath(),
                 saveDirectory.path());
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).fileName(),
                 QStringLiteral("Untitled.md"));

        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             saveDirectory.filePath(QStringLiteral("missing")));
        Backend fallbackDocument;
        QSignalSpy fallbackDialogSpy(&fallbackDocument, &Backend::saveDialogRequested);
        fallbackDocument.saveAsDialog();
        const QUrl fallbackUrl = fallbackDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(fallbackUrl.toLocalFile()).absolutePath(), QDir::homePath());
    }

private:
    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(OmanoteTest)
#include "tst_omanote.moc"
