import QtQuick 2.0
import Sailfish.Silica 1.0

Item {
    id: footer
    objectName: "appFooter"
    readonly property real bottomPadding: Theme.paddingLarge
    width: parent ? parent.width : 0
    height: content.height + bottomPadding
    visible: !Qt.inputMethod.visible
    z: 100
    clip: true
    Rectangle {objectName:"footerBackdrop";anchors.fill:parent;color:Theme.primaryColor.r+Theme.primaryColor.g+Theme.primaryColor.b>1.5?"#171b22":"#f2f4f7"}
    Rectangle {
        objectName: "navigationCapsule"
        x: Theme.paddingMedium
        y: content.y + navigationRow.y + Theme.paddingSmall
        width: parent.width - 2*x
        height: navigationRow.height - 2*Theme.paddingSmall
        radius: height/2
        color: Theme.highlightBackgroundColor
        opacity: Theme.highlightBackgroundOpacity
    }
    Column {
        id: content
        width: parent.width
        MiniPlayer { id: miniBar; objectName: "footerMiniPlayer" }
        Row {
            id: navigationRow
            objectName: "navigationRow"
            x: Theme.paddingMedium
            width: parent.width - 2*x
            height: Theme.itemSizeSmall + Theme.paddingMedium
            Repeater {
                model: [{title:"探索",icon:"search"},{title:"发现",icon:"favorite"},{title:"音乐库",icon:"folder"},{title:"下载",icon:"downloads"}]
                BackgroundItem {
                    objectName: "navigationButton"
                    width: (footer.width - 2*Theme.paddingMedium) / 4
                    height: parent.height
                    enabled: !pageStack.busy
                    Rectangle {anchors.centerIn:parent;width:parent.width*0.9;height:parent.height-2*Theme.paddingSmall;radius:height/2;color:Theme.highlightColor;opacity:0.15;visible:index===app.tabIndex}
                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.paddingSmall
                        Icon { anchors.horizontalCenter: parent.horizontalCenter; source: "image://theme/icon-m-" + modelData.icon; width: Theme.iconSizeSmall; height: width; color: index === app.tabIndex ? Theme.highlightColor : Theme.secondaryColor }
                        Label { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.title; font.pixelSize: Theme.fontSizeExtraSmall; color: index === app.tabIndex ? Theme.highlightColor : Theme.secondaryColor }
                    }
                    onClicked: app.switchTab(index)
                }
            }
        }
    }
}
