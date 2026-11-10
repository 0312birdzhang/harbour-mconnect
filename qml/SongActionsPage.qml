import QtQuick 2.0
import Sailfish.Silica 1.0
Page {
    id:page
    property bool current:false
    property string token:""
    property int songIndex:0
    property var details:({song:{title:"歌曲信息",artist:"",album:"",albumId:"",artists:[]},reference:"",canArtist:false,canAlbum:false})
    property bool liked:false
    property bool likeBusy:false
    property int pending:0
    property string errorText:""
    function load(){if(!core.ready||pending)return;pending=app.call("songInfo",{current:current,token:token,index:songIndex},function(value,error){pending=0;errorText=error;if(!error.length){details=value;liked=value.liked}})}
    Component.onCompleted:load()
    Component.onDestruction:{if(pending)delete app.callbacks[pending]}
    SilicaFlickable {anchors.fill:parent;anchors.bottomMargin:mini.visible?mini.height:0;contentHeight:column.height
        Column{id:column;width:parent.width;spacing:Theme.paddingMedium
            AppHeader{title:page.details.song.title;description:page.details.song.artist}
            Label{x:Theme.horizontalPageMargin;width:parent.width-2*x;text:page.errorText;visible:!!text.length;wrapMode:Text.Wrap;color:Theme.highlightColor}
            Button{anchors.horizontalCenter:parent.horizontalCenter;text:page.liked?"从喜欢移除":"收藏到喜欢";enabled:!!page.details.reference.length && !page.likeBusy;onClicked:{page.likeBusy=true;app.call("toggleLike",{token:page.details.reference,index:0},function(value,error){if(!page)return;page.likeBusy=false;page.errorText=error;if(!error.length)page.liked=value.liked})}}
            Button{anchors.horizontalCenter:parent.horizontalCenter;text:"添加到本地歌单";enabled:!!page.details.reference.length;onClicked:pageStack.push(Qt.resolvedUrl("AddToPlaylistPage.qml"),{token:page.details.reference,songIndex:0})}
            SectionHeader{text:"歌手"}
            Label{x:Theme.horizontalPageMargin;width:parent.width-2*x;text:page.details.song.artist;textFormat:Text.PlainText;wrapMode:Text.Wrap;color:Theme.secondaryColor;visible:!page.details.canArtist}
            Repeater{model:page.details.canArtist?page.details.song.artists:[]
                ListItem{width:column.width;contentHeight:Theme.itemSizeSmall
                    Label{x:Theme.horizontalPageMargin;anchors.verticalCenter:parent.verticalCenter;text:modelData.name;textFormat:Text.PlainText}
                    onClicked:pageStack.push(Qt.resolvedUrl("ArtistPage.qml"),{platform:page.details.song.platform,artistId:modelData.id,artistName:modelData.name})
                }
            }
            SectionHeader{text:"专辑"}
            ListItem{width:column.width;contentHeight:Theme.itemSizeSmall;enabled:page.details.canAlbum && !!page.details.song.albumId.length
                Label{x:Theme.horizontalPageMargin;width:parent.width-2*x;anchors.verticalCenter:parent.verticalCenter;text:page.details.song.album || "暂无专辑信息";textFormat:Text.PlainText;truncationMode:TruncationMode.Fade}
                onClicked:pageStack.push(Qt.resolvedUrl("AlbumPage.qml"),{platform:page.details.song.platform,albumId:page.details.song.albumId,albumName:page.details.song.album})
            }
        }
        VerticalScrollDecorator{}
    }
    AppFooter{id:mini;anchors.bottom:parent.bottom}
}
