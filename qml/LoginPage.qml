import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    property string platform: "netease"
    property string platformName: "网易云音乐"
    property string session: ""
    property string image: ""
    property string loginStatus: ""
    property string errorText: ""
    property bool loading: false
    property int generation: 0
    property int pending: 0
    function refresh() {
        if (!core.ready || loading) return
        var attempt = ++generation
        loading = true; image = ""; errorText = ""; loginStatus = "loading"
        session = Date.now().toString() + "-" + Math.random().toString().substring(2)
        pending = app.call("loginBegin", {session:session,platform:platform,variant:platform === "kugou" ? (variant.currentIndex === 0 ? "lite" : "android") : null}, function(value,error) {
            if (!page || attempt !== generation) return
            pending = 0; loading = false
            if (error.length) { errorText = error; loginStatus = "failed"; return }
            if (value.cancelled) return
            session = value.session; image = value.image; loginStatus = value.status
            app.call("loginStatus", {}, function(latest,error) {
                if (!page || error.length || latest.session !== session) return
                loginStatus = latest.status
            })
        })
    }
    Connections {
        target: core
        onLoginChanged: {
            if (core.login.session === page.session) page.loginStatus = core.login.status
        }
    }
    onLoginStatusChanged: {
        if (loginStatus === "success") {
            app.call("accounts")
            pageStack.pop()
        }
    }
    Component.onCompleted: { if (!validationMode) refresh() }
    Component.onDestruction: {
        ++generation
        if (pending) delete app.callbacks[pending]
        if (session.length) app.call("loginCancel", {session:session})
    }
    Connections { target: core; onReadyChanged: { if (core.ready && !page.session.length) page.refresh() } }
    SilicaFlickable {
        anchors.fill: parent; anchors.bottomMargin: mini.visible ? mini.height : 0
        contentHeight: column.height
        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge
            AppHeader { title: page.platformName + "登录" }
            ComboBox {
                id: variant
                width: parent.width
                visible: page.platform === "kugou"
                label: "客户端"
                menu: ContextMenu { MenuItem { text: "酷狗概念版" } MenuItem { text: "酷狗音乐" } }
                enabled: !page.loading
                onCurrentIndexChanged: { if (page.session.length) page.refresh() }
            }
            BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: page.loading; visible: running; size: BusyIndicatorSize.Large }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width - 2 * Theme.horizontalPageMargin, Theme.itemSizeHuge * 3)
                height: width
                color: "white"
                visible: page.image.length > 0
                Image { anchors.fill: parent; anchors.margins: Theme.paddingLarge; source: page.image; fillMode: Image.PreserveAspectFit; smooth: false }
            }
            Label {
                x: Theme.horizontalPageMargin; width: parent.width - 2*x
                wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                text: page.errorText || ({loading:"正在获取二维码…",waiting:"请使用对应平台的客户端扫描二维码",scanned:"已扫码，请在客户端确认登录",saving:"正在保存登录状态…",success:"登录成功",expired:"二维码已过期，请刷新",failed:"登录失败，请重试"})[page.loginStatus] || ""
            }
            Button { anchors.horizontalCenter: parent.horizontalCenter; text: "刷新二维码"; enabled: core.ready && !page.loading && page.loginStatus !== "saving"; onClicked: page.refresh() }
            Item { width: 1; height: Theme.paddingLarge }
        }
        VerticalScrollDecorator {}
    }
    AppFooter {id:mini;anchors.bottom:parent.bottom}
}
