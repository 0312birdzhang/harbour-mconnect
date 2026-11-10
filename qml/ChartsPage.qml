import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    property string platform: "netease"
    property string platformName: "网易云音乐"
    property var rows: []
    property int pending: 0
    property string errorText: ""
    function load() {
        if (!core.ready || pending) return
        errorText = ""
        pending = app.call("discoveryCharts", {platform:platform}, function(value,error) {
            pending = 0; errorText = error
            if (!error.length) rows = value
        })
    }
    Component.onCompleted: load()
    Component.onDestruction: { if (pending) delete app.callbacks[pending] }
    SilicaListView {
        id: list; anchors.fill: parent; anchors.bottomMargin: mini.visible ? mini.height : 0
        model: page.rows
        PullDownMenu { MenuItem { text: "刷新"; enabled: !page.pending; onClicked: page.load() } }
        header: Column {
            width: parent.width
            AppHeader { title: page.platformName + "榜单" }
            BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: !!page.pending; visible: running }
            Label { x: Theme.horizontalPageMargin; width: parent.width - 2*x; text: page.errorText; visible: text.length > 0; wrapMode: Text.Wrap; color: Theme.highlightColor }
        }
        delegate: ListItem {
            width: list.width; contentHeight: Theme.itemSizeMedium
            Column {
                x: Theme.horizontalPageMargin; width: parent.width - 2*x; anchors.verticalCenter: parent.verticalCenter
                Label { width: parent.width; text: modelData.title; truncationMode: TruncationMode.Fade }
                Label { width: parent.width; text: modelData.group + " " + modelData.description; color: Theme.secondaryColor; font.pixelSize: Theme.fontSizeSmall; truncationMode: TruncationMode.Fade }
            }
            onClicked: pageStack.push(Qt.resolvedUrl("LibrarySongsPage.qml"), {pageTitle:modelData.title,requestMethod:"discoverySongs",requestArgs:{kind:"chart",platform:page.platform,id:modelData.id,period:modelData.period}})
        }
        ViewPlaceholder { enabled: !page.pending && !page.errorText.length && !list.count; text: "暂无榜单" }
        VerticalScrollDecorator {}
    }
    AppFooter { id: mini; anchors.bottom: parent.bottom }
}
