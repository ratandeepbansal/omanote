import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Note library panel. Every colour derives from the theme colours the backend
// publishes, so it follows the system theme exactly like the editor does.
Item {
    id: panel

    property bool darkMode: true
    property color pageColor: "#1a1a1a"
    property color textColor: "#d0d0d0"
    property color mutedColor: "#909191"
    property color accentColor: "#428bca"
    property color selectionColor: "#333333"
    property real textScale: 1
    property string currentPath: ""

    signal noteActivated(string path)
    signal newNoteRequested()
    signal deleteRequested(string path, string title)
    signal renameRequested(string path, string fileName)
    signal collapseRequested()
    signal newFolderRequested()
    signal moveRequested(string path, string folder)
    signal renameFolderRequested(string folder)
    signal deleteFolderRequested(string folder)

    function moveCurrentNote() {
        if (panel.currentPath === "" || !notesModel.contains(panel.currentPath))
            return;
        moveMenu.notePath = panel.currentPath;
        moveMenu.popup();
    }

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * panel.textScale));
    }

    function focusSearch() {
        filterField.forceActiveFocus();
        filterField.selectAll();
    }

    function focusList() {
        noteList.forceActiveFocus();
        if (noteList.currentIndex < 0 && noteList.count > 0)
            noteList.currentIndex = 0;
    }

    function moveSelection(delta) {
        if (noteList.count === 0)
            return;
        var row = notesModel.indexOf(panel.currentPath);
        var next = row < 0 ? 0 : Math.max(0, Math.min(noteList.count - 1, row + delta));
        panel.noteActivated(notesModel.pathAt(next));
    }

    // Mix the page colour a little toward the text colour for the panel ground,
    // so it separates from the page without leaving the theme's palette.
    readonly property color panelColor: Qt.rgba(
        pageColor.r * 0.96 + textColor.r * 0.04,
        pageColor.g * 0.96 + textColor.g * 0.04,
        pageColor.b * 0.96 + textColor.b * 0.04, 1)
    readonly property color hoverColor: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.06)
    readonly property color lineColor: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.12)

    Rectangle {
        anchors.fill: parent
        color: panel.panelColor
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header: search + new note
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 8
            Layout.topMargin: 12
            Layout.bottomMargin: 8
            spacing: 6

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: panel.scaledSize(30)
                color: panel.hoverColor
                border.color: filterField.activeFocus ? panel.accentColor : "transparent"
                border.width: 1
                radius: 6

                TextInput {
                    id: filterField
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    verticalAlignment: TextInput.AlignVCenter
                    clip: true
                    selectByMouse: true
                    color: panel.textColor
                    selectionColor: panel.selectionColor
                    selectedTextColor: panel.textColor
                    font.family: "iA Writer Mono S"
                    font.pixelSize: panel.scaledSize(12)
                    onTextChanged: notesModel.filter = text
                    Keys.onEscapePressed: function(event) {
                        if (text.length > 0) {
                            text = "";
                        } else {
                            panel.focusList();
                        }
                        event.accepted = true;
                    }
                    Keys.onDownPressed: panel.focusList()
                    Keys.onReturnPressed: {
                        if (noteList.count > 0)
                            panel.noteActivated(notesModel.pathAt(0));
                    }
                }

                Label {
                    anchors.fill: filterField
                    verticalAlignment: Text.AlignVCenter
                    text: "Search"
                    visible: filterField.text.length === 0
                    color: panel.mutedColor
                    font.family: "iA Writer Mono S"
                    font.pixelSize: panel.scaledSize(12)
                }
            }

            FooterIconButton {
                iconName: "new"
                iconColor: panel.mutedColor
                tooltip: "New note (Ctrl+N)"
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Layout.rightMargin: 4
                onClicked: panel.newNoteRequested()
            }
        }

        // Folder strip: "All" plus one chip per subfolder of the notes dir.
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 8
            spacing: 4

            Repeater {
                model: [""].concat(notesModel.folders)

                delegate: Rectangle {
                    id: chip
                    required property string modelData
                    readonly property bool active: notesModel.folder === modelData
                    readonly property string label: modelData === "" ? "All" : modelData

                    width: chipLabel.implicitWidth + panel.scaledSize(16)
                    height: panel.scaledSize(22)
                    radius: height / 2
                    color: active ? panel.selectionColor
                         : chipMouse.containsMouse ? panel.hoverColor : "transparent"
                    border.width: 1
                    border.color: active ? "transparent" : panel.lineColor

                    Label {
                        id: chipLabel
                        anchors.centerIn: parent
                        text: chip.label
                        color: chip.active ? panel.textColor : panel.mutedColor
                        font.family: "iA Writer Mono S"
                        font.pixelSize: panel.scaledSize(11)
                    }

                    MouseArea {
                        id: chipMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: function(mouse) {
                            if (mouse.button === Qt.RightButton && chip.modelData !== "") {
                                folderMenu.folder = chip.modelData;
                                folderMenu.popup();
                                return;
                            }
                            notesModel.folder = chip.modelData;
                        }
                    }
                }
            }

            Rectangle {
                width: panel.scaledSize(22)
                height: panel.scaledSize(22)
                radius: height / 2
                color: addFolderMouse.containsMouse ? panel.hoverColor : "transparent"
                border.width: 1
                border.color: panel.lineColor

                Label {
                    anchors.centerIn: parent
                    text: "+"
                    color: panel.mutedColor
                    font.family: "iA Writer Mono S"
                    font.pixelSize: panel.scaledSize(12)
                }

                MouseArea {
                    id: addFolderMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: panel.newFolderRequested()
                }
                ToolTip.visible: addFolderMouse.containsMouse
                ToolTip.text: "New folder"
                ToolTip.delay: 600
            }
        }

        // Tag strip: one chip per tag found across the library. Hidden when
        // no note carries a tag, so an untagged library looks like before.
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 8
            spacing: 4
            visible: notesModel.tags.length > 0

            Repeater {
                model: notesModel.tags

                delegate: Rectangle {
                    id: tagChip
                    required property string modelData
                    readonly property bool active: notesModel.tag === modelData

                    width: tagLabel.implicitWidth + panel.scaledSize(12)
                    height: panel.scaledSize(20)
                    radius: 4
                    color: active ? Qt.rgba(panel.accentColor.r, panel.accentColor.g, panel.accentColor.b, 0.25)
                         : tagMouse.containsMouse ? panel.hoverColor : "transparent"

                    Label {
                        id: tagLabel
                        anchors.centerIn: parent
                        text: "#" + tagChip.modelData
                        color: tagChip.active ? panel.textColor : panel.accentColor
                        font.family: "iA Writer Mono S"
                        font.pixelSize: panel.scaledSize(11)
                    }

                    MouseArea {
                        id: tagMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: notesModel.tag = tagChip.active ? "" : tagChip.modelData
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: panel.lineColor
        }

        ListView {
            id: noteList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: notesModel
            currentIndex: notesModel.indexOf(panel.currentPath)
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            highlightFollowsCurrentItem: true
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Keys.onUpPressed: panel.moveSelection(-1)
            Keys.onDownPressed: panel.moveSelection(1)
            Keys.onReturnPressed: {
                var path = notesModel.pathAt(currentIndex);
                if (path !== "")
                    panel.noteActivated(path);
            }
            Keys.onDeletePressed: {
                var row = currentIndex;
                if (row < 0)
                    return;
                var item = noteList.itemAtIndex(row);
                panel.deleteRequested(notesModel.pathAt(row), item ? item.noteTitle : "");
            }

            delegate: Item {
                id: row
                required property int index
                required property string path
                required property string fileName
                required property string title
                required property string preview
                required property string date
                required property bool pinned
                required property string folder

                readonly property string noteTitle: title
                readonly property bool selected: path === panel.currentPath

                width: ListView.view.width
                height: rowContent.implicitHeight + panel.scaledSize(20)

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    radius: 6
                    color: row.selected
                        ? panel.selectionColor
                        : rowMouse.containsMouse ? panel.hoverColor : "transparent"
                }

                ColumnLayout {
                    id: rowContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    spacing: panel.scaledSize(3)

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label {
                            Layout.fillWidth: true
                            text: row.title
                            color: panel.textColor
                            elide: Text.ElideRight
                            font.family: "iA Writer Mono S"
                            font.pixelSize: panel.scaledSize(13)
                            font.bold: true
                        }

                        Canvas {
                            visible: row.pinned
                            width: panel.scaledSize(10)
                            height: panel.scaledSize(10)
                            onPaint: {
                                var ctx = getContext("2d");
                                ctx.clearRect(0, 0, width, height);
                                ctx.fillStyle = panel.accentColor;
                                ctx.beginPath();
                                ctx.arc(width / 2, height / 2, width / 2, 0, Math.PI * 2);
                                ctx.fill();
                            }
                            Connections {
                                target: panel
                                function onAccentColorChanged() { requestPaint(); }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            text: row.date
                            color: panel.mutedColor
                            font.family: "iA Writer Mono S"
                            font.pixelSize: panel.scaledSize(11)
                        }

                        Label {
                            visible: notesModel.folder === "" && row.folder !== ""
                            text: row.folder
                            color: panel.accentColor
                            opacity: 0.85
                            elide: Text.ElideRight
                            Layout.maximumWidth: panel.scaledSize(90)
                            font.family: "iA Writer Mono S"
                            font.pixelSize: panel.scaledSize(11)
                        }

                        Label {
                            Layout.fillWidth: true
                            text: row.preview.length > 0 ? row.preview : "No additional text"
                            color: panel.mutedColor
                            elide: Text.ElideRight
                            font.family: "iA Writer Mono S"
                            font.pixelSize: panel.scaledSize(11)
                        }
                    }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function(mouse) {
                        noteList.currentIndex = row.index;
                        if (mouse.button === Qt.RightButton) {
                            rowMenu.notePath = row.path;
                            rowMenu.noteTitle = row.title;
                            rowMenu.noteFileName = row.fileName;
                            rowMenu.notePinned = row.pinned;
                            rowMenu.popup();
                            return;
                        }
                        panel.noteActivated(row.path);
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: noteList.count === 0
                text: notesModel.totalCount === 0 ? "No notes yet"
                : (filterField.text.length === 0 && notesModel.tag === "" ? "No notes in this folder" : "No matches")
                color: panel.mutedColor
                font.family: "iA Writer Mono S"
                font.pixelSize: panel.scaledSize(12)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: panel.lineColor
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.topMargin: 8
            Layout.bottomMargin: 10
            text: notesModel.count + (notesModel.count === 1 ? " Note" : " Notes")
            color: panel.mutedColor
            opacity: 0.75
            font.family: "iA Writer Mono S"
            font.pixelSize: panel.scaledSize(11)
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: panel.lineColor
    }

    Menu {
        id: rowMenu
        property string notePath: ""
        property string noteTitle: ""
        property string noteFileName: ""
        property bool notePinned: false

        MenuItem {
            text: rowMenu.notePinned ? "Unpin" : "Pin"
            onTriggered: notesModel.setPinned(rowMenu.notePath, !rowMenu.notePinned)
        }
        MenuItem {
            text: "Rename file…"
            onTriggered: panel.renameRequested(rowMenu.notePath, rowMenu.noteFileName)
        }
        MenuItem {
            text: "Move to folder…"
            onTriggered: {
                moveMenu.notePath = rowMenu.notePath;
                moveMenu.popup();
            }
        }
        MenuItem {
            text: "Show in folder"
            onTriggered: backend.openExternalUrl("file://" + rowMenu.notePath.substring(0, rowMenu.notePath.lastIndexOf("/")))
        }
        MenuSeparator {}
        MenuItem {
            text: "Delete…"
            onTriggered: panel.deleteRequested(rowMenu.notePath, rowMenu.noteTitle)
        }
    }

    Menu {
        id: moveMenu
        property string notePath: ""

        Instantiator {
            model: [""].concat(notesModel.folders)
            delegate: MenuItem {
                required property string modelData
                text: modelData === "" ? "Notes (top level)" : modelData
                enabled: notesModel.folderOf(moveMenu.notePath) !== modelData
                onTriggered: panel.moveRequested(moveMenu.notePath, modelData)
            }
            onObjectAdded: function(index, object) { moveMenu.insertItem(index, object) }
            onObjectRemoved: function(index, object) { moveMenu.removeItem(object) }
        }
    }

    Menu {
        id: folderMenu
        property string folder: ""

        MenuItem {
            text: "Rename folder…"
            onTriggered: panel.renameFolderRequested(folderMenu.folder)
        }
        MenuItem {
            text: "Show in file manager"
            onTriggered: backend.openExternalUrl("file://" + notesModel.notesDir + "/" + folderMenu.folder)
        }
        MenuSeparator {}
        MenuItem {
            text: "Delete folder…"
            onTriggered: panel.deleteFolderRequested(folderMenu.folder)
        }
    }
}
