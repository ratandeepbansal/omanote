#include "notesmodel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <algorithm>

namespace {
const int kPreviewLimit = 120;
const int kReadLimit = 4096;
}

NotesModel::NotesModel(QObject *parent) : QAbstractListModel(parent) {
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(150);
    connect(&m_refreshTimer, &QTimer::timeout, this, &NotesModel::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this]() { m_refreshTimer.start(); });
}

void NotesModel::setNotesDir(const QString &dir) {
    const QString absolute = QDir(dir).absolutePath();
    if (absolute == m_notesDir)
        return;
    m_notesDir = absolute;
    QDir().mkpath(m_notesDir);
    loadPins();
    watchDirectory();
    emit notesDirChanged();
    refresh();
}

void NotesModel::setFilter(const QString &filter) {
    if (m_filter == filter)
        return;
    m_filter = filter;
    emit filterChanged();
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit countChanged();
}

int NotesModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_visible.size();
}

QVariant NotesModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size())
        return {};
    const Note &note = m_notes.at(m_visible.at(index.row()));
    switch (role) {
    case PathRole: return note.path;
    case FileNameRole: return note.fileName;
    case TitleRole: return note.title;
    case PreviewRole: return note.preview;
    case DateRole: return dateLabel(note.modified, QDateTime::currentDateTime());
    case PinnedRole: return note.pinned;
    }
    return {};
}

QHash<int, QByteArray> NotesModel::roleNames() const {
    return {{PathRole, "path"},       {FileNameRole, "fileName"}, {TitleRole, "title"},
            {PreviewRole, "preview"}, {DateRole, "date"},         {PinnedRole, "pinned"}};
}

void NotesModel::refresh() {
    if (m_notesDir.isEmpty())
        return;
    beginResetModel();
    scanDirectory();
    rebuildVisible();
    endResetModel();
    emit countChanged();
    // Directories vanish from the watcher when recreated; re-arm each time.
    watchDirectory();
}

void NotesModel::scanDirectory() {
    QDir dir(m_notesDir);
    const QFileInfoList entries = dir.entryInfoList(
        {QStringLiteral("*.md"), QStringLiteral("*.markdown")},
        QDir::Files | QDir::Readable | QDir::NoDotAndDotDot);

    QVector<Note> notes;
    notes.reserve(entries.size());
    QSet<QString> seen;
    for (const QFileInfo &info : entries) {
        Note note;
        note.path = info.absoluteFilePath();
        note.fileName = info.fileName();
        note.modified = info.lastModified();
        note.pinned = m_pins.contains(note.path);
        seen.insert(note.path);

        CacheEntry &entry = m_cache[note.path];
        if (entry.modified != note.modified || entry.size != info.size()) {
            readNote(note);
            entry = {note.modified, info.size(), note.title, note.preview};
        } else {
            note.title = entry.title;
            note.preview = entry.preview;
        }
        notes.append(note);
    }
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (!seen.contains(it.key()))
            it = m_cache.erase(it);
        else
            ++it;
    }
    std::sort(notes.begin(), notes.end(), &NotesModel::lessThan);
    m_notes = notes;
}

bool NotesModel::lessThan(const Note &a, const Note &b) {
    if (a.pinned != b.pinned)
        return a.pinned;
    if (a.modified != b.modified)
        return a.modified > b.modified;
    return a.fileName.localeAwareCompare(b.fileName) < 0;
}

void NotesModel::rebuildVisible() {
    m_visible.clear();
    const QString needle = m_filter.trimmed();
    for (int i = 0; i < m_notes.size(); ++i) {
        if (needle.isEmpty()) {
            m_visible.append(i);
            continue;
        }
        const Note &note = m_notes.at(i);
        if (note.title.contains(needle, Qt::CaseInsensitive)
                || note.preview.contains(needle, Qt::CaseInsensitive)
                || note.fileName.contains(needle, Qt::CaseInsensitive)) {
            m_visible.append(i);
            continue;
        }
        // Fall back to the body so a search for a word deep in a note still hits.
        QFile file(note.path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)
                && QString::fromUtf8(file.readAll()).contains(needle, Qt::CaseInsensitive))
            m_visible.append(i);
    }
}

void NotesModel::readNote(Note &note) const {
    QFile file(note.path);
    QString text;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        text = QString::fromUtf8(file.read(kReadLimit));
    note.title = titleFor(text, note.fileName);
    note.preview = previewFor(text);
}

QString NotesModel::stripMarkdown(const QString &line) {
    QString out = line.trimmed();
    static const QRegularExpression heading(QStringLiteral("^#{1,6}\\s*"));
    static const QRegularExpression bullet(QStringLiteral("^([-*+]|\\d+[.)])\\s+(\\[[ xX]\\]\\s*)?"));
    static const QRegularExpression quote(QStringLiteral("^>+\\s*"));
    static const QRegularExpression emphasis(QStringLiteral("(\\*{1,3}|_{1,3}|~~|`)"));
    static const QRegularExpression link(QStringLiteral("!?\\[([^\\]]*)\\]\\([^)]*\\)"));
    out.remove(heading);
    out.remove(bullet);
    out.remove(quote);
    out.replace(link, QStringLiteral("\\1"));
    out.remove(emphasis);
    return out.trimmed();
}

QString NotesModel::titleFor(const QString &text, const QString &fileName) {
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString stripped = stripMarkdown(line);
        if (!stripped.isEmpty())
            return stripped.left(kPreviewLimit);
    }
    QString base = QFileInfo(fileName).completeBaseName();
    return base.isEmpty() ? QStringLiteral("Untitled") : base;
}

QString NotesModel::previewFor(const QString &text) {
    const QStringList lines = text.split(QLatin1Char('\n'));
    bool skippedTitle = false;
    for (const QString &line : lines) {
        const QString stripped = stripMarkdown(line);
        if (stripped.isEmpty())
            continue;
        if (!skippedTitle) {
            skippedTitle = true;
            continue;
        }
        return stripped.left(kPreviewLimit);
    }
    return {};
}

QString NotesModel::dateLabel(const QDateTime &modified, const QDateTime &now) {
    if (!modified.isValid())
        return {};
    const QLocale locale = QLocale::c();
    if (modified.date().year() == now.date().year())
        return locale.toString(modified.date(), QStringLiteral("MMM d"));
    return locale.toString(modified.date(), QStringLiteral("MMM d, yyyy"));
}

QString NotesModel::untitledPath() const {
    const QDir dir(m_notesDir);
    QString candidate = dir.filePath(QStringLiteral("Untitled.md"));
    for (int n = 2; QFileInfo::exists(candidate); ++n)
        candidate = dir.filePath(QStringLiteral("Untitled %1.md").arg(n));
    return candidate;
}

QString NotesModel::createNote() {
    if (m_notesDir.isEmpty())
        return {};
    QDir().mkpath(m_notesDir);
    const QString path = untitledPath();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    if (!file.commit())
        return {};
    refresh();
    return path;
}

bool NotesModel::removeNote(const QString &path) {
    if (!contains(path))
        return false;
    QFile file(path);
    bool removed = file.moveToTrash();
    if (!removed) {
        // Some setups have no trash (tmpfs, odd mounts); fall back to deleting.
        removed = file.remove();
    }
    if (!removed)
        return false;
    m_pins.remove(path);
    savePins();
    refresh();
    emit noteRemoved(path);
    return true;
}

bool NotesModel::renameNote(const QString &path, const QString &newFileName) {
    if (!contains(path))
        return false;
    QString name = newFileName.trimmed();
    if (name.isEmpty() || name.contains(QLatin1Char('/')))
        return false;
    if (!name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)
            && !name.endsWith(QStringLiteral(".markdown"), Qt::CaseInsensitive))
        name += QStringLiteral(".md");
    const QString target = QDir(m_notesDir).filePath(name);
    if (target == path)
        return true;
    if (QFileInfo::exists(target))
        return false;
    if (!QFile::rename(path, target))
        return false;
    if (m_pins.remove(path)) {
        m_pins.insert(target);
        savePins();
    }
    refresh();
    return true;
}

void NotesModel::setPinned(const QString &path, bool pinned) {
    if (pinned == m_pins.contains(path))
        return;
    if (pinned)
        m_pins.insert(path);
    else
        m_pins.remove(path);
    savePins();
    refresh();
}

bool NotesModel::isPinned(const QString &path) const {
    return m_pins.contains(path);
}

int NotesModel::indexOf(const QString &path) const {
    for (int row = 0; row < m_visible.size(); ++row) {
        if (m_notes.at(m_visible.at(row)).path == path)
            return row;
    }
    return -1;
}

QString NotesModel::pathAt(int row) const {
    if (row < 0 || row >= m_visible.size())
        return {};
    return m_notes.at(m_visible.at(row)).path;
}

bool NotesModel::contains(const QString &path) const {
    return std::any_of(m_notes.cbegin(), m_notes.cend(),
                       [&path](const Note &note) { return note.path == path; });
}

QString NotesModel::pinsPath() const {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("pins.json"));
}

void NotesModel::loadPins() {
    m_pins.clear();
    QFile file(pinsPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    const QJsonArray pins = json.object().value(QStringLiteral("pinned")).toArray();
    for (const QJsonValue &value : pins)
        m_pins.insert(value.toString());
}

void NotesModel::savePins() const {
    const QString path = pinsPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    QJsonArray pins;
    QStringList sorted(m_pins.cbegin(), m_pins.cend());
    sorted.sort();
    for (const QString &pin : sorted)
        pins.append(pin);
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("pinned"), pins}}).toJson());
    file.commit();
}

void NotesModel::watchDirectory() {
    const QStringList dirs = m_watcher.directories();
    if (!dirs.isEmpty())
        m_watcher.removePaths(dirs);
    if (!m_notesDir.isEmpty() && QDir(m_notesDir).exists())
        m_watcher.addPath(m_notesDir);
}
