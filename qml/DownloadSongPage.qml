import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    objectName: "downloadSongPage"
    property bool current: false
    property string token: ""
    property int songIndex: 0
    property string songTitle: "歌曲"
    property bool busy: false
    property bool enqueued: false
    property string errorText: ""
    property int pending: 0
    function start() {
        if (busy || enqueued || !core.ready) return
        busy = true; errorText = ""
        pending = app.call("downloadEnqueue", {current:current,token:token,index:songIndex,quality:["low","medium","high","lossless","hires","spatial","dolby","master"][quality.currentIndex]}, function(value,error) {
            pending = 0; busy = false
            if (error.length) errorText = error
            else enqueued = true
        })
    }
    Component.onDestruction: { if (pending) app.callbacks[pending] = function() {} }
    Timer {
        interval: 50
        repeat: true
        running: page.enqueued && page.status === PageStatus.Active
        onTriggered: { if (pageStack.currentPage === page) app.openDownloads() }
    }
    SilicaFlickable {
        anchors.fill: parent; anchors.bottomMargin: mini.visible ? mini.height : 0; contentHeight: column.height
        Column {
            id: column; width: parent.width; spacing: Theme.paddingLarge
            AppHeader { title: "下载歌曲"; description: page.songTitle }
            ComboBox {
                id: quality; width: parent.width; label: "音质"; enabled: !page.busy
                menu: ContextMenu {
                    MenuItem { text: "标准" } MenuItem { text: "较高" } MenuItem { text: "极高" } MenuItem { text: "无损" }
                    MenuItem { text: "Hi-Res" } MenuItem { text: "环绕声" } MenuItem { text: "沉浸环绕声" } MenuItem { text: "超清母带" }
                }
            }
            Label { x: Theme.horizontalPageMargin; width: parent.width - 2*x; text: "可用音质取决于平台、歌曲和账号权限。"; wrapMode: Text.Wrap; color: Theme.secondaryColor; font.pixelSize: Theme.fontSizeSmall }
            Label { x: Theme.horizontalPageMargin; width: parent.width - 2*x; text: page.errorText; visible: text.length > 0; wrapMode: Text.Wrap; color: Theme.highlightColor }
            BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: page.busy; visible: running }
            Button { objectName: "startDownloadButton"; anchors.horizontalCenter: parent.horizontalCenter; text: "开始下载"; enabled: core.ready && !page.busy && !page.enqueued; onClicked: page.start() }
        }
        VerticalScrollDecorator {}
    }
    AppFooter {id:mini;anchors.bottom:parent.bottom}
}
