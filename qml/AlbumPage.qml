import QtQuick 2.0
import Sailfish.Silica 1.0
Page{
    id:page
    property string platform:"netease"
    property string albumId:""
    property string albumName:"专辑"
    property var info:({title:"",artist:"",description:"",artwork:"",date:"",company:""})
    property int pending:0
    property int pendingPlay:0
    property string errorText:""
    function load(){if(!core.ready||pending||!albumId.length)return;pending=app.call("albumInfo",{platform:platform,id:albumId},function(value,error){pending=0;errorText=error;if(!error.length)info=value})}
    function play(){if(pendingPlay)return;pendingPlay=app.call("albumSongs",{platform:platform,id:albumId},function(value,error){pendingPlay=0;errorText=error;if(error.length)return;if(!value.total){errorText="暂无歌曲";return}app.call("playResult",{token:value.token,index:0});app.openPlayer()})}
    Component.onCompleted:load()
    Component.onDestruction:{if(pending)delete app.callbacks[pending];if(pendingPlay)delete app.callbacks[pendingPlay]}
    SilicaFlickable{anchors.fill:parent;anchors.bottomMargin:mini.visible?mini.height:0;contentHeight:column.height
        Column{id:column;width:parent.width;spacing:Theme.paddingLarge
            AppHeader{title:page.info.title || page.albumName;description:page.info.artist}
            Image{anchors.horizontalCenter:parent.horizontalCenter;width:Theme.itemSizeHuge*2;height:width;source:page.info.artwork;visible:!!source.toString().length;fillMode:Image.PreserveAspectCrop;asynchronous:true}
            Label{x:Theme.horizontalPageMargin;width:parent.width-2*x;text:page.info.date+" "+page.info.company;textFormat:Text.PlainText;wrapMode:Text.Wrap;color:Theme.secondaryColor;font.pixelSize:Theme.fontSizeSmall}
            BusyIndicator{anchors.horizontalCenter:parent.horizontalCenter;running:!!page.pending || !!page.pendingPlay;visible:running}
            Label{x:Theme.horizontalPageMargin;width:parent.width-2*x;text:page.errorText;visible:!!text.length;wrapMode:Text.Wrap;color:Theme.highlightColor}
            Button{anchors.horizontalCenter:parent.horizontalCenter;text:"专辑歌曲";enabled:core.ready && !!page.albumId.length;onClicked:pageStack.push(Qt.resolvedUrl("LibrarySongsPage.qml"),{pageTitle:page.albumName,requestMethod:"albumSongs",requestArgs:{platform:page.platform,id:page.albumId}})}
            Button{anchors.horizontalCenter:parent.horizontalCenter;text:"播放全部";enabled:core.ready && !!page.albumId.length && !page.pendingPlay;onClicked:page.play()}
            Label{x:Theme.horizontalPageMargin;width:parent.width-2*x;text:page.info.description;textFormat:Text.PlainText;wrapMode:Text.Wrap;color:Theme.secondaryColor;font.pixelSize:Theme.fontSizeSmall}
        }
        VerticalScrollDecorator{}
    }
    AppFooter{id:mini;anchors.bottom:parent.bottom}
}
