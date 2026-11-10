import QtQuick 2.0
import Sailfish.Silica 1.0

BackgroundItem {
    id: mini
    clip: true
    width: parent ? parent.width : 0
    height: Theme.itemSizeMedium
    visible: !!core.state.song
    Rectangle { x: Theme.paddingMedium; width: parent.width - 2*x; height: parent.height - Theme.paddingSmall; radius: Theme.paddingMedium; color: Theme.highlightBackgroundColor; opacity: Theme.highlightBackgroundOpacity }
    Image { id: art; x: 2*Theme.paddingMedium; anchors.verticalCenter: parent.verticalCenter; width: Theme.itemSizeSmall; height: width; source: core.artwork; fillMode: Image.PreserveAspectCrop; asynchronous: true }
    Column {
        anchors.left: art.right; anchors.leftMargin: Theme.paddingMedium
        anchors.right: play.left; anchors.rightMargin: Theme.paddingSmall
        anchors.verticalCenter: parent.verticalCenter
        Label { width: parent.width; text: core.state.song ? core.state.song.title : ""; textFormat: Text.PlainText; truncationMode: TruncationMode.Fade; font.pixelSize: Theme.fontSizeSmall }
        Label { width: parent.width; text: core.state.song ? core.state.song.artist : ""; textFormat: Text.PlainText; truncationMode: TruncationMode.Fade; font.pixelSize: Theme.fontSizeExtraSmall; color: Theme.secondaryColor }
    }
    IconButton { id: play; anchors.right: parent.right; anchors.rightMargin: Theme.paddingMedium; anchors.verticalCenter: parent.verticalCenter; icon.source: "image://theme/icon-m-" + (core.state.playing ? "pause" : "play"); enabled: core.ready && !core.state.busy; onClicked: app.call("toggle") }
    Rectangle { x: Theme.paddingMedium; y: parent.height - Theme.paddingSmall; height: 2; width: (parent.width - 2*x) * Math.max(0,Math.min(1,(core.state.position || 0)/Math.max(1,core.state.duration || 0))); color: Theme.highlightColor }
    onClicked: app.openPlayer()
}
