#include "notesmodel.h"
#include "tagpattern.h"

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
const int kReadLimit = 1 << 20; // tags can sit anywhere in the note
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

void NotesModel::setFolder(const QString &folder) {
    if (m_folder == folder)
        return;
    m_folder = folder;
    emit folderChanged();
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit countChanged();
}

void NotesModel::setTag(const QString &tag) {
    const QString normalized = tag.trimmed().toLower();
    if (m_tag == normalized)
        return;
    m_tag = normalized;
    emit tagChanged();
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
    case FolderRole: return note.folder;
    case TagsRole: return note.tags;
    }
    return {};
}

QHash<int, QByteArray> NotesModel::roleNames() const {
    return {{PathRole, "path"},       {FileNameRole, "fileName"}, {TitleRole, "title"},
            {PreviewRole, "preview"}, {DateRole, "date"},         {PinnedRole, "pinned"},
            {FolderRole, "folder"},   {TagsRole, "tags"}};
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
    const QDir root(m_notesDir);
    const QStringList filters{QStringLiteral("*.md"), QStringLiteral("*.markdown")};
    const QDir::Filters fileFlags = QDir::Files | QDir::Readable | QDir::NoDotAndDotDot;

    // The root plus each first-level subfolder. Hidden folders and "assets"
    // (image attachments) are skipped.
    QStringList folders;
    for (const QFileInfo &info : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                                                     QDir::Name | QDir::IgnoreCase)) {
        if (info.fileName().startsWith(QLatin1Char('.'))
                || info.fileName().compare(QStringLiteral("assets"), Qt::CaseInsensitive) == 0)
            continue;
        folders.append(info.fileName());
    }

    QVector<Note> notes;
    QSet<QString> seen;
    auto collect = [&](const QDir &dir, const QString &folder) {
        for (const QFileInfo &info : dir.entryInfoList(filters, fileFlags)) {
            Note note;
            note.path = info.absoluteFilePath();
            note.fileName = info.fileName();
            note.folder = folder;
            note.modified = info.lastModified();
            note.pinned = m_pins.contains(note.path);
            seen.insert(note.path);

            CacheEntry &entry = m_cache[note.path];
            if (entry.modified != note.modified || entry.size != info.size()) {
                readNote(note);
                entry = {note.modified, info.size(), note.title, note.preview, note.tags};
            } else {
                note.title = entry.title;
                note.preview = entry.preview;
                note.tags = entry.tags;
            }
            notes.append(note);
        }
    };
    collect(root, QString());
    for (const QString &folder : folders)
        collect(QDir(root.filePath(folder)), folder);

    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (!seen.contains(it.key()))
            it = m_cache.erase(it);
        else
            ++it;
    }
    std::sort(notes.begin(), notes.end(), &NotesModel::lessThan);
    m_notes = notes;
    QSet<QString> tagSet;
    for (const Note &note : m_notes)
        for (const QString &tag : note.tags)
            tagSet.insert(tag);
    QStringList tags(tagSet.cbegin(), tagSet.cend());
    tags.sort();
    if (tags != m_tags) {
        m_tags = tags;
        emit tagsChanged();
    }
    if (!m_tag.isEmpty() && !m_tags.contains(m_tag)) {
        m_tag.clear();
        emit tagChanged();
    }
    if (folders != m_folders) {
        m_folders = folders;
        emit foldersChanged();
    }
    // A folder that disappeared on disk drops the filter back to all notes.
    if (!m_folder.isEmpty() && !m_folders.contains(m_folder)) {
        m_folder.clear();
        emit folderChanged();
    }
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
    // "tag:foo" words in the search box narrow by tag; the rest is free text.
    QStringList requiredTags;
    if (!m_tag.isEmpty())
        requiredTags.append(m_tag);
    QStringList words;
    for (const QString &word : m_filter.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (word.startsWith(QStringLiteral("tag:"), Qt::CaseInsensitive) && word.size() > 4)
            requiredTags.append(word.mid(4).toLower());
        else if (word.startsWith(QLatin1Char('#')) && word.size() > 1)
            requiredTags.append(word.mid(1).toLower());
        else
            words.append(word);
    }
    const QString needle = words.join(QLatin1Char(' ')).trimmed();
    for (int i = 0; i < m_notes.size(); ++i) {
        const Note &note = m_notes.at(i);
        if (!m_folder.isEmpty() && note.folder != m_folder)
            continue;
        const bool hasTags = std::all_of(requiredTags.cbegin(), requiredTags.cend(),
                                         [&note](const QString &tag) { return note.tags.contains(tag); });
        if (!hasTags)
            continue;
        if (needle.isEmpty()) {
            m_visible.append(i);
            continue;
        }
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
    note.tags = tagsFor(text);
}

QStringList NotesModel::tagsFor(const QString &text) {
    QSet<QString> tags;
    QStringList lines = text.split(QLatin1Char('\n'));

    // YAML front matter: "tags: [a, b]", "tags: a, b" or a "- a" list below "tags:".
    if (!lines.isEmpty() && lines.first().trimmed() == QStringLiteral("---")) {
        int end = -1;
        for (int i = 1; i < lines.size(); ++i) {
            if (lines.at(i).trimmed() == QStringLiteral("---")) { end = i; break; }
        }
        if (end > 0) {
            static const QRegularExpression tagsKey(QStringLiteral("^tags\\s*:\\s*(.*)$"));
            static const QRegularExpression listItem(QStringLiteral("^\\s*-\\s*(.+)$"));
            for (int i = 1; i < end; ++i) {
                const QRegularExpressionMatch key = tagsKey.match(lines.at(i));
                if (!key.hasMatch())
                    continue;
                QString value = key.captured(1).trimmed();
                if (value.isEmpty()) {
                    for (int j = i + 1; j < end; ++j) {
                        const QRegularExpressionMatch item = listItem.match(lines.at(j));
                        if (!item.hasMatch())
                            break;
                        value += item.captured(1) + QLatin1Char(',');
                    }
                }
                value.remove(QLatin1Char('[')).remove(QLatin1Char(']'));
                for (QString tag : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                    tag = tag.trimmed().remove(QLatin1Char('"')).remove(QLatin1Char('\''));
                    if (tag.startsWith(QLatin1Char('#')))
                        tag.remove(0, 1);
                    if (!tag.isEmpty())
                        tags.insert(tag.toLower());
                }
            }
            lines = lines.mid(end + 1);
        }
    }

    bool inFence = false;
    static const QRegularExpression inlineCode(QStringLiteral("`[^`]*`"));
    for (QString line : lines) {
        if (line.trimmed().startsWith(QStringLiteral("```"))) {
            inFence = !inFence;
            continue;
        }
        if (inFence)
            continue;
        line.remove(inlineCode);
        QRegularExpressionMatchIterator it = tagPattern().globalMatch(line);
        while (it.hasNext())
            tags.insert(it.next().captured(1).toLower());
    }
    QStringList out(tags.cbegin(), tags.cend());
    out.sort();
    return out;
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
    const QDir dir(m_folder.isEmpty() ? m_notesDir : QDir(m_notesDir).filePath(m_folder));
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
    QDir().mkpath(QFileInfo(path).absolutePath());
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
    const QString target = QFileInfo(path).dir().filePath(name);
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

bool NotesModel::validFolderName(const QString &name) {
    const QString trimmed = name.trimmed();
    return !trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('.'))
        && !trimmed.contains(QLatin1Char('/'))
        && trimmed.compare(QStringLiteral("assets"), Qt::CaseInsensitive) != 0;
}

bool NotesModel::createFolder(const QString &name) {
    if (m_notesDir.isEmpty() || !validFolderName(name))
        return false;
    const QString target = QDir(m_notesDir).filePath(name.trimmed());
    if (QFileInfo::exists(target))
        return false;
    if (!QDir().mkpath(target))
        return false;
    refresh();
    return true;
}

bool NotesModel::renameFolder(const QString &folder, const QString &newName) {
    if (!m_folders.contains(folder) || !validFolderName(newName))
        return false;
    const QString name = newName.trimmed();
    if (name == folder)
        return true;
    const QDir root(m_notesDir);
    const QString from = root.filePath(folder);
    const QString to = root.filePath(name);
    if (QFileInfo::exists(to))
        return false;
    if (!QDir().rename(from, to))
        return false;
    // Pins are keyed by absolute path; rewrite the ones that moved.
    bool pinsChanged = false;
    for (const QString &pin : QSet<QString>(m_pins)) {
        if (pin.startsWith(from + QLatin1Char('/'))) {
            m_pins.remove(pin);
            m_pins.insert(to + pin.mid(from.size()));
            pinsChanged = true;
        }
    }
    if (pinsChanged)
        savePins();
    if (m_folder == folder) {
        m_folder = name;
        emit folderChanged();
    }
    refresh();
    return true;
}

bool NotesModel::removeFolder(const QString &folder) {
    if (!m_folders.contains(folder))
        return false;
    const QString target = QDir(m_notesDir).filePath(folder);
    QFile dir(target);
    bool removed = dir.moveToTrash();
    if (!removed)
        removed = QDir(target).removeRecursively();
    if (!removed)
        return false;
    bool pinsChanged = false;
    for (const QString &pin : QSet<QString>(m_pins)) {
        if (pin.startsWith(target + QLatin1Char('/'))) {
            m_pins.remove(pin);
            pinsChanged = true;
        }
    }
    if (pinsChanged)
        savePins();
    const QVector<Note> before = m_notes;
    refresh();
    for (const Note &note : before) {
        if (note.folder == folder)
            emit noteRemoved(note.path);
    }
    return true;
}

bool NotesModel::moveNote(const QString &path, const QString &folder) {
    if (!contains(path))
        return false;
    if (!folder.isEmpty() && !m_folders.contains(folder))
        return false;
    const QDir root(m_notesDir);
    const QString targetDir = folder.isEmpty() ? m_notesDir : root.filePath(folder);
    const QString target = QDir(targetDir).filePath(QFileInfo(path).fileName());
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

QString NotesModel::folderOf(const QString &path) const {
    for (const Note &note : m_notes) {
        if (note.path == path)
            return note.folder;
    }
    return {};
}

int NotesModel::countInFolder(const QString &folder) const {
    return std::count_if(m_notes.cbegin(), m_notes.cend(),
                         [&folder](const Note &note) { return folder.isEmpty() || note.folder == folder; });
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
    if (!m_notesDir.isEmpty() && QDir(m_notesDir).exists()) {
        m_watcher.addPath(m_notesDir);
        for (const QString &folder : m_folders)
            m_watcher.addPath(QDir(m_notesDir).filePath(folder));
    }
}
