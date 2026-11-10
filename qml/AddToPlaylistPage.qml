import QtQuick 2.0
import Sailfish.Silica 1.0
Page{
    id:page
    property string token:""
    property int songIndex:0
    property var rows:[]
    property int pending:0
    property bool busy:false
    property string errorText:""
    function load(){if(!core.ready||pending)return;pending=app.call("libraryPlaylists",{source:"mine"},function(value,error){pending=0;errorText=error;if(!error.length)rows=value})}
    Component.onCompleted:load()
    Component.onDestruction:{if(pending)delete app.callbacks[pending]}
    SilicaListView{id:list;anchors.fill:parent;anchors.bottomMargin:mini.visible?mini.height:0;model:page.rows
        header:Column{width:parent.width;AppHeader{title:"添加到本地歌单"}Label{x:Theme.horizontalPageMargin;width:parent.width-2*x;text:page.errorText;visible:!!text.length;wrapMode:Text.Wrap;color:Theme.highlightColor}BusyIndicator{anchors.horizontalCenter:parent.horizontalCenter;running:page.busy;visible:running}}
        delegate:ListItem{width:list.width;contentHeight:Theme.itemSizeMedium;enabled:!page.busy
            Label{x:Theme.horizontalPageMargin;width:parent.width-2*x;anchors.verticalCenter:parent.verticalCenter;text:modelData.title;textFormat:Text.PlainText;truncationMode:TruncationMode.Fade}
            onClicked:{page.busy=true;app.call("addToPlaylist",{id:modelData.id,token:page.token,index:page.songIndex},function(value,error){if(!page)return;page.busy=false;page.errorText=error;if(!error.length)pageStack.pop()})}
        }
        ViewPlaceholder{enabled:!page.pending && !list.count;text:"暂无本地歌单";hintText:"请先在音乐库新建歌单"}
        VerticalScrollDecorator{}
    }
    AppFooter {id:mini;anchors.bottom:parent.bottom}
}
