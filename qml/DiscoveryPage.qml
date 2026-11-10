import QtQuick 2.0
import Sailfish.Silica 1.0

Item {
    id: page
    objectName: "discoveryRoot"
    property bool initialized: false
    property alias flickableItem: scroll
    property bool embedded: false
    property bool active: true
    property int status: active ? PageStatus.Active : PageStatus.Inactive
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    property var platforms: []
    property int platformIndex: 0
    property var selected: platforms.length > platformIndex ? platforms[platformIndex] : ({id:["netease","qq","kugou","bilibili"][platformIndex],name:["网易云音乐","QQ音乐","酷狗音乐","哔哩哔哩音乐"][platformIndex],daily:platformIndex!==3,charts:platformIndex!==3,newSongs:platformIndex!==3})
    property var songs: []
    property string token: ""
    property string sourceLabel: ""
    property string sourceNote: ""
    property int pendingPlatforms: 0
    property int pending: 0
    property int generation: 0
    property string errorText: ""
    function load(force) {
        if (!initialized || !core.ready || !active) return
        errorText=""
        if (!selected.daily){songs=[];token="";sourceLabel="";sourceNote="";pending=0;return}
        pending=app.requestRecommendations(selected.id,force)
    }
    function apply(value) { token=value.token; songs=value.songs.slice(0,9); sourceLabel=value.sourceLabel || ""; sourceNote=value.sourceNote || "" }
    function open(kind) {
        if (kind==="chart") pageStack.push(Qt.resolvedUrl("ChartsPage.qml"),{platform:selected.id,platformName:selected.name})
        else pageStack.push(Qt.resolvedUrl("LibrarySongsPage.qml"),{pageTitle:kind==="daily"?"每日推荐":"新歌速递",requestMethod:"discoverySongs",requestArgs:{platform:selected.id,kind:kind,region:"all"}})
    }
    function setup() {
        if (!initialized || !core.ready || !active) return
        pendingPlatforms=app.ensureDiscoveryPlatforms()
    }
    onActiveChanged: {if(active)setup()}
    Component.onCompleted: {initialized=true;setup()}
    Connections {target:core;onReadyChanged:{if(core.ready)page.setup()}}
    Connections {
        target:app
        onDiscoveryPlatformsReady:{
            if(!page.active)return
            page.pendingPlatforms=0;page.errorText=error
            if(!error.length){page.platforms=result;page.load(false)}
        }
        onDiscoveryReady:{
            if(platform!==page.selected.id)return
            page.pending=0;page.errorText=error
            if(!error.length)page.apply(result)
        }
        onDiscoveryReset:{if(page.active)page.setup()}
    }
    SilicaFlickable {
        id:scroll;objectName:"discoveryScroll"
        anchors.fill:parent; anchors.bottomMargin:mini.visible?mini.height:0
        contentHeight:column.height

        Column {
            id:column; width:parent.width; spacing:Theme.paddingMedium
            AppHeader {visible:!page.embedded;title:"发现";tools:true}
            PlatformTabs {visible:!page.embedded;width:parent.width;currentIndex:page.platformIndex;onSelected:{if(page.embedded)app.selectSlide(index);else{page.platformIndex=index;page.load(false)}}}
            Row {
                x:Theme.horizontalPageMargin;width:parent.width-2*x;height:Theme.itemSizeMedium
                Repeater {
                    model:[{title:"每日推荐",kind:"daily",icon:"favorite"},{title:"榜单中心",kind:"chart",icon:"menu"},{title:"新歌速递",kind:"new",icon:"music"}]
                    BackgroundItem {
                        width:parent.width/3;height:parent.height
                        enabled:core.ready && (modelData.kind==="daily"?page.selected.daily:modelData.kind==="chart"?page.selected.charts:page.selected.newSongs)
                        opacity:enabled?1:0.35
                        Column {anchors.centerIn:parent;spacing:Theme.paddingSmall
                            Icon {anchors.horizontalCenter:parent.horizontalCenter;source:"image://theme/icon-m-"+modelData.icon;color:Theme.highlightColor}
                            Label {anchors.horizontalCenter:parent.horizontalCenter;text:modelData.title;font.pixelSize:Theme.fontSizeSmall}
                        }
                        onClicked:page.open(modelData.kind)
                    }
                }
            }
            Item {
                x:Theme.horizontalPageMargin;width:parent.width-2*x;height:Theme.itemSizeSmall
                Label {anchors.verticalCenter:parent.verticalCenter;text:"歌单推荐";font.pixelSize:Theme.fontSizeMedium}
                BackgroundItem {anchors.right:parent.right;width:more.width+Theme.paddingLarge;height:parent.height;enabled:page.selected.daily && core.ready
                    Label {id:more;anchors.centerIn:parent;text:"查看全部";font.pixelSize:Theme.fontSizeSmall;color:Theme.highlightColor}
                    onClicked:page.open("daily")
                }
            }
            Label {x:Theme.horizontalPageMargin;width:parent.width-2*x;text:page.sourceLabel+(page.sourceNote.length?" · "+page.sourceNote:"");textFormat:Text.PlainText;visible:!!text.length;wrapMode:Text.Wrap;font.pixelSize:Theme.fontSizeExtraSmall;color:Theme.secondaryColor}
            BusyIndicator {anchors.horizontalCenter:parent.horizontalCenter;running:!!page.pending;visible:running}
            Label {x:Theme.horizontalPageMargin;width:parent.width-2*x;text:page.errorText || (!page.selected.daily?"该平台暂无推荐，请通过探索或音乐库浏览":!page.pending && !page.songs.length?"暂无推荐内容，可下拉刷新":"");visible:!!text.length;wrapMode:Text.Wrap;color:Theme.secondaryColor}
            Grid {
                id:grid;objectName:"recommendationGrid"
                x:Theme.horizontalPageMargin;width:parent.width-2*x;columns:3;spacing:Theme.paddingMedium
                Repeater {
                    model:page.songs
                    BackgroundItem {
                        id:card;objectName:"recommendationCard"
                        property string artwork:""
                        property int requestId:0
                        width:(grid.width-2*grid.spacing)/3
                        height:width+caption.height+Theme.paddingMedium
                        Component.onCompleted:{if(core.ready)requestId=app.call("resultArtwork",{token:page.token,index:index},function(value,error){requestId=0;if(!error.length)artwork=value.url})}
                        Component.onDestruction:{if(requestId)delete app.callbacks[requestId]}
                        Rectangle {width:parent.width;height:width;radius:Theme.paddingSmall;color:Theme.highlightBackgroundColor;opacity:0.3}
                        Icon {anchors.horizontalCenter:parent.horizontalCenter;y:(parent.width-height)/2;source:"image://theme/icon-m-music";color:Theme.secondaryColor;visible:!card.artwork.length}
                        Image {width:parent.width;height:width;source:card.artwork;fillMode:Image.PreserveAspectCrop;asynchronous:true}
                        Column {id:caption;y:parent.width+Theme.paddingSmall;width:parent.width
                            Label {width:parent.width;text:modelData.title;textFormat:Text.PlainText;height:font.pixelSize*2.7;maximumLineCount:2;wrapMode:Text.Wrap;font.pixelSize:Theme.fontSizeSmall;truncationMode:TruncationMode.Fade}
                            Label {width:parent.width;text:modelData.artist;textFormat:Text.PlainText;font.pixelSize:Theme.fontSizeExtraSmall;color:Theme.secondaryColor;truncationMode:TruncationMode.Fade}
                        }
                        onClicked:{app.call("playResult",{token:page.token,index:index});app.openPlayer()}
                    }
                }
            }
            Item {width:1;height:Theme.paddingLarge}
        }
        VerticalScrollDecorator{}
    }
    AppFooter{id:mini;visible:!page.embedded && !Qt.inputMethod.visible;anchors.bottom:parent.bottom}
}
