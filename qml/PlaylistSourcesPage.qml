import QtQuick 2.0
import Sailfish.Silica 1.0
Page {
    SilicaFlickable {
        anchors.fill:parent;anchors.bottomMargin:mini.visible?mini.height:0;contentHeight:column.height
        Column {
            id:column;width:parent.width
            AppHeader{title:"歌单"}
            ListItem{width:parent.width;contentHeight:Theme.itemSizeMedium
                Label{x:Theme.horizontalPageMargin;anchors.verticalCenter:parent.verticalCenter;text:"我的歌单";color:Theme.highlightColor}
                onClicked:pageStack.push(Qt.resolvedUrl("LibraryPlaylistsPage.qml"),{source:"mine",sourceName:"我的歌单"})
            }
            SectionHeader{text:"平台歌单"}
            Repeater{
                model:core.accounts
                ListItem{
                    width:column.width;contentHeight:Theme.itemSizeMedium
                    Column{x:Theme.horizontalPageMargin;width:parent.width-2*x;anchors.verticalCenter:parent.verticalCenter
                        Label{text:modelData.name}
                        Label{text:modelData.loggedIn?modelData.nickname:"未登录 · 点击扫码登录";font.pixelSize:Theme.fontSizeSmall;color:Theme.secondaryColor}
                    }
                    onClicked:{if(modelData.loggedIn)pageStack.push(Qt.resolvedUrl("LibraryPlaylistsPage.qml"),{source:modelData.id,sourceName:modelData.name});else pageStack.push(Qt.resolvedUrl("LoginPage.qml"),{platform:modelData.id,platformName:modelData.name})}
                }
            }
        }
        VerticalScrollDecorator{}
    }
    AppFooter{id:mini;anchors.bottom:parent.bottom}
}
