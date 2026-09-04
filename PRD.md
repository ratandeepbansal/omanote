# Omanote — Product Requirements Document

**Status:** v1 implemented (2026-09-04) · **Date:** 2026-09-04 · **Owner:** Ratandeep Bansal

## 1. Summary

Omanote is a fork of [omawrite](https://github.com/omacom-io/omawrite) (MIT, Qt 6 Quick + C++) that adds a
collapsible side panel for managing many notes, in the spirit of Apple Notes. Everything omawrite does today
(distraction-free Markdown editing, system dark/light theming, find/replace, print, recovery) is preserved.
The addition is a notes library: a folder of `.md` files listed in a sidebar, with autosave, search, pinning
and deletion.

## 2. Background

- omawrite is a single-document editor: one window, one file, explicit Ctrl+S, XDG portal file pickers.
- License check: MIT (app) + OFL-1.1 (bundled iA Writer Mono font). Forking and renaming is permitted; the
  MIT copyright notice and OFL notice must be retained.
- Architecture (upstream, ~3k lines):
  - `src/backend.{h,cpp}` — file I/O, recovery, portal dialogs, word count, theme colours, window geometry.
  - `src/systemtheme.{h,cpp}` — reads Omarchy/GNOME theme over D-Bus portal, emits dark/light + palette.
  - `src/markdownhighlighter.{h,cpp}` — QSyntaxHighlighter for Markdown.
  - `src/Main.qml` (~1000 lines) — the whole UI: editor, footer, search bar, dialogs, shortcuts.
  - Build: qmake (`omawrite.pro`), Arch `PKGBUILD` in `pkgbuild/`.

## 3. Goals

1. Capture and switch between many notes without leaving the app or touching a file picker.
2. Sidebar can be collapsed to return to omawrite's pure single-column writing view.
3. Notes are plain Markdown files on disk, usable by any other tool.
4. Follow the system theme exactly as omawrite does (same `SystemTheme` source, same palette properties).
5. Coexist with the installed omawrite package (separate binary, config, data, desktop entry).

## 4. Non-goals (v1)

- Notebooks. (v0.2 shipped folders as one level of subdirectories, and tags as `#tag` text / front matter.)
- Sync, cloud, or accounts.
- Rich text, attachments, images, checklists rendering.
- Multi-window note library (Ctrl+N still opens a new window, sharing the same folder).
- Rewriting the editor. The editing surface stays omawrite's.

## 5. Users and use cases

- Omarchy user who wants quick notes with the same aesthetic as omawrite.
- Scenarios: jot a thought (new note, type, done, autosaved); find an older note by search; keep a few
  reference notes pinned; delete junk; collapse the sidebar to write long-form.

## 6. Decisions already made

| Topic | Decision |
|---|---|
| Storage | Folder of `.md` files, default `~/Documents/notes`, configurable in `omanote.conf` |
| Saving | Autosave (debounced ~1 s after last edit, plus on note switch, blur, and quit). Ctrl+S forces save. |
| Sidebar rows | Title + one-line preview + modified date, sorted by modified desc, pinned section first |
| Search | Filter box at top of sidebar, matches title and body, case-insensitive |
| Pinning | Per-note pin, persisted outside the note file |
| Deletion | Sidebar context menu / Delete key, confirmation dialog, file moved to trash |
| Name | `omanote`; binary `/usr/bin/omanote`, config `~/.config/Omacom/omanote.conf`, data `~/.local/share/Omacom/omanote` |

## 7. Functional requirements

### 7.1 Notes library
- **FR1** On launch, scan the notes folder (non-recursive) for `*.md` files and list them.
- **FR2** If the folder does not exist, create it. If it is empty, create and open a "Welcome" note.
- **FR3** Watch the folder with `QFileSystemWatcher`; external adds/removes/edits update the list. Editing an
  open note externally reuses omawrite's existing external-change dialog.
- **FR4** Note title = first non-empty line with leading `#` and whitespace stripped, falling back to the
  file name without extension. Preview = next non-empty line, Markdown symbols stripped, truncated.
- **FR5** File naming: new notes are created as `Untitled.md`, `Untitled 2.md`, … File names are stable
  and never renamed automatically. The displayed title always comes from the file content (FR4), so the
  file name is only a storage identifier. A "Rename" action in the context menu allows manual renames.
- **FR6** Pins are stored in `~/.local/share/Omacom/omanote/pins.json` keyed by file path.
- **FR7** Sort: pinned (modified desc), then unpinned (modified desc).

### 7.2 Sidebar
- **FR8** Left-side panel, default width 260 px (scaled by text scale), min 200, max 40 % of window,
  user-resizable by dragging the divider. Width persisted.
- **FR9** Collapsible via toggle button in the footer, `Ctrl+\`, and a swipe/drag of the divider to zero.
  Collapsed state persisted. When collapsed the editor recenters exactly as upstream omawrite.
- **FR10** Top of sidebar: search field and "new note" button. `Ctrl+Shift+F` focuses search; `Esc` clears it.
- **FR11** Row shows title (bold), preview (muted), date (muted, right-aligned or below). Selected row uses
  the theme selection colour. Pinned rows show a small pin glyph.
- **FR11a** Date column shows an absolute short date: "Sep 4" for the current year, "Sep 4, 2025" otherwise.
- **FR12** Keyboard: `Ctrl+N` new note (upstream's new-window moves to `Ctrl+Shift+N`), `Ctrl+Alt+Up/Down`
  previous/next note, `Delete` on a focused row prompts deletion, `Ctrl+Shift+P` toggles pin.
- **FR13** Right-click context menu on a row: Pin/Unpin, Rename, Delete, Reveal in file manager (xdg-open).
- **FR14** Empty states: "No notes yet" and "No matches" messages in muted colour.

### 7.3 Editor integration
- **FR15** Selecting a row loads that file into the existing editor through `backend.open()`; the previous
  note is autosaved first if modified. No unsaved-changes prompt for note switching.
- **FR16** Ctrl+O still opens an arbitrary file via portal; such a file is shown in the editor and, if it is
  outside the notes folder, is not added to the list (footer shows its path).
- **FR17** Autosave: debounce timer (1000 ms) after `modifiedChanged`; also on note switch, window
  deactivation, and `aboutToQuit`. Upstream crash recovery remains active for the current document.
- **FR18** Word count, status text, search, find/replace, print, fullscreen, and shortcuts are unchanged.

### 7.4 Theming
- **FR19** Sidebar uses only `backend.themeBackground / themeForeground / themeAccent / themeSelection`
  and `backend.darkMode`, so it changes live with the system theme exactly like the editor.
- **FR20** Sidebar background is the theme background mixed ~4 % toward the foreground to give subtle
  separation; divider is foreground at ~12 % opacity. Fonts: same iA Writer Mono family and `textScale`.

### 7.5 Configuration
- **FR21** `omanote.conf` gains `[notes] folder=`, `[sidebar] width=`, `[sidebar] collapsed=`.
- **FR22** Command-line: `omanote [file.md]` behaves like omawrite; `omanote --notes-dir DIR` overrides folder.

## 8. Non-functional requirements

- Startup with 1,000 notes under 300 ms on the MacBook Pro; list is a `ListView` backed by a C++
  `QAbstractListModel`, previews computed lazily and cached.
- No new runtime dependencies beyond upstream (`qt6-base`, `qt6-declarative`, `xdg-desktop-portal`).
- Never lose data: autosave writes to a temp file then renames; deletion uses `QFile::moveToTrash` (system trash), never `rm`.
- Text scale changes re-flow the sidebar without restart (same mechanism as editor).

## 9. Technical design (high level)

- New C++ `NotesModel : QAbstractListModel` (`src/notesmodel.{h,cpp}`) — roles: path, title, preview,
  modified, pinned; methods: `refresh()`, `create()`, `remove(path)`, `setPinned(path,bool)`,
  `setFilter(text)`. Owns the `QFileSystemWatcher` and pin store.
- `Backend` gains: `notesDir`, `autosaveEnabled`, `autosave()` slot wired to a `QTimer`, and a
  `currentPath` property the model uses for selection.
- `Main.qml`: wrap the existing editor column in a `RowLayout` / `SplitView` with a new
  `src/NotesSidebar.qml`; add `NoteRow.qml`, `DeleteNoteDialog.qml`. Collapse animates width to 0.
- Rename: `omawrite.pro` → `omanote.pro`, `TARGET=omanote`, `QCoreApplication` app name `omanote`,
  new `pkgbuild/` files and icon. Keep upstream git history via fork so future merges are possible.
- Tests: extend `tests/tst_omawrite.cpp` with NotesModel tests (title extraction, sort order, filter,
  pin persistence, rename collision).

## 10. Milestones

1. **M1 Fork & rename** — builds and installs as `omanote`, identical to omawrite. (0.5 day)
2. **M2 NotesModel + sidebar list** — read-only list, click to open, collapse. (1 day)
3. **M3 Create / autosave / rename-on-title / delete** (1 day)
4. **M4 Search, pin, context menu, keyboard nav, persistence** (1 day)
5. **M5 Polish** — theming pass, empty states, animations, PKGBUILD, README, tests. (0.5 day)

## 11. Resolved questions (2026-09-04)

1. File names are stable; title lives only in the file content. Manual rename available.
2. Notes folder defaults to `~/Documents/notes`.
3. Dates are absolute short form ("Sep 4").
4. `Ctrl+N` = new note, `Ctrl+Shift+N` = new window (checked: `Ctrl+Shift+N` is unused upstream).
5. Deletion moves the file to the system trash.

## 12. Success criteria

- A user can create, find, switch, pin, and delete notes using only the keyboard.
- Collapsing the sidebar yields a pixel-equivalent omawrite layout.
- Theme switch (`omarchy theme` change) updates sidebar and editor simultaneously with no restart.
- Zero data-loss bugs in a 1-week dogfood.
