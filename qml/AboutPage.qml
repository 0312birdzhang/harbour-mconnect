import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    objectName: "aboutPage"

    SilicaFlickable {
        id: aboutScroll
        objectName: "aboutScroll"
        anchors.fill: parent
        anchors.bottomMargin: mini.visible ? mini.height : 0
        contentHeight: content.height
        clip: true

        Column {
            id: content
            width: parent.width

            AppHeader { title: "关于" }

            Flow {
                id: body
                objectName: "aboutContent"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                spacing: Theme.paddingLarge

                Column {
                    id: identity
                    objectName: "aboutIdentity"
                    width: page.isLandscape ? (body.width - body.spacing) / 2 : body.width
                    spacing: Theme.paddingMedium

                    Image {
                        objectName: "aboutIcon"
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Theme.iconSizeLauncher
                        height: width
                        source: "/usr/share/icons/hicolor/172x172/apps/harbour-mconnect.png"
                        fillMode: Image.PreserveAspectFit
                    }
                    Label {
                        objectName: "aboutName"
                        width: parent.width
                        text: "Mconnect"
                        font.pixelSize: Theme.fontSizeLarge
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                    Label {
                        objectName: "aboutVersion"
                        width: parent.width
                        text: "版本 " + Qt.application.version
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryColor
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                    Label {
                        width: parent.width
                        text: "SailfishOS 原生音乐播放器"
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryColor
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                }

                Column {
                    id: details
                    objectName: "aboutDetails"
                    width: identity.width
                    spacing: Theme.paddingMedium

                    Label {
                        width: parent.width
                        text: "开发者"
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.highlightColor
                    }
                    Label {
                        objectName: "aboutDeveloper"
                        width: parent.width
                        text: "BirdZhang"
                        wrapMode: Text.Wrap
                    }
                    Label {
                        width: parent.width
                        text: "参考项目"
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.highlightColor
                    }
                    Repeater {
                        model: [
                            {name: "Mconnect-Music_connect", url: "https://github.com/Hjdd14/Mconnect-Music_connect", description: "音乐应用与多平台业务实现"},
                            {name: "BiliMusic", url: "https://github.com/eason919191/BiliMusic", description: "哔哩哔哩音乐接口适配"}
                        ]
                        BackgroundItem {
                            objectName: "aboutReference"
                            property string projectName: modelData.name
                            property url projectUrl: modelData.url
                            width: details.width
                            height: referenceColumn.height + 2 * Theme.paddingMedium

                            Column {
                                id: referenceColumn
                                x: Theme.paddingMedium
                                y: Theme.paddingMedium
                                width: parent.width - 2 * x
                                spacing: Theme.paddingSmall

                                Label {
                                    width: parent.width
                                    text: modelData.name
                                    textFormat: Text.PlainText
                                    color: Theme.highlightColor
                                    wrapMode: Text.Wrap
                                }
                                Label {
                                    width: parent.width
                                    text: modelData.description
                                    font.pixelSize: Theme.fontSizeSmall
                                    color: Theme.secondaryColor
                                    wrapMode: Text.Wrap
                                }
                                Label {
                                    width: parent.width
                                    text: modelData.url
                                    textFormat: Text.PlainText
                                    font.pixelSize: Theme.fontSizeExtraSmall
                                    color: Theme.secondaryColor
                                    wrapMode: Text.WrapAnywhere
                                }
                            }
                            onClicked: Qt.openUrlExternally(projectUrl)
                        }
                    }
                    Label {
                        width: parent.width
                        text: "许可证 MIT"
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryColor
                    }
                }
            }
            Item { width: 1; height: Theme.paddingLarge }
            Column {
                objectName: "aboutDisclaimer"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                spacing: Theme.paddingMedium

                Label {
                    width: parent.width
                    text: "免责声明"
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                }
                Label {
                    objectName: "aboutDisclaimerText"
                    width: parent.width
                    text: "本项目为个人学习与自用开发的第三方客户端，与网易云音乐、QQ 音乐、酷狗音乐官方无任何关联。\n\n"
                          + "不提供、不存储、不分发任何音乐内容；所有内容版权归各平台与版权方所有。\n\n"
                          + "登录凭据只保存在本机（系统安全存储），仓库与日志中不含任何真实账号信息。\n\n"
                          + "接口随时可能调整，功能以实际可用为准。使用产生的任何后果由使用者自行承担。"
                    textFormat: Text.PlainText
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryColor
                    wrapMode: Text.Wrap
                }
            }
            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
    AppFooter { id: mini; anchors.bottom: parent.bottom }
}
