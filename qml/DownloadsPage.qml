import QtQuick 2.0
import Sailfish.Silica 1.0

Item {
    id: page
    objectName: "downloadsRoot"
    property bool initialized: false
    property alias flickableItem: list
    property bool embedded: false
    property bool active: true
    property int status: active ? PageStatus.Active : PageStatus.Inactive
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    property var snapshot: ({tasks:[],paused:false,blocked:""})
    property int pending: 0
    property bool acting: false
    property string errorText: ""
    property int filterIndex: 0
    ListModel { id: downloadRows; objectName: "downloadTaskModel" }
    RemorsePopup { id: cancelRemorse }
    function applySnapshot(value) {
        var tasks=value.tasks.filter(function(task){return page.filterIndex===0 || (page.filterIndex===2 ? task.status==="completed" : task.status!=="completed" && task.status!=="failed")})
        for (var i = 0; i < tasks.length; ++i) {
            var task = tasks[i]
            var encoded = JSON.stringify(task)
            if (i >= downloadRows.count || downloadRows.get(i).taskId !== task.id) {
                var found = -1
                for (var j = i + 1; j < downloadRows.count; ++j) {
                    if (downloadRows.get(j).taskId === task.id) { found = j; break }
                }
                if (found >= 0) downloadRows.move(found,i,1)
                else downloadRows.insert(i,{taskId:task.id,rowJson:encoded})
            }
            if (downloadRows.get(i).rowJson !== encoded) downloadRows.setProperty(i,"rowJson",encoded)
        }
        if (downloadRows.count > tasks.length) downloadRows.remove(tasks.length,downloadRows.count-tasks.length)
        snapshot = value
    }
    function load() {
        if (!initialized || !core.ready || !active || pending || acting) return
        pending = app.call("downloads", {}, function(value,error) {
            pending = 0
            if (error.length) errorText = error
            else applySnapshot(value)
        })
    }
    function control(action,id) {
        if (acting) return
        acting = true; errorText = ""
        app.call("downloadControl", {action:action,id:id || ""}, function(value,error) {
            if (!page) return
            acting = false; errorText = error; load()
        })
    }
    Component.onCompleted: {initialized=true;load()}
    Component.onDestruction: { if (pending) delete app.callbacks[pending] }
    onStatusChanged: { if (status === PageStatus.Active) load() }
    Timer { interval: 1500; repeat: true; running: page.status === PageStatus.Active && Qt.application.active && core.ready; onTriggered: page.load() }
    SilicaListView {
        id: list; anchors.fill: parent; anchors.bottomMargin: mini.visible ? mini.height : 0
        model: downloadRows

        header: Column {
            width: parent.width
            AppHeader { visible:!page.embedded; title: "下载"; description: page.snapshot.tasks.length+" 个任务"+(page.snapshot.paused?" · 队列已暂停":"");tools:true }
            PlatformTabs {visible:!page.embedded;width:parent.width;names:["全部","下载中","已完成"];currentIndex:page.filterIndex;onSelected:{if(page.embedded)app.selectSlide(index);else{page.filterIndex=index;page.applySnapshot(page.snapshot)}}}
            Label { x: Theme.horizontalPageMargin; width: parent.width - 2*x; text: page.errorText || page.snapshot.blocked; visible: !!text.length; wrapMode: Text.Wrap; color: Theme.highlightColor }
            BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: page.acting; visible: running }
        }
        delegate: ListItem {
            id: row
            property var record: JSON.parse(rowJson)
            width: list.width; contentHeight: info.height + 2 * Theme.paddingMedium
            Column {
                id: info; x: Theme.horizontalPageMargin; width: parent.width - 2*x; anchors.verticalCenter: parent.verticalCenter
                Label { width: parent.width; text: row.record.title; truncationMode: TruncationMode.Fade }
                Label { width: parent.width; text: row.record.artist + " · " + row.record.quality; color: Theme.secondaryColor; font.pixelSize: Theme.fontSizeSmall; truncationMode: TruncationMode.Fade }
                Label { width: parent.width; text: ({waiting:"等待下载",downloading:"正在下载",paused:"已暂停",completed:"已完成",failed:"下载失败"})[row.record.status] + " · " + Math.round(row.record.progress * 100) + "%" + (row.record.cache ? " · 离线缓存" : ""); font.pixelSize: Theme.fontSizeExtraSmall; color: Theme.secondaryHighlightColor }
                ProgressBar { width: parent.width; minimumValue: 0; maximumValue: 1; value: row.record.progress; visible: row.record.status === "downloading" }
                Label { width: parent.width; visible: row.record.status === "failed"; text: ({network:"网络连接失败",auth:"登录已过期，请重新登录",vip:"当前账号不支持此音质",disk:"存储空间不足",notFound:"内容不存在",unsupported:"该内容暂不支持下载",storagePermission:"无法写入下载目录"})[row.record.failure] || "下载失败，请重试"; wrapMode: Text.Wrap; color: Theme.highlightColor; font.pixelSize: Theme.fontSizeSmall }
            }
            onClicked: {
                if (row.record.status === "completed") {
                    app.errorText = ""
                    app.call("playDownload", {id:row.record.id})
                    app.openPlayer()
                } else showMenu()
            }
            menu: ContextMenu {
                MenuItem { text: "暂停"; visible: row.record.status === "downloading"; onClicked: page.control("pause",row.record.id) }
                MenuItem { text: row.record.status === "failed" ? "重试" : "继续"; visible: row.record.status === "paused" || row.record.status === "failed"; onClicked: page.control("resume",row.record.id) }
                MenuItem { text: "取消任务"; visible: row.record.status !== "completed"; onClicked: { var id = row.record.id; cancelRemorse.execute("正在取消下载",function(){ if (page) page.control("cancel",id) }) } }
            }
        }
        ViewPlaceholder { enabled: !page.pending && !list.count; text: "暂无下载"; hintText: "可在歌曲长按菜单或播放页中下载" }
        VerticalScrollDecorator {}
    }
    AppFooter { id: mini;visible:!page.embedded && !Qt.inputMethod.visible; anchors.bottom: parent.bottom }
}
