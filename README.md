<p align="center"><img src="docs/icon.svg" width="96" alt="Omanote icon"></p>

# Omanote

**A dead-simple Markdown notes app for Omarchy, with a collapsible notes sidebar.**

Omanote is a fork of [Omawrite](https://github.com/omacom-io/omawrite), the distraction-free
Markdown writer that ships with Omarchy. It keeps everything Omawrite does, then adds the one thing a
notes app needs: a side panel that lists all your notes, in the spirit of Apple Notes.

<p align="center"><img src="docs/screenshot.png" width="720" alt="Omanote with the notes sidebar open"></p>

## Why

Omawrite is one window, one file. That is perfect for writing an essay and awkward for the fifty small
notes you collect in a week. Omanote turns a folder of Markdown files into a library you can browse,
search, pin, and switch between without touching a file picker. Collapse the sidebar and you are back in
plain Omawrite.

## Features

- **Notes are plain files.** Every note is a `.md` file in `~/Documents/notes`. Use them with any other
  tool, sync them however you like. File names never change behind your back.
- **Autosave.** Notes save about a second after you stop typing, when you switch notes, when the window
  loses focus, and on quit. Files opened from elsewhere with Ctrl+O keep Omawrite's explicit save.
- **Sidebar.** Title, one-line preview, and date for every note, newest first. Pinned notes stay on top.
  Drag the edge to resize, or drag it closed.
- **Search.** Filters by title and body as you type.
- **Delete to trash.** With a confirmation, never a silent remove.
- **System theme.** Colours come from the same Omarchy theme source Omawrite uses, so a theme change
  restyles the sidebar and editor together, live, with no restart.
- **Text size.** Follows `omarchy display text size` like Omawrite does.

## Install

On Omarchy or any Arch-based system:

```sh
git clone https://github.com/ratandeepbansal/omanote.git
cd omanote
./bin/install
```

That builds the package with `makepkg` and installs it alongside Omawrite. Omanote uses its own binary,
config file, and data folder, so the two coexist.

To build without installing, run `./bin/build` and find the binary in `build/`.

Requirements: `qt6-base`, `qt6-declarative`, `xdg-desktop-portal`, and `gcc` and `make` to build.

## Usage

Launch `omanote` from the app menu or a terminal. Pass a file to open it directly. Pass
`--notes-dir DIR` to use a different notes folder, or set `notes/folder` in
`~/.config/Omacom/omanote.conf`.

### Shortcuts

| Keys | Action |
|---|---|
| `Ctrl+N` | New note |
| `Ctrl+\` | Show or hide the sidebar |
| `Ctrl+Shift+F` | Search notes |
| `Ctrl+Alt+Up` / `Ctrl+Alt+Down` | Previous / next note |
| `Ctrl+Shift+P` | Pin or unpin the current note |
| `Delete` (on a selected row) | Delete note, with confirmation |
| Right-click a note | Pin, rename file, show in folder, delete |
| `Ctrl+Shift+N` | New window |
| `Ctrl+S` / `Ctrl+Shift+S` | Save / save as (for files outside the notes folder) |
| `Ctrl+O` | Open any Markdown file |
| `Ctrl+F` / `Ctrl+H` | Find / find and replace |
| `Ctrl+B` / `Ctrl+I` / `Ctrl+K` | Bold / italic / link |
| `Ctrl+P` | Print |
| `F11` / `Super+F` | Fullscreen |
| `Ctrl+?` | Shortcut reference |

## Development

```sh
./bin/build   # qmake + make into build/
./bin/test    # unit and QML integration tests, offscreen
```

The notes library lives in `src/notesmodel.{h,cpp}` and `src/NotesSidebar.qml`. The editor, theming,
recovery, and everything else is Omawrite's code, lightly extended in `src/backend.cpp` for autosave.
`PRD.md` records the product decisions.

## Credits and license

Omanote is MIT licensed, like Omawrite. Omawrite is by David Heinemeier Hansson and contributors at
[omacom-io/omawrite](https://github.com/omacom-io/omawrite); the original copyright notice is retained in
`LICENSE`. The bundled iA Writer Mono font is under the SIL Open Font License 1.1, see `fonts/OFL.txt`.
