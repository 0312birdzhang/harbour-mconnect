import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    property string errorText: ""
    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: mini.visible ? mini.height : 0
        model: core.accounts
        header: Column {
            width: parent.width
            AppHeader { title: "账号管理" }
            Label { x: Theme.horizontalPageMargin; width: parent.width - 2*x; wrapMode: Text.Wrap; text: page.errorText; visible: text.length > 0; color: Theme.highlightColor }
        }
        delegate: ListItem {
            id: row
            width: list.width
            contentHeight: Theme.itemSizeMedium
            Column {
                x: Theme.horizontalPageMargin; width: parent.width - 2*x; anchors.verticalCenter: parent.verticalCenter
                Label { text: modelData.name }
                Label { width: parent.width; truncationMode: TruncationMode.Fade; text: modelData.loggedIn ? modelData.nickname + " · 已登录" : "未登录"; color: Theme.secondaryColor; font.pixelSize: Theme.fontSizeSmall }
            }
            menu: ContextMenu {
                MenuItem {
                    text: "退出登录"
                    visible: modelData.loggedIn
                    onClicked: {
                        var platform = modelData.id
                        row.remorseAction("正在退出登录", function() {
                            app.call("logout", {platform:platform})
                        })
                    }
                }
            }
            onClicked: pageStack.push(Qt.resolvedUrl("LoginPage.qml"), {platform:modelData.id,platformName:modelData.name})
        }
        VerticalScrollDecorator {}
    }
    AppFooter { id: mini; anchors.bottom: parent.bottom }
}
