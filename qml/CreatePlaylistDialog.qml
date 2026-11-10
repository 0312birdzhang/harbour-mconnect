import QtQuick 2.0
import Sailfish.Silica 1.0

Dialog {
    id: dialog
    property string dialogTitle: "新建歌单"
    property string initialName: ""
    property alias playlistName: name.text
    Component.onCompleted: name.text = initialName
    onStatusChanged: { if (status === PageStatus.Active && !validationMode) focusTimer.restart() }
    Timer { id: focusTimer; interval: 120; onTriggered: { if (dialog.status === PageStatus.Active) { name.forceActiveFocus(); Qt.inputMethod.show() } } }
    canAccept: name.text.trim().length > 0
    Column {
        width: parent.width
        DialogHeader { title: dialog.dialogTitle }
        TextField {
            id: name
            width: parent.width
            label: "歌单名称"
            placeholderText: "输入歌单名称"
            maximumLength: 200
            EnterKey.enabled: text.trim().length > 0
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.onClicked: accept()
        }
    }
}
