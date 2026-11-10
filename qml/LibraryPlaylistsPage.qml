import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    property string source: "mine"
    property string sourceName: "我的歌单"
    property var rows: []
    property bool loading: false
    property string errorText: ""
    property int pending: 0
    RemorsePopup { id: deleteRemorse }
    function edit(action,id,name) {
        app.call("playlistEdit",{action:action,id:id,name:name || ""},function(value,error) {
            if (!page) return
            if (error.length) errorText=error
            else reload()
        })
    }
    function reload() {
        if (!core.ready || loading) return
        loading = true; errorText = ""
        pending = app.call("libraryPlaylists", {source:source}, function(value,error) {
            pending = 0; loading = false
            if (error.length) { errorText = error; return }
            rows = value
        })
    }
    Component.onCompleted: reload()
    onStatusChanged: { if (status === PageStatus.Active) reload() }
    Component.onDestruction: { if (pending) delete app.callbacks[pending] }
    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: mini.visible ? mini.height : 0
        model: page.rows
        PullDownMenu {
            MenuItem { text: "导入歌单链接"; visible: page.source === "mine"; onClicked: pageStack.push(Qt.resolvedUrl("PlaylistTransferPage.qml")) }
            MenuItem {
                text: "新建歌单"
                visible: page.source === "mine"
                onClicked: {
                    var dialog = pageStack.push(Qt.resolvedUrl("CreatePlaylistDialog.qml"))
                    dialog.accepted.connect(function() {
                        app.call("createPlaylist", {name:dialog.playlistName}, function(value,error) {
                            if (!page) return
                            if (error.length) page.errorText = error
                            else page.reload()
                        })
                    })
                }
            }
            MenuItem { text: "刷新"; enabled: !page.loading; onClicked: page.reload() }
        }
        header: Column {
            width: parent.width
            AppHeader { title: page.sourceName }
            BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: page.loading; visible: running }
            Label { x: Theme.horizontalPageMargin; width: parent.width - 2*x; wrapMode: Text.Wrap; text: page.errorText; visible: text.length > 0; color: Theme.highlightColor }
        }
        delegate: ListItem {
            width: list.width
            contentHeight: Theme.itemSizeMedium
            Column {
                x: Theme.horizontalPageMargin; width: parent.width - 2*x; anchors.verticalCenter: parent.verticalCenter
                Label { width: parent.width; text: modelData.title; truncationMode: TruncationMode.Fade }
                Label { width: parent.width; text: modelData.count + " 首" + (modelData.creator.length ? " · " + modelData.creator : ""); color: Theme.secondaryColor; font.pixelSize: Theme.fontSizeSmall; truncationMode: TruncationMode.Fade }
            }
            menu: ContextMenu {
                MenuItem {
                    text: "重命名"; visible: page.source === "mine"
                    onClicked: {
                        var id=modelData.id
                        var dialog=pageStack.push(Qt.resolvedUrl("CreatePlaylistDialog.qml"),{dialogTitle:"重命名歌单",initialName:modelData.title})
                        dialog.accepted.connect(function(){ if(page)page.edit("rename",id,dialog.playlistName) })
                    }
                }
                MenuItem { text: "复制歌单"; visible: page.source === "mine"; onClicked: page.edit("duplicate",modelData.id,"") }
                MenuItem { text: "导出链接"; visible: page.source === "mine"; onClicked: pageStack.push(Qt.resolvedUrl("PlaylistTransferPage.qml"),{importMode:false,playlistId:modelData.id,playlistName:modelData.title}) }
                MenuItem { text: "删除歌单"; visible: page.source === "mine"; onClicked: { var id=modelData.id; deleteRemorse.execute("正在删除歌单",function(){if(page)page.edit("delete",id,"")}) } }
                MenuItem {
                    text: "添加当前歌曲"
                    visible: page.source === "mine"
                    enabled: !!core.state.song
                    onClicked: app.call("addCurrentToPlaylist", {id:modelData.id}, function(value,error) {
                        if (!page) return
                        if (error.length) page.errorText = error
                        else page.reload()
                    })
                }
            }
            onClicked: pageStack.push(Qt.resolvedUrl("LibrarySongsPage.qml"), {pageTitle:modelData.title,requestArgs:{source:page.source === "mine" ? "mine" : "platform",platform:page.source,id:modelData.id}})
        }
        ViewPlaceholder { enabled: !page.loading && !page.errorText.length && list.count === 0; text: "暂无歌单"; hintText: page.source === "mine" ? "下拉菜单新建歌单" : "可下拉刷新" }
        VerticalScrollDecorator {}
    }
    AppFooter { id: mini; anchors.bottom: parent.bottom }
}
