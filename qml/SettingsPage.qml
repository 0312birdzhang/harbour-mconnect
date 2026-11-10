import QtQuick 2.0
import Sailfish.Silica 1.0
import Sailfish.Pickers 1.0

Page {
    id: page
    objectName: "settingsPage"
    property var options: ({})
    property string downloadDirectory: ""
    property int pending: 0
    property bool loaded: false
    property string errorText: ""
    function load() {
        if (!core.ready || pending) return
        pending = app.call("cacheSettings", {}, function(value,error) {
            pending = 0; errorText = error
            if (!error.length) {
                options = value
                loaded = true
                loadDownloadDirectory()
            }
        })
    }
    function loadDownloadDirectory() {
        pending = app.call("downloadDirectory", {}, function(value, error) {
            pending = 0
            errorText = error
            if (!error.length) downloadDirectory = value.path
        })
    }
    function chooseDownloadDirectory() {
        if (!loaded || pending || !core.ready || pageStack.busy) return
        pageStack.push(directoryPicker)
    }
    function saveDownloadDirectory(path) {
        if (!loaded || pending || !core.ready) return
        pending = app.call("setDownloadDirectory", {path: path}, function(value, error) {
            pending = 0
            errorText = error
            if (!error.length) downloadDirectory = value.path
        })
    }
    function save(key,value) {
        if (!loaded || pending) return
        pending = app.call("setCacheSetting", {key:key,value:value}, function(value,error) {
            pending = 0; errorText = error
            if (!error.length) options = value
        })
    }
    Component.onCompleted: load()
    Component.onDestruction: { if (pending) delete app.callbacks[pending] }
    Connections { target: core; onReadyChanged: { if (core.ready) page.load() } }
    Component {
        id: directoryPicker
        FolderPickerPage {
            objectName: "downloadFolderPicker"
            title: "选择下载位置"
            dialogTitle: "选择文件夹"
            onSelectedPathChanged: {
                if (selectedPath.length > 0) page.saveDownloadDirectory(selectedPath)
            }
        }
    }
    SilicaFlickable {
        anchors.fill: parent; anchors.bottomMargin: mini.visible ? mini.height : 0; contentHeight: column.height
        Column {
            id: column; width: parent.width
            AppHeader { title: "设置" }
            Label { x: Theme.horizontalPageMargin; width: parent.width - 2*x; text: page.errorText; visible: text.length > 0; wrapMode: Text.Wrap; color: Theme.highlightColor }
            SectionHeader { text: "下载" }
            ValueButton {
                objectName: "downloadDirectoryEntry"
                width: parent.width
                label: "下载目录"
                value: page.downloadDirectory || "选择文件夹"
                enabled: page.loaded && !page.pending && core.ready
                onClicked: page.chooseDownloadDirectory()
            }
            SectionHeader { text: "播放模式" }
            TextSwitch { text: "随机播放"; checked: !!core.state.shuffle; automaticCheck: false; enabled: core.ready; onClicked: app.call("toggleShuffle") }
            Button { anchors.horizontalCenter: parent.horizontalCenter; text: "循环模式：" + ({off:"顺序播放",all:"列表循环",one:"单曲循环"})[core.state.repeat || "off"]; enabled: core.ready; onClicked: app.call("cycleRepeat") }
            SectionHeader { text: "离线缓存" }
            TextSwitch { text: "离线模式"; description: "优先本地文件，不自动执行联网缓存任务"; checked: page.options.offlineMode === true; automaticCheck: false; enabled: page.loaded && !page.pending; onClicked: page.save("offlineMode",!checked) }
            TextSwitch { text: "仅 Wi-Fi 自动缓存"; description: "手动下载不受此开关限制"; checked: page.options.wifiOnly === true; automaticCheck: false; enabled: page.loaded && !page.pending; onClicked: page.save("wifiOnly",!checked) }
            TextSwitch { text: "失败自动重试"; checked: page.options.autoRetry === true; automaticCheck: false; enabled: page.loaded && !page.pending; onClicked: page.save("autoRetry",!checked) }
            TextSwitch { text: "超额自动清理离线缓存"; description: "保留手动下载的歌曲"; checked: page.options.autoCleanup === true; automaticCheck: false; enabled: page.loaded && !page.pending; onClicked: page.save("autoCleanup",!checked) }
            ComboBox {
                width: parent.width; label: "缓存上限"; enabled: page.loaded && !page.pending
                currentIndex: [128,256,512,1024,2048,4096,8192,16384,32768].indexOf(page.options.sizeLimitMb || 1024)
                menu: ContextMenu {
                    Repeater { model: [128,256,512,1024,2048,4096,8192,16384,32768]; MenuItem { text: modelData + " MB"; onClicked: page.save("sizeLimitMb",modelData) } }
                }
            }
            Label { x: Theme.horizontalPageMargin; width: parent.width - 2*x; visible: page.loaded && [128,256,512,1024,2048,4096,8192,16384,32768].indexOf(page.options.sizeLimitMb) < 0; text: "当前缓存上限：" + page.options.sizeLimitMb + " MB"; color: Theme.secondaryColor; font.pixelSize: Theme.fontSizeSmall }
            Label { x: Theme.horizontalPageMargin; width: parent.width - 2*x; text: "海报和歌词优先使用已有本地缓存。海报采用私有格式保存。"; wrapMode: Text.Wrap; color: Theme.secondaryColor; font.pixelSize: Theme.fontSizeSmall }
            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
    AppFooter { id: mini; anchors.bottom: parent.bottom }
}
