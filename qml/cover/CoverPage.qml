import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    id: cover
    readonly property real coverScale: (size === Cover.Small ? Theme.coverSizeSmall.width : Theme.coverSizeLarge.width) / width
    readonly property real actionSize: 1.5 * Theme.iconSizeSmall / coverScale
    readonly property real actionOffset: Theme.paddingSmall / coverScale

    Image { anchors.fill: parent; source: core.artwork; fillMode: Image.PreserveAspectCrop; asynchronous: true }
    Label {
        x: Theme.paddingMedium
        width: parent.width - 2 * x
        anchors.bottom: parent.bottom
        anchors.bottomMargin: coverActionRow.height + Theme.paddingMedium
        text: core.state.song ? core.state.song.title : "Mconnect"
        color: core.coverInk
        style: Text.Outline
        styleColor: core.coverInk === "#ffffff" ? "#60000000" : "#60ffffff"
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
    }
    Item {
        id: coverActionRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.min(Theme.itemSizeSmall / cover.coverScale, width / 2)
        visible: coverActions.enabled
        Image {
            id: toggleIcon
            width: cover.actionSize
            height: width
            anchors.verticalCenter: parent.verticalCenter
            x: parent.width / 4 - width / 2 - cover.actionOffset
            source: Qt.resolvedUrl("icons/cover-" + (core.state.playing ? "pause" : "play") + ".png")
        }
        Image {
            id: nextIcon
            width: cover.actionSize
            height: width
            anchors.verticalCenter: parent.verticalCenter
            x: 3 * parent.width / 4 - width / 2 + cover.actionOffset
            source: Qt.resolvedUrl("icons/cover-next-song.png")
        }
    }
    CoverActionList {
        id: coverActions
        enabled: core.ready && !!core.state.song
        iconBackground: false
        CoverAction {
            iconSource: Qt.resolvedUrl("icons/cover-action-transparent.png")
            onTriggered: core.request("toggle", {})
        }
        CoverAction {
            iconSource: Qt.resolvedUrl("icons/cover-action-transparent.png")
            onTriggered: core.request("next", {})
        }
    }
}
