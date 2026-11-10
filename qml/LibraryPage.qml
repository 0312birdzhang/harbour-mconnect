import QtQuick 2.0
import Sailfish.Silica 1.0

Item {
    id:page
    objectName:"libraryRoot"
    property alias flickableItem: libraryScroll
    property bool embedded: false
    property bool active: true
    property int status: active ? PageStatus.Active : PageStatus.Inactive
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    SilicaFlickable {
        id:libraryScroll
        anchors.fill:parent;anchors.bottomMargin:mini.visible?mini.height:0;contentHeight:column.height
        Column {
            id:column;width:parent.width
            AppHeader{visible:!page.embedded;title:"音乐库";tools:true}
            Repeater {
                model:[{title:"喜欢的歌曲",icon:"favorite",kind:"likes"},{title:"播放历史",icon:"music",kind:"history"},{title:"本地音乐",icon:"folder",kind:"local"},{title:"缓存设置",icon:"downloads",kind:"cache"},{title:"歌单",icon:"menu",kind:"playlists"},{title:"导入歌单",icon:"share",kind:"import"},{title:"下载管理",icon:"downloads",kind:"downloads"}]
                ListItem {
                    width:column.width;contentHeight:Theme.itemSizeMedium;enabled:core.ready
                    Icon{id:icon;x:Theme.horizontalPageMargin;anchors.verticalCenter:parent.verticalCenter;source:"image://theme/icon-m-"+modelData.icon;color:modelData.kind==="likes"?"#ed6b7b":Theme.highlightColor}
                    Label{anchors.left:icon.right;anchors.leftMargin:Theme.paddingLarge;anchors.right:arrow.left;anchors.verticalCenter:parent.verticalCenter;text:modelData.title;font.pixelSize:Theme.fontSizeMedium}
                    Icon{id:arrow;anchors.right:parent.right;anchors.rightMargin:Theme.horizontalPageMargin;anchors.verticalCenter:parent.verticalCenter;source:"image://theme/icon-m-right";color:Theme.secondaryColor}
                    onClicked:{
                        var kind=modelData.kind
                        if(kind==="local")pageStack.push(Qt.resolvedUrl("LocalMusicPage.qml"))
                        else if(kind==="playlists")pageStack.push(Qt.resolvedUrl("PlaylistSourcesPage.qml"))
                        else if(kind==="import")pageStack.push(Qt.resolvedUrl("PlaylistTransferPage.qml"))
                        else if(kind==="downloads")app.switchTab(3)
                        else if(kind==="cache")pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
                        else pageStack.push(Qt.resolvedUrl("LibrarySongsPage.qml"),{pageTitle:modelData.title,requestArgs:{source:kind}})
                    }
                }
            }
            Rectangle{x:Theme.horizontalPageMargin;width:parent.width-2*x;height:1;color:Theme.secondaryColor;opacity:0.2}
            ListItem{width:parent.width;contentHeight:Theme.itemSizeMedium
                Icon{id:accountIcon;x:Theme.horizontalPageMargin;anchors.verticalCenter:parent.verticalCenter;source:"image://theme/icon-m-person";color:Theme.highlightColor}
                Label{anchors.left:accountIcon.right;anchors.leftMargin:Theme.paddingLarge;anchors.verticalCenter:parent.verticalCenter;text:"账号管理"}
                onClicked:pageStack.push(Qt.resolvedUrl("AccountsPage.qml"))
            }
            ListItem{objectName:"librarySettingsEntry";width:parent.width;contentHeight:Theme.itemSizeMedium
                Icon{id:settingsIcon;x:Theme.horizontalPageMargin;anchors.verticalCenter:parent.verticalCenter;source:"image://theme/icon-m-setting";color:Theme.highlightColor}
                Label{anchors.left:settingsIcon.right;anchors.leftMargin:Theme.paddingLarge;anchors.verticalCenter:parent.verticalCenter;text:"设置"}
                onClicked:pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            ListItem {
                objectName: "libraryAboutEntry"
                width: parent.width
                contentHeight: Theme.itemSizeMedium
                Icon {
                    id: aboutIcon
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-m-about"
                    color: Theme.highlightColor
                }
                Label {
                    anchors.left: aboutIcon.right
                    anchors.leftMargin: Theme.paddingLarge
                    anchors.verticalCenter: parent.verticalCenter
                    text: "关于"
                }
                onClicked: app.openUnique("AboutPage.qml", "aboutPage", {})
            }
            Item{width:1;height:Theme.paddingLarge}
        }
        VerticalScrollDecorator{}
    }
    AppFooter{id:mini;visible:!page.embedded && !Qt.inputMethod.visible;anchors.bottom:parent.bottom}
}
