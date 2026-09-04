#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

// Lists the Markdown files in the notes folder. Titles and previews come from
// the file contents, never from the file name, so file names stay stable.
class NotesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString notesDir READ notesDir NOTIFY notesDirChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    // Folder filter: "" shows every note, otherwise only notes in that subfolder.
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)
    // Tag filter: "" shows every note, otherwise only notes carrying that tag.
    Q_PROPERTY(QString tag READ tag WRITE setTag NOTIFY tagChanged)
    Q_PROPERTY(QStringList tags READ tags NOTIFY tagsChanged)
    // "modified" (default), "created" or "title". Pinned notes always come first.
    Q_PROPERTY(QString sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)

public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        FileNameRole,
        TitleRole,
        PreviewRole,
        DateRole,
        PinnedRole,
        FolderRole,
        TagsRole,
        FullDateRole,
    };
    Q_ENUM(Roles)

    struct Note {
        QString path;
        QString fileName;
        QString title;
        QString preview;
        QDateTime modified;
        QDateTime created;
        bool pinned = false;
        QString folder; // relative subfolder, empty for the notes root
        QStringList tags; // lower-case, unique, sorted
    };

    explicit NotesModel(QObject *parent = nullptr);

    void setNotesDir(const QString &dir);
    QString notesDir() const { return m_notesDir; }
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    int count() const { return m_visible.size(); }
    int totalCount() const { return m_notes.size(); }
    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);
    QStringList folders() const { return m_folders; }
    QString tag() const { return m_tag; }
    void setTag(const QString &tag);
    QStringList tags() const { return m_tags; }
    QString sortMode() const { return m_sortMode; }
    void setSortMode(const QString &mode);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString createNote();
    Q_INVOKABLE bool createFolder(const QString &name);
    Q_INVOKABLE bool renameFolder(const QString &folder, const QString &newName);
    Q_INVOKABLE bool removeFolder(const QString &folder);
    Q_INVOKABLE bool moveNote(const QString &path, const QString &folder);
    Q_INVOKABLE QString folderOf(const QString &path) const;
    Q_INVOKABLE int countInFolder(const QString &folder) const;
    Q_INVOKABLE bool removeNote(const QString &path);
    Q_INVOKABLE bool renameNote(const QString &path, const QString &newFileName);
    Q_INVOKABLE void setPinned(const QString &path, bool pinned);
    Q_INVOKABLE bool isPinned(const QString &path) const;
    Q_INVOKABLE int indexOf(const QString &path) const;
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE bool contains(const QString &path) const;
    Q_INVOKABLE QString untitledPath() const;

    static QString titleFor(const QString &text, const QString &fileName);
    static QString previewFor(const QString &text);
    static QString dateLabel(const QDateTime &modified, const QDateTime &now);
    static QString fullDateLabel(const QDateTime &modified);
    static QString stripMarkdown(const QString &line);
    static bool validFolderName(const QString &name);
    static QStringList tagsFor(const QString &text);

signals:
    void notesDirChanged();
    void filterChanged();
    void countChanged();
    void folderChanged();
    void foldersChanged();
    void tagChanged();
    void tagsChanged();
    void sortModeChanged();
    void noteRemoved(const QString &path);

private:
    void scanDirectory();
    void rebuildVisible();
    void readNote(Note &note) const;
    void loadPins();
    void savePins() const;
    QString pinsPath() const;
    void watchDirectory();
    bool lessThan(const Note &a, const Note &b) const;
    void sortNotes();

    QString m_notesDir;
    QString m_filter;
    QString m_folder;
    QStringList m_folders;
    QString m_tag;
    QStringList m_tags;
    QString m_sortMode = QStringLiteral("modified");
    QVector<Note> m_notes;
    QVector<int> m_visible;
    QSet<QString> m_pins;
    QFileSystemWatcher m_watcher;
    QTimer m_refreshTimer;
    // Cache of parsed titles/previews keyed by path; invalidated by mtime+size.
    struct CacheEntry { QDateTime modified; qint64 size; QString title; QString preview; QStringList tags; };
    mutable QHash<QString, CacheEntry> m_cache;
};
