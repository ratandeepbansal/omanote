import QtQuick
import QtQuick.Controls

Dialog {
    id: root

    property string noteTitle: ""
    property string notePath: ""
    property bool darkMode: true
    property color textColor: darkMode ? "#d0d0d0" : "#42464c"
    property color strongTextColor: darkMode ? "#eeeeee" : "#222324"
    property color activeButtonColor: "#428bca"
    property int containerWidth: 420
    property int containerHeight: 320
    property real textScale: 1

    signal deleteConfirmed(string path)

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    onOpened: deleteButton.forceActiveFocus()
    width: Math.min(420, containerWidth - 48)
    x: Math.round((containerWidth - width) / 2)
    y: Math.round((containerHeight - height) / 2)
    padding: 20

    background: Rectangle {
        color: root.darkMode ? "#1a1a1a" : "#ffffff"
        border.color: root.darkMode ? "#343434" : "#d8d8d8"
        radius: 0
    }

    contentItem: Column {
        spacing: 12

        Label {
            text: "Delete note"
            color: root.strongTextColor
            font.family: "iA Writer Mono S"
            font.pixelSize: Math.round(16 * root.textScale)
            font.bold: true
        }

        Label {
            width: parent.width
            text: "Move \"" + root.noteTitle + "\" to the trash?"
            color: root.textColor
            wrapMode: Text.Wrap
            font.family: "iA Writer Mono S"
            font.pixelSize: Math.round(13 * root.textScale)
        }
    }

    footer: Item {
        implicitHeight: dialogButtons.implicitHeight + 20

        Row {
            id: dialogButtons
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            SquareDialogButton {
                id: cancelButton
                text: "Cancel"
                darkMode: root.darkMode
                textScale: root.textScale
                labelColor: root.textColor
                KeyNavigation.left: deleteButton
                KeyNavigation.right: deleteButton
                KeyNavigation.tab: deleteButton
                KeyNavigation.backtab: deleteButton
                onClicked: root.reject()
            }

            SquareDialogButton {
                id: deleteButton
                text: "Delete"
                primary: true
                darkMode: root.darkMode
                textScale: root.textScale
                activeColor: root.activeButtonColor
                KeyNavigation.left: cancelButton
                KeyNavigation.right: cancelButton
                KeyNavigation.tab: cancelButton
                KeyNavigation.backtab: cancelButton
                onClicked: {
                    root.close();
                    root.deleteConfirmed(root.notePath);
                }
            }
        }
    }
}
