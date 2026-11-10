import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    objectName: "queuePage"
    SilicaListView {
        id: list
        anchors.fill: parent; anchors.bottomMargin: mini.visible ? mini.height : 0
        model: core.queue
        header: AppHeader { title: "播放队列" }
        delegate: ListItem {
            width: list.width
            contentHeight: Theme.itemSizeMedium
            highlighted: index === core.state.index
            Column {
                x: Theme.horizontalPageMargin; width: parent.width - 2 * x
                anchors.verticalCenter: parent.verticalCenter
                Label { width: parent.width; text: modelData.title; truncationMode: TruncationMode.Fade; color: index === core.state.index ? Theme.highlightColor : Theme.primaryColor }
                Label { width: parent.width; text: modelData.artist; truncationMode: TruncationMode.Fade; font.pixelSize: Theme.fontSizeSmall; color: Theme.secondaryColor }
            }
            onClicked: app.call("playIndex", {index: index})
        }
        ViewPlaceholder { enabled: list.count === 0; text: "播放队列为空" }
        VerticalScrollDecorator {}
    }
    AppFooter {id:mini;anchors.bottom:parent.bottom}
}
