import QtQuick 2.0
import Sailfish.Silica 1.0
Page{
    id:page
    property string platform:"netease"
    property string artistId:""
    property string artistName:"歌手"
    property var rows:[]
    property int currentPage:0
    property bool more:false
    property int pending:0
    property string errorText:""
    function load(append){if(!core.ready||pending||!artistId.length)return;var next=append?currentPage+1:1;pending=app.call("artistAlbums",{platform:platform,id:artistId,page:next},function(value,error){pending=0;errorText=error;if(error.length)return;rows=append?rows.concat(value):value;currentPage=next;more=value.length>=30})}
    Component.onCompleted:load(false)
    Component.onDestruction:{if(pending)delete app.callbacks[pending]}
    SilicaListView{id:list;anchors.fill:parent;anchors.bottomMargin:mini.visible?mini.height:0;model:page.rows
        PullDownMenu{MenuItem{text:"刷新";enabled:!page.pending;onClicked:page.load(false)}}
        header:Column{width:parent.width;AppHeader{title:page.artistName+"的专辑"}BusyIndicator{anchors.horizontalCenter:parent.horizontalCenter;running:!!page.pending;visible:running}Label{x:Theme.horizontalPageMargin;width:parent.width-2*x;text:page.errorText;visible:!!text.length;wrapMode:Text.Wrap;color:Theme.highlightColor}}
        delegate:ListItem{width:list.width;contentHeight:Theme.itemSizeMedium
            Column{x:Theme.horizontalPageMargin;width:parent.width-2*x;anchors.verticalCenter:parent.verticalCenter
                Label{width:parent.width;text:modelData.title;textFormat:Text.PlainText;truncationMode:TruncationMode.Fade}
                Label{width:parent.width;text:modelData.date+(modelData.count>0?" · "+modelData.count+" 首":"");font.pixelSize:Theme.fontSizeSmall;color:Theme.secondaryColor}
            }
            onClicked:pageStack.push(Qt.resolvedUrl("AlbumPage.qml"),{platform:page.platform,albumId:modelData.id,albumName:modelData.title})
        }
        footer:Button{anchors.horizontalCenter:parent.horizontalCenter;text:"加载更多";visible:page.more;enabled:!page.pending;onClicked:page.load(true)}
        ViewPlaceholder{enabled:!page.pending && !page.errorText.length && !list.count;text:"暂无专辑"}
        VerticalScrollDecorator{}
    }
    AppFooter{id:mini;anchors.bottom:parent.bottom}
}
