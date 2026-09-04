import QtQuick
import QtQuick.Controls

Dialog {
    id: root

    property string notePath: ""
    property string fileName: ""
    property bool darkMode: true
    property color textColor: darkMode ? "#d0d0d0" : "#42464c"
    property color strongTextColor: darkMode ? "#eeeeee" : "#222324"
    property color activeButtonColor: "#428bca"
    property color selectionColor: "#333333"
    property int containerWidth: 420
    property int containerHeight: 320
    property real textScale: 1
    property string error: ""

    signal renameConfirmed(string path, string newFileName)

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    onOpened: {
        error = "";
        nameField.text = fileName;
        var dot = fileName.lastIndexOf(".");
        nameField.select(0, dot > 0 ? dot : fileName.length);
        nameField.forceActiveFocus();
    }
    width: Math.min(420, containerWidth - 48)
    x: Math.round((containerWidth - width) / 2)
    y: Math.round((containerHeight - height) / 2)
    padding: 20

    background: Rectangle {
        color: root.darkMode ? "#1a1a1a" : "#ffffff"
        border.color: root.darkMode ? "#343434" : "#d8d8d8"
        radius: 0
    }

    function submit() {
        root.renameConfirmed(root.notePath, nameField.text);
    }

    contentItem: Column {
        spacing: 12

        Label {
            text: "Rename file"
            color: root.strongTextColor
            font.family: "iA Writer Mono S"
            font.pixelSize: Math.round(16 * root.textScale)
            font.bold: true
        }

        Rectangle {
            width: parent.width
            height: Math.round(34 * root.textScale)
            color: root.darkMode ? "#202020" : "#f6f6f6"
            border.color: nameField.activeFocus ? root.activeButtonColor
                                                : (root.darkMode ? "#424242" : "#c8c8c8")

            TextInput {
                id: nameField
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                verticalAlignment: TextInput.AlignVCenter
                selectByMouse: true
                clip: true
                color: root.textColor
                selectionColor: root.selectionColor
                selectedTextColor: root.strongTextColor
                font.family: "iA Writer Mono S"
                font.pixelSize: Math.round(13 * root.textScale)
                Keys.onReturnPressed: root.submit()
                Keys.onEnterPressed: root.submit()
            }
        }

        Label {
            width: parent.width
            visible: root.error.length > 0
            text: root.error
            color: "#d9534f"
            wrapMode: Text.Wrap
            font.family: "iA Writer Mono S"
            font.pixelSize: Math.round(12 * root.textScale)
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
                text: "Cancel"
                darkMode: root.darkMode
                textScale: root.textScale
                labelColor: root.textColor
                onClicked: root.reject()
            }

            SquareDialogButton {
                text: "Rename"
                primary: true
                darkMode: root.darkMode
                textScale: root.textScale
                activeColor: root.activeButtonColor
                onClicked: root.submit()
            }
        }
    }
}
