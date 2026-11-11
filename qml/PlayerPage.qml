import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    objectName: "playerPage"
    property bool showLyrics: false
    property bool modeBusy: false
    property string playbackMode: core.state.shuffle ? "shuffle" : core.state.repeat === "one" ? "one" : "all"
    function normalizeRepeat() {
        if (core.ready && !modeBusy && core.state.repeat === "off") {
            modeBusy = true
            app.call("cycleRepeat", {}, function(value, error) {
                modeBusy = false
                if (error) app.errorText = error
            })
        }
    }
    Component.onCompleted: normalizeRepeat()
    function cyclePlaybackMode() {
        if (modeBusy) return
        var target = playbackMode === "all" ? "one" : playbackMode === "one" ? "shuffle" : "all"
        var repeats = ["off", "all", "one"]
        var count = (repeats.indexOf(target === "one" ? "one" : "all") - repeats.indexOf(core.state.repeat || "off") + 3) % 3
        var commands = []
        if (!!core.state.shuffle !== (target === "shuffle")) commands.push("toggleShuffle")
        for (var i = 0; i < count; ++i) commands.push("cycleRepeat")
        modeBusy = true
        function run() {
            if (!commands.length) { modeBusy = false; return }
            app.call(commands.shift(), {}, function(value, error) {
                if (error) { app.errorText = error; modeBusy = false; return }
                run()
            })
        }
        run()
    }
    property var lines: core.lyrics.lines || []
    property int currentLine: -1
    function updateLine() {
        var current = -1
        for (var i = 0; i < lines.length; ++i) {
            if (lines[i].time > (core.state.position || 0)) break
            current = i
        }
        if (current !== currentLine) {
            currentLine = current
            if (showLyrics && current >= 0 && !lyrics.moving) lyrics.positionViewAtIndex(current, ListView.Center)
        }
    }
    Connections { target: core; onStateChanged: { page.updateLine(); page.normalizeRepeat() } onReadyChanged: page.normalizeRepeat(); onLyricsChanged: page.updateLine() }
    onShowLyricsChanged: { if(showLyrics)lyricsTimer.restart() }
    Timer {id:lyricsTimer;interval:10;onTriggered:{page.updateLine();if(page.currentLine>=0)lyrics.positionViewAtIndex(page.currentLine,ListView.Center)}}
    SilicaFlickable {
        id: playerScroll
        objectName: "playerPageFlickable"
        anchors.fill: parent
        contentHeight: height
        clip: true
        PullDownMenu {
            id: playerMenu
            objectName: "playerMenu"
            width: playerScroll.width
            enabled: page.status === PageStatus.Active
            MenuItem { text: "歌曲信息与收藏"; enabled: !!core.state.song; onClicked: pageStack.push(Qt.resolvedUrl("SongActionsPage.qml"), {current:true}) }
            MenuItem { objectName: "playerDownloadAction"; text: "下载当前歌曲"; enabled: !!core.state.song; onClicked: pageStack.push(Qt.resolvedUrl("DownloadSongPage.qml"), {current:true,songTitle:core.state.song.title}) }
            MenuItem { text: "播放与缓存设置"; onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml")) }
            MenuItem { text: "播放队列"; onClicked: app.openQueue() }
            MenuItem { text: page.showLyrics ? "显示封面" : "显示歌词"; onClicked: page.showLyrics = !page.showLyrics }
        }
        Item {
            width: playerScroll.width
            height: playerScroll.height
            SilicaListView {
                id: lyrics
                objectName: "playerLyrics"
                anchors.top: header.bottom
                anchors.bottom: page.isLandscape ? parent.bottom : controlPane.top
                anchors.bottomMargin: page.isLandscape ? Theme.paddingLarge : 0
                width: page.isLandscape ? parent.width / 2 : parent.width
                clip: true
                model: page.showLyrics ? page.lines : []
                spacing: Theme.paddingMedium
                delegate: BackgroundItem {
                    width: lyrics.width
                    height: lineColumn.height + Theme.paddingMedium * 2
                    Column {
                        id: lineColumn
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        anchors.verticalCenter: parent.verticalCenter
                        Label { width: parent.width; text: modelData.text; textFormat: Text.PlainText; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; color: index === page.currentLine ? Theme.highlightColor : Theme.primaryColor; font.pixelSize: index === page.currentLine ? Theme.fontSizeLarge : Theme.fontSizeMedium }
                        Label { width: parent.width; text: modelData.translation; visible: text.length > 0; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; color: Theme.secondaryColor; font.pixelSize: Theme.fontSizeSmall }
                    }
                    onClicked: { if (index === page.currentLine) page.showLyrics = false; else app.call("seek", {position: modelData.time}) }
                }
                ViewPlaceholder {
                    flickable: lyrics
                    parent: lyrics.contentItem
                    width: lyrics.width
                    x: 0
                    y: lyrics.originY + Math.max(0, (lyrics.height - height) / 2)
                    enabled: page.showLyrics && page.lines.length === 0
                    text: core.lyrics.loading ? "正在加载歌词…" : core.lyrics.error ? "歌词加载失败" : "暂无歌词"
                }
                VerticalScrollDecorator {}
            }
            MouseArea {
                anchors.top: lyrics.top; anchors.bottom: lyrics.bottom
                anchors.left: lyrics.left; width: Theme.horizontalPageMargin
                visible: page.showLyrics
                onClicked: page.showLyrics = false
            }
            MouseArea {
                anchors.top: lyrics.top; anchors.bottom: lyrics.bottom
                anchors.right: lyrics.right; width: Theme.horizontalPageMargin
                visible: page.showLyrics
                onClicked: page.showLyrics = false
            }
            AppHeader {
                id: header
                title: core.state.song ? core.state.song.title : "正在播放"
                description: core.state.song ? core.state.song.artist : "尚未选择歌曲"
            }
            Image {
                objectName: "playerArtwork"
                visible: !page.showLyrics
                anchors.centerIn: lyrics
                width: Math.max(0, Math.min(lyrics.width - 2 * Theme.horizontalPageMargin, lyrics.height))
                height: width
                source: core.artwork
                asynchronous: true
                fillMode: Image.PreserveAspectFit
                MouseArea {anchors.fill:parent;onClicked:page.showLyrics=true}
            }
            SilicaFlickable {
                id: controlPane
                objectName: "playerControlPane"
                x: page.isLandscape ? parent.width - width : 0
                width: page.isLandscape ? parent.width / 2 : parent.width
                anchors.bottom: parent.bottom
                height: page.isLandscape ? Math.max(0, page.height - header.height)
                                        : Math.min(contentHeight, Math.max(0, page.height - header.height - Theme.itemSizeMedium))
                contentHeight: controls.height
                flickableDirection: Flickable.VerticalFlick
                clip: true
                Column {
                    id: controls
                    objectName: "playerControls"
                    width: controlPane.width
                    y: page.isLandscape ? Math.max(0, (controlPane.height - height) / 2) : 0
                    Label { x: Theme.horizontalPageMargin; width: parent.width - 2 * x; text: app.errorText || core.state.error || ""; visible: text.length > 0; wrapMode: Text.Wrap; color: Theme.highlightColor; font.pixelSize: Theme.fontSizeSmall }
                    Slider {
                        objectName: "playbackProgress"
                        width: parent.width
                        minimumValue: 0
                        maximumValue: Math.max(1, core.state.duration || 0)
                        value: core.state.position || 0
                        enabled: core.ready && !!core.state.song
                        valueText: app.time(value) + " / " + app.time(core.state.duration)
                        onReleased: {
                            app.call("seek", {position: Math.round(value)})
                            // Silica assigns value during dragging and release, removing its binding.
                            value = Qt.binding(function() { return core.state.position || 0 })
                        }
                        onCanceled: value = Qt.binding(function() { return core.state.position || 0 })
                    }
                    Row {
                        objectName: "playbackButtons"
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Math.max(0,(controls.width-4*Theme.itemSizeSmall-2*Theme.horizontalPageMargin)/3)
                        IconButton {
                            objectName: "playbackModeButton"
                            width: Theme.itemSizeSmall
                            icon.source: "image://theme/icon-m-" + (page.playbackMode === "shuffle" ? "shuffle" : page.playbackMode === "one" ? "repeat-single" : "repeat")
                            icon.color: Theme.highlightColor
                            enabled: core.ready && !page.modeBusy
                            onClicked: page.cyclePlaybackMode()
                        }
                        IconButton {width:Theme.itemSizeSmall; icon.source: "image://theme/icon-m-previous"; enabled: core.ready && !!core.state.song; onClicked: app.call("previous") }
                        IconButton {width:Theme.itemSizeSmall; icon.source: "image://theme/icon-m-" + (core.state.playing ? "pause" : "play"); enabled: core.ready && !!core.state.song && !core.state.busy; onClicked: app.call("toggle") }
                        IconButton {width:Theme.itemSizeSmall; icon.source: "image://theme/icon-m-next"; enabled: core.ready && !!core.state.song; onClicked: app.call("next") }
                    }
                    Row {
                        width:parent.width;height:Theme.itemSizeSmall
                        Repeater {
                            model:["歌曲信息","播放队列"]
                            BackgroundItem{width:controls.width/2;height:parent.height
                                Label{anchors.centerIn:parent;text:modelData;color:Theme.secondaryColor;font.pixelSize:Theme.fontSizeSmall}
                                onClicked:{if(index===0)pageStack.push(Qt.resolvedUrl("SongActionsPage.qml"),{current:true});else app.openQueue()}
                            }
                        }
                    }
                    Item { width: 1; height: Theme.paddingLarge }
                }
                VerticalScrollDecorator {}
            }
        }
    }
}
