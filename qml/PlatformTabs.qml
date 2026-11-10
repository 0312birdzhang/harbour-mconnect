import QtQuick 2.0
import Sailfish.Silica 1.0

Row {
    id: tabs
    property int currentIndex: 0
    property var names: ["网易云", "QQ音乐", "酷狗", "哔哩哔哩"]
    signal selected(int index)
    height: Theme.itemSizeSmall
    Repeater {
        model: tabs.names
        BackgroundItem {
            width: tabs.width / tabs.names.length
            height: tabs.height
            Label { anchors.centerIn: parent; text: modelData; font.pixelSize: Theme.fontSizeSmall; color: index === tabs.currentIndex ? Theme.highlightColor : Theme.secondaryColor }
            Rectangle { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; width: parent.width * 0.55; height: 2; radius: 1; color: Theme.highlightColor; visible: index === tabs.currentIndex }
            onClicked: tabs.selected(index)
        }
    }
}
