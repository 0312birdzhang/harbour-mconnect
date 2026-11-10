import QtQuick 2.0
import Sailfish.Silica 1.0

Item {
    id: header
    objectName: "appHeader"
    property string title: ""
    property string description: ""
    property bool tools: false
    property bool showBack: pageStack.depth > 1
    property alias headerPage: nativeHeader.page
    readonly property real topInset: headerPage && headerPage.orientation === Orientation.Portrait
                                     ? Screen.topCutout.height : 0
    width: parent ? parent.width : 0
    height: titles.height + 2 * Theme.paddingLarge + topInset
    // PageHeader resolves the containing Page through nested layout items.
    PageHeader {
        id: nativeHeader
        visible: false
        interactive: false
    }
    Column {
        id: titles
        objectName: "headerTitles"
        x: Theme.horizontalPageMargin
        width: parent.width - x - Theme.horizontalPageMargin - (header.tools ? Theme.itemSizeSmall : 0)
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: header.topInset / 2
        spacing: Theme.paddingSmall
        Label { width: parent.width; text: header.title; textFormat: Text.PlainText; font.pixelSize: Theme.fontSizeLarge; truncationMode: TruncationMode.Fade }
        Label { width: parent.width; text: header.description; textFormat: Text.PlainText; visible: text.length > 0; color: Theme.secondaryColor; font.pixelSize: Theme.fontSizeExtraSmall; truncationMode: TruncationMode.Fade }
    }
    IconButton {
        objectName: "headerSettingsButton"
        anchors.right: parent.right
        anchors.rightMargin: Theme.paddingMedium
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: header.topInset / 2
        visible: header.tools
        icon.source: "image://theme/icon-m-setting"
        onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
    }
}
