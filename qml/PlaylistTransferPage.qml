import QtQuick 2.0
import Sailfish.Silica 1.0
Page{
    id:page
    property bool importMode:true
    property string playlistId:""
    property string playlistName:""
    property string link:""
    property int pending:0
    property string message:""
    function exportLink(){if(!core.ready||!playlistId.length||pending)return;pending=app.call("exportPlaylist",{id:playlistId},function(value,error){pending=0;message=error;if(!error.length){link=value.link;message="链接已生成，可复制到剪贴板"}})}
    function importLink(){if(pending)return;pending=app.call("importPlaylist",{link:input.text.trim()},function(value,error){pending=0;message=error;if(!error.length){message="已导入："+value.title;input.text=""}})}
    Component.onCompleted:{if(!importMode)exportLink()}
    Component.onDestruction:{if(pending)delete app.callbacks[pending]}
    SilicaFlickable{anchors.fill:parent;anchors.bottomMargin:mini.visible?mini.height:0;contentHeight:column.height
        Column{id:column;width:parent.width;spacing:Theme.paddingLarge
            AppHeader{title:page.importMode?"导入歌单链接":"导出歌单链接";description:page.playlistName}
            Label{x:Theme.horizontalPageMargin;width:parent.width-2*x;text:page.importMode?"粘贴 Mconnect 或平台的完整歌单链接，保存到本地歌单。":"导出歌单名称和歌曲信息，本地音频文件需要另行传输。";wrapMode:Text.Wrap;color:Theme.secondaryColor}
            TextArea{id:input;width:parent.width;height:Theme.itemSizeHuge*2;visible:page.importMode;placeholderText:"粘贴歌单链接";label:"歌单链接";inputMethodHints:Qt.ImhNoPredictiveText|Qt.ImhNoAutoUppercase}
            Button{anchors.horizontalCenter:parent.horizontalCenter;visible:page.importMode;text:"粘贴剪贴板";enabled:!page.pending;onClicked:input.text=core.clipboardText()}
            Button{anchors.horizontalCenter:parent.horizontalCenter;visible:page.importMode;text:"导入";enabled:core.ready && !page.pending && input.text.trim().length>0 && input.text.length<=250000;onClicked:{input.focus=false;page.importLink()}}
            Button{anchors.horizontalCenter:parent.horizontalCenter;visible:!page.importMode;text:"复制歌单链接";enabled:page.link.length>0;onClicked:page.message=core.copyText(page.link)?"链接已复制":"复制失败，请重试"}
            BusyIndicator{anchors.horizontalCenter:parent.horizontalCenter;running:!!page.pending;visible:running}
            Label{x:Theme.horizontalPageMargin;width:parent.width-2*x;text:page.message;textFormat:Text.PlainText;visible:!!text.length;wrapMode:Text.Wrap;color:Theme.highlightColor}
        }
        VerticalScrollDecorator{}
    }
    AppFooter {id:mini;anchors.bottom:parent.bottom}
}
