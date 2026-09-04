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
                text: notesModel.totalCount === 0 ? "No notes yet" : "No matches"
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
            text: notesModel.totalCount + (notesModel.totalCount === 1 ? " Note" : " Notes")
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
            text: "Show in folder"
            onTriggered: backend.openExternalUrl("file://" + notesModel.notesDir)
        }
        MenuSeparator {}
        MenuItem {
            text: "Delete…"
            onTriggered: panel.deleteRequested(rowMenu.notePath, rowMenu.noteTitle)
        }
    }
}
