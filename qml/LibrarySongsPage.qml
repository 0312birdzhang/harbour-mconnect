import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    property string pageTitle: "歌曲"
    property var requestArgs: ({source:"likes"})
    property string requestMethod: "librarySongs"
    property string sourceLabel: ""
    property string sourceNote: ""
    property var rows: []
    property string token: ""
    property int total: 0
    property bool loading: false
    property string errorText: ""
    property int pending: 0
    property bool editable: requestMethod === "librarySongs" && requestArgs.source === "mine"
    RemorsePopup { id: removeRemorse }
    function editSong(action,index,direction,reference) {
        app.call("playlistEdit",{action:action,id:requestArgs.id,token:reference || token,index:index,direction:direction},function(value,error) {
            if (!page) return
            if (error.length) errorText = error
            else reload(false)
        })
    }
    function copyLocal() {
        var dialog = pageStack.push(Qt.resolvedUrl("CreatePlaylistDialog.qml"), {dialogTitle:"保存为本地歌单",initialName:pageTitle})
        dialog.accepted.connect(function() {
            app.call("copyResultToPlaylist",{name:dialog.playlistName,token:page.token},function(value,error) {
                if (!page) return
                page.errorText = error.length ? error : "已保存到我的歌单"
            })
        })
    }
    function reload(more) {
        if (!core.ready || loading) return
        loading = true; errorText = ""
        pending = app.call(more ? "resultPage" : requestMethod, more ? {token:token,offset:rows.length} : requestArgs, function(value,error) {
            pending = 0; loading = false
            if (error.length) { errorText = error; return }
            token = value.token; total = value.total
            if (!more) { sourceLabel = value.sourceLabel || ""; sourceNote = value.sourceNote || "" }
            rows = more ? rows.concat(value.songs) : value.songs
        })
    }
    function play(index) {
        app.errorText = ""
        app.call("playResult", {token:token,index:index})
        app.openPlayer()
    }
    Component.onCompleted: reload(false)
    onStatusChanged: { if (status === PageStatus.Active && editable) reload(false) }
    Component.onDestruction: { if (pending) delete app.callbacks[pending] }
    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: mini.visible ? mini.height : 0
        model: page.rows
        PullDownMenu {
            MenuItem { text: "保存为本地歌单"; enabled: page.total > 0 && !page.loading; onClicked: page.copyLocal() }
            MenuItem { text: "刷新"; enabled: !page.loading; onClicked: page.reload(false) }
            MenuItem { text: "播放全部"; enabled: !!page.token.length && page.total > 0 && !page.loading; onClicked: page.play(0) }
        }
        header: Column {
            width: parent.width
            AppHeader { title: page.pageTitle; description: page.total + " 首" }
            Label { x: Theme.horizontalPageMargin; width: parent.width - 2*x; text: page.sourceLabel + (page.sourceNote.length ? " · " + page.sourceNote : ""); visible: page.sourceLabel.length > 0; wrapMode: Text.Wrap; color: Theme.secondaryColor; font.pixelSize: Theme.fontSizeSmall }
            Button { anchors.horizontalCenter: parent.horizontalCenter; text: "播放全部"; enabled: page.total > 0 && !page.loading; onClicked: page.play(0) }
            BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: page.loading; visible: running }
            Label { x: Theme.horizontalPageMargin; width: parent.width - 2*x; text: page.errorText; visible: text.length > 0; wrapMode: Text.Wrap; color: Theme.highlightColor }
        }
        delegate: ListItem {
            width: list.width; contentHeight: Theme.itemSizeMedium
            enabled: !page.loading
            Column {
                x: Theme.horizontalPageMargin; width: parent.width - 2*x; anchors.verticalCenter: parent.verticalCenter
                Label { width: parent.width; text: modelData.title; truncationMode: TruncationMode.Fade }
                Label { width: parent.width; text: modelData.artist; truncationMode: TruncationMode.Fade; color: Theme.secondaryColor; font.pixelSize: Theme.fontSizeSmall }
            }
            onClicked: page.play(index)
            menu: ContextMenu {
                MenuItem { text: "歌曲信息与收藏"; onClicked: pageStack.push(Qt.resolvedUrl("SongActionsPage.qml"), {token:page.token,songIndex:index}) }
                MenuItem { text: "置顶"; visible: page.editable; onClicked: page.editSong("move",index,0) }
                MenuItem { text: "上移"; visible: page.editable; onClicked: page.editSong("move",index,-1) }
                MenuItem { text: "下移"; visible: page.editable; onClicked: page.editSong("move",index,1) }
                MenuItem { text: "从歌单移除"; visible: page.editable; onClicked: { var i=index; var reference=page.token; removeRemorse.execute("正在移出歌单",function(){if(page)page.editSong("remove",i,0,reference)}) } }
                MenuItem { text: "下载歌曲"; onClicked: pageStack.push(Qt.resolvedUrl("DownloadSongPage.qml"), {token:page.token,songIndex:index,songTitle:modelData.title}) }
            }
        }
        footer: Button { anchors.horizontalCenter: parent.horizontalCenter; text: "加载更多"; visible: page.rows.length < page.total; enabled: !page.loading; onClicked: page.reload(true) }
        ViewPlaceholder { enabled: !page.loading && !page.errorText.length && list.count === 0; text: "暂无歌曲" }
        VerticalScrollDecorator {}
    }
    AppFooter { id: mini; anchors.bottom: parent.bottom }
}
