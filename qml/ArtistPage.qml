import QtQuick 2.0
import Sailfish.Silica 1.0
Page{
    id:page
    property string platform:"netease"
    property string artistId:""
    property string artistName:"歌手"
    property var info:({title:"",description:"",artwork:"",albumCount:0,songCount:0,albumsSupported:false})
    property int pending:0
    property string errorText:""
    function load(){if(!core.ready||pending||!artistId.length)return;pending=app.call("artistInfo",{platform:platform,id:artistId},function(value,error){pending=0;errorText=error;if(!error.length)info=value})}
    Component.onCompleted:load()
    Component.onDestruction:{if(pending)delete app.callbacks[pending]}
    SilicaFlickable{anchors.fill:parent;anchors.bottomMargin:mini.visible?mini.height:0;contentHeight:column.height
        PullDownMenu{MenuItem{text:"刷新";enabled:!page.pending;onClicked:page.load()}}
        Column{id:column;width:parent.width;spacing:Theme.paddingLarge
            AppHeader{title:page.info.title || page.artistName}
            Image{anchors.horizontalCenter:parent.horizontalCenter;width:Theme.itemSizeHuge*2;height:width;source:page.info.artwork;visible:!!source.toString().length;fillMode:Image.PreserveAspectCrop;asynchronous:true}
            BusyIndicator{anchors.horizontalCenter:parent.horizontalCenter;running:!!page.pending;visible:running}
            Label{x:Theme.horizontalPageMargin;width:parent.width-2*x;text:page.errorText;visible:!!text.length;wrapMode:Text.Wrap;color:Theme.highlightColor}
            Button{anchors.horizontalCenter:parent.horizontalCenter;text:"热门歌曲";enabled:core.ready && !!page.artistId.length;onClicked:pageStack.push(Qt.resolvedUrl("LibrarySongsPage.qml"),{pageTitle:page.artistName+" · 热门歌曲",requestMethod:"artistSongs",requestArgs:{platform:page.platform,id:page.artistId}})}
            Button{anchors.horizontalCenter:parent.horizontalCenter;text:"专辑"+(page.info.albumCount>0?"（"+page.info.albumCount+"）":"");enabled:core.ready && page.info.albumsSupported;onClicked:pageStack.push(Qt.resolvedUrl("ArtistAlbumsPage.qml"),{platform:page.platform,artistId:page.artistId,artistName:page.artistName})}
            Label{x:Theme.horizontalPageMargin;width:parent.width-2*x;text:page.info.description;textFormat:Text.PlainText;wrapMode:Text.Wrap;color:Theme.secondaryColor;font.pixelSize:Theme.fontSizeSmall}
        }
        VerticalScrollDecorator{}
    }
    AppFooter{id:mini;anchors.bottom:parent.bottom}
}
