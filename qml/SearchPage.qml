import QtQuick 2.0
import Sailfish.Silica 1.0

Item {
    id: page
    objectName: "exploreRoot"
    property alias flickableItem: list
    property bool embedded: false
    property bool active: true
    property int status: active ? PageStatus.Active : PageStatus.Inactive
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    property int cachePlatform: 0
    property string queryText: ""
    property int platformIndex: 0
    property Item searchField: null
    property bool autoFocus: false
    property int modeIndex: 0
    property var results: []
    property string token: ""
    property int pendingSearch: 0
    property int generation: 0
    property bool searching: false
    property string searchError: ""
    property string activeQuery: ""
    property int currentPage: 0
    property bool hasMore: false
    readonly property bool isLandscape: app.homePage ? app.homePage.isLandscape : width > height
    onActiveChanged: {if(!active && searchField)searchField.focus=false}
    onStatusChanged: { if (autoFocus && status === PageStatus.Active) focusTimer.restart() }
    Connections { target: core; onReadyChanged: { if (page.autoFocus && core.ready && page.status === PageStatus.Active) focusTimer.restart() } }
    Timer {
        id: focusTimer
        interval: 120
        onTriggered: {
            if (page.status === PageStatus.Active && core.ready) {
                if(page.searchField)page.searchField.forceActiveFocus()
                Qt.inputMethod.show()
            }
        }
    }
    Component.onCompleted: {
        cachePlatform=platformIndex
        var saved=embedded?(app.searchStates[cachePlatform] || {}):app.searchState
        if (saved.query!==undefined) {
            queryText=saved.query;if(!embedded)platformIndex=saved.platform; modeIndex=saved.mode
            results=saved.results;token=saved.token;activeQuery=saved.activeQuery;currentPage=saved.page;hasMore=saved.more
        }
    }
    Component.onDestruction: {
        ++generation
        if (pendingSearch) app.callbacks[pendingSearch] = function() {}
        if (!embedded || !app.homePage || app.homePage.currentPane === page) saveState()
    }
    function saveState() {
        var saved={query:queryText,platform:platformIndex,mode:modeIndex,results:results,token:token,activeQuery:activeQuery,page:currentPage,more:hasMore}
        if(embedded)app.searchStates[cachePlatform]=saved;else app.searchState=saved
    }
    function reset() {
        if (pendingSearch) app.callbacks[pendingSearch] = function() {}
        pendingSearch = 0
        ++generation
        searching = false
        results = []
        token = ""
        activeQuery = ""
        currentPage = 0
        hasMore = false
        searchError = ""
        list.positionViewAtBeginning()
    }
    function selectPlatform(index) {
        if (index < 0 || index > 3 || index === platformIndex) return
        saveState()
        reset()
        cachePlatform = index
        platformIndex = index
        if (queryText.trim().length) search(false)
    }
    function selectMode(index) {
        if (index < 0 || index > 1 || index === modeIndex) return
        modeIndex = index
        reset()
        if (queryText.trim().length) search(false)
    }
    function search(more) {
        if (!core.ready || searching) return
        var query = more ? activeQuery : queryText.trim()
        if (!query.length) return
        var generationId = ++generation
        var next = more ? currentPage + 1 : 1
        searching = true
        searchError = ""
        pendingSearch=app.call(modeIndex===0 ? "search" : "searchPlaylists", {query: query, platform: ["netease", "qq", "kugou", "bilibili"][platformIndex], page: next}, function(value, error) {
            if (generationId !== generation) return
            pendingSearch=0;searching = false
            if (error.length) { searchError = error; return }
            activeQuery = query
            currentPage = next
            if (modeIndex===1) {
                results=more?results.concat(value.playlists):value.playlists
                hasMore=value.playlists.length>=30
                return
            }
            // Keep each page's server token so playback uses the original Song objects.
            var rows = more ? results.slice() : []
            for (var i = 0; i < value.songs.length; ++i) {
                var song = value.songs[i]
                song.token = value.token
                song.resultIndex = i
                rows.push(song)
            }
            results = rows
            hasMore = value.songs.length >= 30
        })
    }
    SilicaListView {
        id: list
        objectName: "searchResults"
        anchors.fill: parent
        anchors.bottomMargin: mini.visible ? mini.height : 0
        clip: true
        model: page.results

        header: Column {
            objectName: "searchControls"
            width: parent.width
            AppHeader { visible:!page.embedded; title: "探索"; tools:true }
            Item {
                id: toolbar
                width: parent.width
                height: page.isLandscape ? Math.max(selectors.height, searchInput.height)
                                         : selectors.height + searchInput.height
                Row {
                    id: selectors
                    objectName: "searchSelectors"
                    width: page.isLandscape ? toolbar.width / 2 : toolbar.width
                    ComboBox {
                        id: platform
                        objectName: "searchPlatform"
                        width: selectors.width / 2
                        label: "平台"
                        currentIndex: page.platformIndex
                        menu: ContextMenu {
                            MenuItem { text: "网易云音乐" }
                            MenuItem { text: "QQ音乐" }
                            MenuItem { text: "酷狗音乐" }
                            MenuItem { text: "哔哩哔哩" }
                        }
                        onCurrentIndexChanged: page.selectPlatform(currentIndex)
                        Connections {
                            target: page
                            onPlatformIndexChanged: platform.currentIndex = page.platformIndex
                        }
                    }
                    ComboBox {
                        id: mode
                        objectName: "searchMode"
                        width: selectors.width / 2
                        label: "类型"
                        currentIndex: page.modeIndex
                        menu: ContextMenu {
                            MenuItem { text: "歌曲" }
                            MenuItem { text: "歌单" }
                        }
                        onCurrentIndexChanged: page.selectMode(currentIndex)
                        Connections {
                            target: page
                            onModeIndexChanged: mode.currentIndex = page.modeIndex
                        }
                    }
                }
                Item {
                    id: searchInput
                    x: page.isLandscape ? selectors.width : 0
                    y: page.isLandscape ? 0 : selectors.height
                    width: toolbar.width - x
                    height: page.isLandscape ? Math.max(field.height, searchButton.height)
                                             : field.height + searchButton.height
                    SearchField {
                        id: field
                        objectName: "searchField"
                        text:page.queryText
                        onTextChanged:page.queryText=text
                        Component.onCompleted:page.searchField=field
                        width: page.isLandscape ? parent.width - searchButton.width - Theme.horizontalPageMargin : parent.width
                        placeholderText: page.modeIndex === 0 ? "搜索歌曲" : "搜索歌单"
                        enabled: core.ready
                        onActiveFocusChanged: { if (activeFocus) Qt.inputMethod.show() }
                        EnterKey.iconSource: "image://theme/icon-m-search"
                        EnterKey.onClicked: { focus = false; page.search(false) }
                    }
                    Button {
                        id: searchButton
                        objectName: "searchButton"
                        width: page.isLandscape ? 2 * Theme.itemSizeSmall : preferredWidth
                        x: page.isLandscape ? parent.width - width - Theme.horizontalPageMargin : (parent.width - width) / 2
                        y: page.isLandscape ? (parent.height - height) / 2 : field.height
                        text: "搜索"
                        enabled: core.ready && !page.searching && field.text.trim().length > 0
                        onClicked: { field.focus = false; page.search(false) }
                    }
                }
            }
            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryHighlightColor
                text: !core.ready ? "正在连接音乐核心…" : page.searchError || app.errorText
                visible: text.length > 0
            }
            BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: page.searching || !core.ready; visible: running; size: BusyIndicatorSize.Medium }
        }
        delegate: ListItem {
            width: list.width
            contentHeight: Theme.itemSizeMedium
            Column {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                anchors.verticalCenter: parent.verticalCenter
                Label { width: parent.width; text: modelData.title; truncationMode: TruncationMode.Fade }
                Label { width: parent.width; text: page.modeIndex===0 ? modelData.artist : (modelData.count+" 首 · "+modelData.creator); color: Theme.secondaryColor; font.pixelSize: Theme.fontSizeSmall; truncationMode: TruncationMode.Fade }
            }
            onClicked: {
                if (page.modeIndex===1) {
                    pageStack.push(Qt.resolvedUrl("LibrarySongsPage.qml"),{pageTitle:modelData.title,requestArgs:{source:"platform",platform:modelData.platform,id:modelData.id}})
                } else {
                    app.call("playResult", {token: modelData.token, index: modelData.resultIndex})
                    app.openPlayer()
                }
            }
            menu: ContextMenu {
                MenuItem { text: "歌曲信息与收藏"; visible:page.modeIndex===0; onClicked: pageStack.push(Qt.resolvedUrl("SongActionsPage.qml"), {token:modelData.token,songIndex:modelData.resultIndex}) }
                MenuItem { text: "下载歌曲"; visible:page.modeIndex===0; onClicked: pageStack.push(Qt.resolvedUrl("DownloadSongPage.qml"), {token:modelData.token,songIndex:modelData.resultIndex,songTitle:modelData.title}) }
            }
        }
        footer: Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: page.searching ? "加载中…" : "加载更多"
            visible: page.hasMore
            enabled: !page.searching
            onClicked: page.search(true)
        }
        ViewPlaceholder {
            id: emptyState
            objectName: "searchPlaceholder"
            flickable: list
            parent: list.contentItem
            width: list.width
            x: 0
            // Keep the empty state below the search controls in this viewport.
            y: list.originY + (list.headerItem ? list.headerItem.height : 0)
               + Math.max(Theme.paddingLarge, (list.height - (list.headerItem ? list.headerItem.height : 0) - height) / 2)
            enabled: !page.searching && !page.results.length && !page.searchError.length && core.ready
            text: page.activeQuery.length ? "暂无搜索结果" : "搜索喜欢的音乐"
            hintText: page.activeQuery.length ? "试试其他关键词或音乐平台" : "歌曲、歌手或歌单"
        }
        VerticalScrollDecorator {}
    }
    AppFooter { id: mini;visible:!page.embedded && !Qt.inputMethod.visible; anchors.bottom: parent.bottom }
}
