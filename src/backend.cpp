#include "backend.h"

#include <QClipboard>
#include <QDateTime>
#include <QImage>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QPrintDialog>
#include <QFileDialog>
#include <QPrinter>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextTable>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>
#include <QWindow>

#include <algorithm>

#include "markdownhighlighter.h"

constexpr qreal typoraLineHeightPercent = 140;
const QString lastSaveDirectorySetting = QStringLiteral("file/lastSaveDirectory");

QString Backend::normalizedLinkUrl(const QString &clipboardText) {
    QString candidate = clipboardText.trimmed();
    static const QRegularExpression lineBreakRe(QStringLiteral("[\\r\\n]"));
    const int lineBreak = candidate.indexOf(lineBreakRe);
    if (lineBreak >= 0)
        candidate = candidate.left(lineBreak).trimmed();

    if (candidate.isEmpty())
        return {};

    if (candidate.startsWith(QStringLiteral("www."), Qt::CaseInsensitive))
        candidate.prepend(QStringLiteral("https://"));

    static const QRegularExpression schemeRe(
        QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:"));
    if (!schemeRe.match(candidate).hasMatch())
        return {};

    const QUrl url(candidate);
    if (!url.isValid() || url.scheme().isEmpty())
        return {};

    const QString scheme = url.scheme().toLower();
    const bool webUrl = scheme == QStringLiteral("http")
        || scheme == QStringLiteral("https")
        || scheme == QStringLiteral("ftp");
    if (webUrl && url.host().isEmpty())
        return {};

    if (!webUrl && scheme != QStringLiteral("mailto"))
        return {};

    return url.toString();
}

Backend::Backend(QObject *parent) : QObject(parent) {
    const QString stateDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(stateDirectory);
    // Claim an orphaned snapshot before taking an empty slot. This ensures a
    // crash in window 2 is still recovered even if window 1 exited normally.
    for (int pass = 0; pass < 2 && !m_recoveryLock; ++pass) {
        for (int slot = 0; slot < 100; ++slot) {
            const QString base = QDir(stateDirectory).filePath(
                QStringLiteral("recovery-%1").arg(slot));
            const bool snapshotExists = QFileInfo::exists(base + QStringLiteral(".json"));
            if ((pass == 0) != snapshotExists)
                continue;
            auto lock = std::make_unique<QLockFile>(base + QStringLiteral(".lock"));
            if (lock->tryLock()) {
                m_recoveryPath = base + QStringLiteral(".json");
                m_recoveryLock = std::move(lock);
                break;
            }
        }
    }
    m_wordCountTimer.setSingleShot(true);
    m_wordCountTimer.setInterval(120);
    connect(&m_wordCountTimer, &QTimer::timeout, this, &Backend::refreshWordCount);
    m_recoveryTimer.setSingleShot(true);
    m_recoveryTimer.setInterval(750);
    m_autosaveTimer.setSingleShot(true);
    m_autosaveTimer.setInterval(1000);
    connect(&m_autosaveTimer, &QTimer::timeout, this, [this]() {
        if (m_modified && autosaveActive())
            saveTo(m_fileUrl);
    });
    connect(&m_recoveryTimer, &QTimer::timeout, this, &Backend::writeRecovery);
    connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &path) {
                if (path != m_fileUrl.toLocalFile())
                    return;

                const bool deleted = !QFileInfo::exists(path);
                if (!deleted && m_hasKnownFileContents) {
                    QFile file(path);
                    if (file.open(QIODevice::ReadOnly)
                            && file.readAll() == m_lastKnownFileContents) {
                        // Atomic saves can replace the watched inode. Re-arm the
                        // watcher, but do not report our own save as an outside edit.
                        watchCurrentFile();
                        return;
                    }
                }

                emit externalChangeDetected(deleted, m_modified);
            });

    loadOmarchyTheme();
    watchOmarchyTheme();
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
}

Backend::~Backend() = default;

void Backend::setParentWindow(QWindow *window) {
    m_parentWindow = window;
}

QString Backend::fileName() const {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty())
        return QStringLiteral("Untitled.md");

    if (m_fileUrl.isLocalFile()) {
        const QFileInfo info(m_fileUrl.toLocalFile());
        if (!info.fileName().isEmpty())
            return info.fileName();
    }

    const QString name = m_fileUrl.fileName();
    return name.isEmpty() ? QStringLiteral("Untitled.md") : name;
}

void Backend::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    loadOmarchyTheme();
    emit darkModeChanged();
}

void Backend::setTextScale(qreal textScale) {
    if (qFuzzyCompare(m_textScale, textScale))
        return;

    m_textScale = textScale;
    emit textScaleChanged();
}

void Backend::attachDocument(QObject *textDocument) {
    auto *quickDocument = qobject_cast<QQuickTextDocument *>(textDocument);
    if (!quickDocument || !quickDocument->textDocument()) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    if (m_highlighter)
        delete m_highlighter.data();

    m_document = quickDocument->textDocument();
    m_lastDocumentText = m_document->toPlainText();
    m_highlighter = new MarkdownHighlighter(m_document);
    m_highlighter->setDarkMode(m_darkMode);
    m_highlighter->setColors(m_themeBackground, m_themeForeground, m_themeAccent);

    connect(m_document, &QTextDocument::contentsChange, this,
            [this](int position, int, int charsAdded) {
                if (m_formattingTypography || m_loading)
                    return;
                m_lastChangePos = position;
                m_lastChangeAdded = charsAdded;
            });

    applyDocumentTypography();
    restoreRecovery();
}

void Backend::openDialog() {
    emit openDialogRequested();
}

void Backend::open(const QUrl &url) {
    if (!url.isLocalFile()) {
        setStatus(QStringLiteral("Only local files can be opened."));
        return;
    }

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(QStringLiteral("Could not open %1.").arg(targetName));
        return;
    }

    const QByteArray contents = file.readAll();
    loadDocumentText(QString::fromUtf8(contents));
    clearRecovery();
    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    setFileUrl(url);
    watchCurrentFile();
    setModified(false);
    setStatus(QStringLiteral("Opened %1").arg(fileName()));
}

void Backend::save() {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty()) {
        saveAsDialog();
        return;
    }

    saveTo(m_fileUrl);
}

void Backend::saveForClose() {
    if (!m_modified) {
        emit closeAfterSave();
        return;
    }

    m_closeAfterSave = true;
    save();
}

bool Backend::saveNow() {
    if (!m_fileUrl.isLocalFile())
        return false;
    if (m_modified)
        saveTo(m_fileUrl);
    return !m_modified;
}

void Backend::openPath(const QString &path) {
    open(QUrl::fromLocalFile(path));
}

bool Backend::isNotePath(const QString &path) const {
    if (m_notesDir.isEmpty() || path.isEmpty())
        return false;
    const QString dir = QFileInfo(path).absolutePath();
    return dir == m_notesDir;
}

bool Backend::autosaveActive() const {
    return m_fileUrl.isLocalFile() && isNotePath(m_fileUrl.toLocalFile());
}

QString Backend::filePath() const {
    return m_fileUrl.isLocalFile() ? m_fileUrl.toLocalFile() : QString();
}

QString Backend::defaultNotesDir() {
    const QString documents =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(documents.isEmpty() ? QDir::homePath() : documents)
        .filePath(QStringLiteral("notes"));
}

void Backend::setNotesDir(const QString &dir) {
    const QString absolute = QDir(dir).absolutePath();
    if (absolute == m_notesDir)
        return;
    m_notesDir = absolute;
    emit notesDirChanged();
    emit fileUrlChanged();
}

QVariant Backend::setting(const QString &key, const QVariant &fallback) const {
    return QSettings().value(key, fallback);
}

void Backend::setSetting(const QString &key, const QVariant &value) {
    QSettings().setValue(key, value);
}

void Backend::newDocument() {
    m_autosaveTimer.stop();
    clearRecovery();
    loadDocumentText(QString());
    m_lastKnownFileContents.clear();
    m_hasKnownFileContents = false;
    setFileUrl(QUrl());
    watchCurrentFile();
    setModified(false);
    setStatus(QString());
}

void Backend::saveAsDialog() {
    emit saveDialogRequested(suggestedSaveUrl());
}

void Backend::saveAs(const QUrl &url) {
    saveTo(url);
}

void Backend::fileDialogCanceled() {
    m_closeAfterSave = false;
}

void Backend::discardRecovery() {
    clearRecovery();
}

void Backend::reloadFromDisk() {
    if (m_fileUrl.isLocalFile())
        open(m_fileUrl);
}

void Backend::keepExternalVersion() {
    QFile file(m_fileUrl.toLocalFile());
    if (file.open(QIODevice::ReadOnly)) {
        m_lastKnownFileContents = file.readAll();
        m_hasKnownFileContents = true;
    } else {
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
    }
    setModified(true);
    scheduleRecovery();
    watchCurrentFile();
    setStatus(QStringLiteral("Kept your version"));
}

void Backend::printDocument() {
    if (!m_document) {
        setStatus(QStringLiteral("There is no document to print."));
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer);
    dialog.setWindowTitle(QStringLiteral("Print %1").arg(fileName()));
    dialog.winId();
    if (dialog.windowHandle() && m_parentWindow)
        dialog.windowHandle()->setTransientParent(m_parentWindow);

    if (dialog.exec() == QDialog::Accepted) {
        QTextDocument rendered;
        rendered.setDefaultFont(m_document->defaultFont());
        rendered.setMarkdown(currentDocumentText());
        rendered.print(&printer);
    }
}

void Backend::exportDocument() {
    if (!m_document) {
        setStatus(QStringLiteral("There is no document to export."));
        return;
    }
    const QString stem = QFileInfo(fileName()).completeBaseName();
    const QString startDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString selectedFilter;
    const QString target = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("Export %1").arg(fileName()),
        QDir(startDir).filePath(stem + QStringLiteral(".pdf")),
        QStringLiteral("PDF (*.pdf);;HTML (*.html)"), &selectedFilter);
    if (target.isEmpty())
        return;
    QTextDocument rendered;
    rendered.setDefaultFont(m_document->defaultFont());
    rendered.setMarkdown(currentDocumentText(), QTextDocument::MarkdownDialectGitHub);
    const bool html = target.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive)
        || target.endsWith(QStringLiteral(".htm"), Qt::CaseInsensitive)
        || (selectedFilter.startsWith(QStringLiteral("HTML")) && !target.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive));
    if (html) {
        QSaveFile file(target);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(rendered.toHtml().toUtf8()) < 0 || !file.commit()) {
            setStatus(QStringLiteral("Could not write %1.").arg(QFileInfo(target).fileName()));
            return;
        }
    } else {
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(target);
        rendered.print(&printer);
    }
    setStatus(QStringLiteral("Exported %1.").arg(QFileInfo(target).fileName()));
}

void Backend::raiseWindow() {
    if (!m_parentWindow)
        return;
    m_parentWindow->show();
    m_parentWindow->raise();
    m_parentWindow->requestActivate();
}

void Backend::newWindow() {
    const bool started = QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                                 {QStringLiteral("--new-window")});
    if (!started)
        setStatus(QStringLiteral("Could not open a new window."));
}

QString Backend::clipboardUrl() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    if (!mimeData)
        return {};

    if (mimeData->hasUrls()) {
        const QList<QUrl> urls = mimeData->urls();
        for (const QUrl &url : urls) {
            const QString normalized = normalizedLinkUrl(url.toString());
            if (!normalized.isEmpty())
                return normalized;
        }
    }

    if (!mimeData->hasText())
        return {};

    return normalizedLinkUrl(mimeData->text());
}

QString Backend::clipboardText() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    return mimeData && mimeData->hasText() ? mimeData->text() : QString();
}

bool Backend::clipboardHasImage() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    const QMimeData *mimeData = clipboard ? clipboard->mimeData() : nullptr;
    return mimeData && mimeData->hasImage() && !mimeData->hasText();
}

QString Backend::saveClipboardImage() {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};
    const QImage image = clipboard->image();
    if (image.isNull())
        return {};
    const QString notePath = filePath();
    const QString baseDir = notePath.isEmpty() ? m_notesDir : QFileInfo(notePath).absolutePath();
    if (baseDir.isEmpty())
        return {};
    const QDir assets(QDir(baseDir).filePath(QStringLiteral("assets")));
    if (!QDir().mkpath(assets.absolutePath()))
        return {};
    QString stem = notePath.isEmpty() ? QStringLiteral("image")
                                      : QFileInfo(notePath).completeBaseName();
    stem.replace(QRegularExpression(QStringLiteral("[^\\w-]+")), QStringLiteral("-"));
    const QString name = QStringLiteral("%1-%2.png")
        .arg(stem, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    if (!image.save(assets.filePath(name), "PNG"))
        return {};
    return QStringLiteral("![](assets/%1)").arg(name);
}

QString Backend::markdownToHtml(const QString &markdown) const {
    QTextDocument document;
    document.setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
    return document.toHtml();
}

QString Backend::previewHtml(const QString &markdown) const {
    // QTextDocument::setMarkdown drops images, so lift them out first, convert
    // the rest, and put <img> tags back where the placeholders landed.
    const QString notePath = filePath();
    const QString baseDir = notePath.isEmpty() ? m_notesDir : QFileInfo(notePath).absolutePath();
    static const QRegularExpression imageRe(QStringLiteral("!\\[([^\\]]*)\\]\\(([^)\\s]+)[^)]*\\)"));
    QStringList images;
    QString text;
    int last = 0;
    QRegularExpressionMatchIterator it = imageRe.globalMatch(markdown);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        text += markdown.mid(last, match.capturedStart(0) - last);
        text += QStringLiteral("OMANOTEIMG%1X").arg(images.size());
        last = match.capturedEnd(0);
        QString src = match.captured(2);
        const QUrl url(src);
        if (url.scheme().isEmpty() && !src.startsWith(QLatin1Char('/')) && !baseDir.isEmpty())
            src = QUrl::fromLocalFile(QDir(baseDir).filePath(src)).toString();
        else if (src.startsWith(QLatin1Char('/')))
            src = QUrl::fromLocalFile(src).toString();
        images.append(QStringLiteral("<img src=\"%1\" alt=\"%2\" />")
                          .arg(src.toHtmlEscaped(), match.captured(1).toHtmlEscaped()));
    }
    text += markdown.mid(last);
    QString html = markdownToHtml(text);
    for (int i = 0; i < images.size(); ++i)
        html.replace(QStringLiteral("OMANOTEIMG%1X").arg(i), images.at(i));
    return html;
}

QList<QStringList> Backend::tableRowsFromHtml(const QString &html) {
    QList<QStringList> rows;
    if (!html.contains(QStringLiteral("<table"), Qt::CaseInsensitive))
        return rows;
    QTextDocument document;
    document.setHtml(html);
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        QTextCursor cursor(block);
        QTextTable *table = cursor.currentTable();
        if (!table)
            continue;
        for (int r = 0; r < table->rows(); ++r) {
            QStringList row;
            for (int c = 0; c < table->columns(); ++c) {
                const QTextTableCell cell = table->cellAt(r, c);
                QString text;
                for (QTextFrame::iterator it = cell.begin(); !it.atEnd(); ++it) {
                    if (it.currentBlock().isValid())
                        text += it.currentBlock().text() + QLatin1Char(' ');
                }
                row.append(text.simplified());
            }
            rows.append(row);
        }
        break; // first table only
    }
    return rows;
}

QList<QStringList> Backend::tableRowsFromTsv(const QString &text) {
    QList<QStringList> rows;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r?\n")));
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty())
            continue;
        if (!line.contains(QLatin1Char('\t')))
            return {};
        QStringList cells = line.split(QLatin1Char('\t'));
        for (QString &cell : cells)
            cell = cell.trimmed();
        rows.append(cells);
    }
    return rows.size() >= 2 ? rows : QList<QStringList>();
}

QList<QStringList> Backend::tableRowsFromMarkdown(const QString &markdown) {
    QList<QStringList> rows;
    static const QRegularExpression separator(QStringLiteral("^\\s*\\|?\\s*:?-{1,}:?\\s*(\\|\\s*:?-{1,}:?\\s*)*\\|?\\s*$"));
    for (const QString &line : markdown.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.contains(QLatin1Char('|')))
            continue;
        if (separator.match(trimmed).hasMatch())
            continue;
        QString inner = trimmed;
        if (inner.startsWith(QLatin1Char('|')))
            inner.remove(0, 1);
        if (inner.endsWith(QLatin1Char('|')))
            inner.chop(1);
        // Split on unescaped pipes only; "\|" is a literal pipe inside a cell.
        QStringList cells;
        QString current;
        for (int i = 0; i < inner.size(); ++i) {
            const QChar ch = inner.at(i);
            if (ch == QLatin1Char('\\') && i + 1 < inner.size() && inner.at(i + 1) == QLatin1Char('|')) {
                current += QLatin1Char('|');
                ++i;
            } else if (ch == QLatin1Char('|')) {
                cells.append(current.trimmed());
                current.clear();
            } else {
                current += ch;
            }
        }
        cells.append(current.trimmed());
        rows.append(cells);
    }
    return rows;
}

QString Backend::markdownTable(const QList<QStringList> &rows) {
    if (rows.isEmpty())
        return {};
    int columns = 0;
    for (const QStringList &row : rows)
        columns = std::max(columns, int(row.size()));
    if (columns == 0)
        return {};
    QVector<int> widths(columns, 3);
    QList<QStringList> cells;
    for (const QStringList &row : rows) {
        QStringList padded = row;
        while (padded.size() < columns)
            padded.append(QString());
        for (int c = 0; c < columns; ++c) {
            padded[c] = padded[c].simplified().replace(QLatin1Char('|'), QStringLiteral("\\|"));
            widths[c] = std::max(widths[c], int(padded[c].size()));
        }
        cells.append(padded);
    }
    QString out;
    auto emitRow = [&](const QStringList &row) {
        out += QLatin1Char('|');
        for (int c = 0; c < columns; ++c)
            out += QLatin1Char(' ') + row.at(c).leftJustified(widths[c]) + QStringLiteral(" |");
        out += QLatin1Char('\n');
    };
    emitRow(cells.first());
    out += QLatin1Char('|');
    for (int c = 0; c < columns; ++c)
        out += QLatin1Char(' ') + QString(widths[c], QLatin1Char('-')) + QStringLiteral(" |");
    out += QLatin1Char('\n');
    for (int r = 1; r < cells.size(); ++r)
        emitRow(cells.at(r));
    return out;
}

QString Backend::htmlTable(const QList<QStringList> &rows) {
    if (rows.isEmpty())
        return {};
    QString out = QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\">\n");
    for (int r = 0; r < rows.size(); ++r) {
        out += QStringLiteral("<tr>");
        const QString tag = r == 0 ? QStringLiteral("th") : QStringLiteral("td");
        for (const QString &cell : rows.at(r))
            out += QStringLiteral("<%1>%2</%1>").arg(tag, cell.toHtmlEscaped());
        out += QStringLiteral("</tr>\n");
    }
    out += QStringLiteral("</table>\n");
    return out;
}

QString Backend::clipboardTableMarkdown() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    const QMimeData *mimeData = clipboard ? clipboard->mimeData() : nullptr;
    if (!mimeData)
        return {};
    QList<QStringList> rows;
    if (mimeData->hasHtml())
        rows = tableRowsFromHtml(mimeData->html());
    if (rows.isEmpty() && mimeData->hasText())
        rows = tableRowsFromTsv(mimeData->text());
    return markdownTable(rows);
}

QString Backend::formatMarkdownTable(const QString &markdown) const {
    QString out = markdownTable(tableRowsFromMarkdown(markdown));
    if (out.endsWith(QLatin1Char('\n')))
        out.chop(1);
    return out;
}

void Backend::copyTable(const QString &markdown) const {
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return;
    const QList<QStringList> rows = tableRowsFromMarkdown(markdown);
    auto *mimeData = new QMimeData;
    mimeData->setText(markdown);
    if (!rows.isEmpty())
        mimeData->setHtml(htmlTable(rows));
    clipboard->setMimeData(mimeData);
}

bool Backend::editorTextChanged() {
    if (m_loading || m_formattingTypography)
        return false;

    const QString text = currentDocumentText();
    if (text == m_lastDocumentText)
        return false;
    m_lastDocumentText = text;

    if (m_document) {
        const int blockCount = m_document->blockCount();
        if (blockCount > m_formattedBlockCount)
            reapplyTypographyToChange();
        m_formattedBlockCount = blockCount;
    }

    scheduleWordCount();
    setModified(true);
    setStatus(QStringLiteral("Unsaved"));
    scheduleRecovery();
    if (autosaveActive())
        m_autosaveTimer.start();
    return true;
}

QVariantList Backend::hiddenRangesAt(int position) const {
    QVariantList ranges;
    if (!m_document)
        return ranges;

    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid())
        return ranges;

    const int lineStart = block.position();
    QList<QPair<int, int>> spans;
    const QList<MarkdownHighlighter::InlineMarkup> markup =
        MarkdownHighlighter::inlineMarkup(block.text());
    for (const MarkdownHighlighter::InlineMarkup &item : markup) {
        for (const MarkdownHighlighter::Span &marker : item.markers) {
            spans.append({lineStart + marker.start,
                          lineStart + marker.start + marker.length});
        }
    }
    std::sort(spans.begin(), spans.end());

    for (const auto &span : spans) {
        ranges.append(QVariantMap{{QStringLiteral("start"), span.first},
                                  {QStringLiteral("end"), span.second}});
    }
    return ranges;
}

void Backend::setSearchHighlight(const QString &query, int currentMatchStart) {
    if (m_highlighter)
        m_highlighter->setSearch(query, currentMatchStart);
}

void Backend::openExternalUrl(const QUrl &url) {
    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")
            || scheme == QStringLiteral("mailto"))
        QDesktopServices::openUrl(url);
}

QVariantMap Backend::windowGeometry() const {
    QSettings settings;
    return {{QStringLiteral("x"), settings.value(QStringLiteral("window/x"), -1)},
            {QStringLiteral("y"), settings.value(QStringLiteral("window/y"), -1)},
            {QStringLiteral("width"), settings.value(QStringLiteral("window/width"), 1280)},
            {QStringLiteral("height"), settings.value(QStringLiteral("window/height"), 820)},
            {QStringLiteral("maximized"), settings.value(QStringLiteral("window/maximized"), false)}};
}

void Backend::saveWindowGeometry(int x, int y, int width, int height, bool maximized) {
    QSettings settings;
    if (!maximized) {
        settings.setValue(QStringLiteral("window/x"), x);
        settings.setValue(QStringLiteral("window/y"), y);
        settings.setValue(QStringLiteral("window/width"), width);
        settings.setValue(QStringLiteral("window/height"), height);
    }
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}

void Backend::loadDocumentText(const QString &text) {
    if (!m_document) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    m_loading = true;
    m_document->setPlainText(text);
    m_lastDocumentText = text;
    m_loading = false;

    applyDocumentTypography();
    m_wordCountTimer.stop();
    setWordCount(countWords(text));
}

void Backend::setFileUrl(const QUrl &url) {
    if (m_fileUrl == url)
        return;

    m_fileUrl = url;
    emit fileUrlChanged();
    watchCurrentFile();
}

void Backend::setModified(bool modified) {
    if (m_modified == modified)
        return;

    m_modified = modified;
    emit modifiedChanged();
}

void Backend::setStatus(const QString &status) {
    if (m_status == status)
        return;

    m_status = status;
    emit statusChanged();
}

void Backend::saveTo(const QUrl &url) {
    if (!url.isLocalFile()) {
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Only local files can be saved."));
        return;
    }

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();
    QSaveFile file(url.toLocalFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Could not save %1.").arg(targetName));
        return;
    }

    const QByteArray contents = currentDocumentText().toUtf8();
    file.write(contents);

    // QSaveFile commits by replacing the target. Stop watching the old inode
    // before that replacement so our own write is not classified as external.
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);

    // commit() flushes, fsyncs, and atomically renames the temp file into place,
    // returning false (and leaving the original untouched) on any write error.
    if (!file.commit()) {
        watchCurrentFile();
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Could not write %1.").arg(targetName));
        return;
    }

    const bool shouldClose = m_closeAfterSave;
    m_closeAfterSave = false;
    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    setFileUrl(url);
    watchCurrentFile();
    QSettings().setValue(lastSaveDirectorySetting,
                         QFileInfo(url.toLocalFile()).absolutePath());
    setModified(false);
    setStatus(QStringLiteral("Saved %1").arg(fileName()));
    clearRecovery();
    m_autosaveTimer.stop();
    emit saveSucceeded();
    emit fileSaved(url.toLocalFile());

    if (shouldClose)
        emit closeAfterSave();
}

void Backend::scheduleRecovery() {
    m_recoveryTimer.start();
}

QString Backend::recoveryPath() const {
    return m_recoveryPath;
}

void Backend::writeRecovery() {
    if (!m_modified)
        return;
    const QString path = recoveryPath();
    if (path.isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QJsonObject recovery{{QStringLiteral("fileUrl"), m_fileUrl.toString()},
                               {QStringLiteral("text"), currentDocumentText()}};
    file.write(QJsonDocument(recovery).toJson(QJsonDocument::Compact));
    file.commit();
}

void Backend::restoreRecovery() {
    QFile file(recoveryPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject() || !json.object().contains(QStringLiteral("text")))
        return;
    const QJsonObject recovery = json.object();
    loadDocumentText(recovery.value(QStringLiteral("text")).toString());
    const QUrl recoveredUrl(recovery.value(QStringLiteral("fileUrl")).toString());
    QFile diskFile(recoveredUrl.toLocalFile());
    if (recoveredUrl.isLocalFile() && diskFile.open(QIODevice::ReadOnly)) {
        m_lastKnownFileContents = diskFile.readAll();
        m_hasKnownFileContents = true;
    } else {
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
    }
    setFileUrl(recoveredUrl);
    setModified(true);
    setStatus(QStringLiteral("Recovered unsaved changes"));
}

void Backend::clearRecovery() {
    m_recoveryTimer.stop();
    QFile::remove(recoveryPath());
}

void Backend::watchCurrentFile() {
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);
    if (m_fileUrl.isLocalFile() && QFileInfo::exists(m_fileUrl.toLocalFile()))
        m_fileWatcher.addPath(m_fileUrl.toLocalFile());
}

void Backend::loadOmarchyTheme() {
    m_themeBackground = m_darkMode ? QStringLiteral("#101010") : QStringLiteral("#ffffff");
    m_themeForeground = m_darkMode ? QStringLiteral("#eeeeee") : QStringLiteral("#222324");
    m_themeAccent = m_darkMode ? QStringLiteral("#5584aa") : QStringLiteral("#2077b2");
    m_themeSelection = m_darkMode ? QStringLiteral("#186a9a") : QStringLiteral("#2077b2");

    const QString colorsPath = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
    QString themeMode;
    QFile file(colorsPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;

            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0)
                continue;

            const QString key = line.left(equals).trimmed();
            QString value = line.mid(equals + 1).trimmed();
            if (value.size() >= 2
                    && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
                        || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))))
                value = value.mid(1, value.size() - 2);

            if (key == QStringLiteral("mode"))
                themeMode = value;
            else if (key == QStringLiteral("background"))
                m_themeBackground = value;
            else if (key == QStringLiteral("foreground"))
                m_themeForeground = value;
            else if (key == QStringLiteral("accent"))
                m_themeAccent = value;
            else if (key == QStringLiteral("selection"))
                m_themeSelection = value;
        }
    }

    bool themeModeKnown = false;
    bool themeIsDark = m_darkMode;
    if (themeMode == QStringLiteral("dark")) {
        themeIsDark = true;
        themeModeKnown = true;
    } else if (themeMode == QStringLiteral("light")) {
        themeIsDark = false;
        themeModeKnown = true;
    } else {
        const QColor background(m_themeBackground);
        if (background.isValid()) {
            const double luminance = 0.299 * background.redF()
                + 0.587 * background.greenF() + 0.114 * background.blueF();
            themeIsDark = luminance < 0.5;
            themeModeKnown = true;
        }
    }
    if (themeModeKnown && themeIsDark != m_darkMode) {
        m_darkMode = themeIsDark;
        emit darkModeChanged();
    }

    if (m_highlighter) {
        m_highlighter->setDarkMode(m_darkMode);
        m_highlighter->setColors(m_themeBackground, m_themeForeground, m_themeAccent);
    }

    emit themeColorsChanged();
}

void Backend::watchOmarchyTheme() {
    const QStringList watched = m_themeWatcher.files() + m_themeWatcher.directories();
    if (!watched.isEmpty())
        m_themeWatcher.removePaths(watched);

    const QString currentDir = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current");
    const QString themeDir = currentDir + QStringLiteral("/theme");
    const QString colorsPath = themeDir + QStringLiteral("/colors.toml");

    if (QDir(currentDir).exists())
        m_themeWatcher.addPath(currentDir);
    if (QDir(themeDir).exists())
        m_themeWatcher.addPath(themeDir);
    if (QFile::exists(colorsPath))
        m_themeWatcher.addPath(colorsPath);
}

QUrl Backend::suggestedSaveUrl() const {
    if (m_fileUrl.isLocalFile())
        return m_fileUrl;

    const QString savedDirectory = QSettings().value(lastSaveDirectorySetting).toString();
    const QDir directory = savedDirectory.isEmpty() || !QDir(savedDirectory).exists()
        ? QDir::home()
        : QDir(savedDirectory);
    return QUrl::fromLocalFile(
        directory.filePath(suggestedFileName(currentDocumentText())));
}

QString Backend::currentDocumentText() const {
    return m_document ? m_document->toPlainText() : QString();
}

int Backend::countWords(const QString &text) {
    static const QRegularExpression wordRe(
        QStringLiteral("[\\p{L}\\p{N}]+(?:['-][\\p{L}\\p{N}]+)*"));
    int count = 0;
    QRegularExpressionMatchIterator it = wordRe.globalMatch(text);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

QString Backend::suggestedFileName(const QString &text) {
    QString name = text.section(QLatin1Char('\n'), 0, 0).trimmed();
    name.replace(QRegularExpression(QStringLiteral("[/\\x00-\\x1f\\x7f]")),
                 QStringLiteral("-"));
    name = name.left(120).trimmed();
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
        name = QStringLiteral("Untitled");
    if (!name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        name += QStringLiteral(".md");
    return name;
}

void Backend::setWordCount(int words) {
    if (m_wordCount == words)
        return;

    m_wordCount = words;
    emit wordCountChanged();
}

void Backend::refreshWordCount() {
    setWordCount(countWords(currentDocumentText()));
}

void Backend::scheduleWordCount() {
    m_wordCountTimer.start();
}

void Backend::applyDocumentTypography() {
    if (!m_document)
        return;

    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);

    // A full pass is only used for freshly loaded/attached documents, so it is
    // safe to drop undo history here (re-enabling clears the stack anyway).
    const bool undoEnabled = m_document->isUndoRedoEnabled();
    m_document->setUndoRedoEnabled(false);

    m_formattingTypography = true;
    QTextCursor cursor(m_document);
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockFormat(blockFormat);
    m_formattingTypography = false;

    m_document->setUndoRedoEnabled(undoEnabled);

    m_formattedBlockCount = m_document->blockCount();
}

void Backend::reapplyTypographyToChange() {
    if (!m_document)
        return;

    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);

    // Format only the block(s) touched by the last edit instead of the whole
    // document, and fold the change into the preceding edit command so a single
    // undo reverts both the text and its formatting.
    const int maxPos = m_document->characterCount() - 1;
    const int start = qBound(0, m_lastChangePos, maxPos);
    const int end = qBound(start, m_lastChangePos + m_lastChangeAdded, maxPos);

    m_formattingTypography = true;
    QTextCursor cursor(m_document);
    cursor.joinPreviousEditBlock();
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.mergeBlockFormat(blockFormat);
    cursor.endEditBlock();
    m_formattingTypography = false;
}
