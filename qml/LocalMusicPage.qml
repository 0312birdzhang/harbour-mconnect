import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    property var info: ({count:0,scanning:false,directory:"",error:"",parsed:0,reused:0,removed:0})
    property int groupIndex: 0
    property var groups: []
    property int pending: 0
    property int pendingGroups: 0
    property int generation: 0
    property string errorText: ""
    function load() {
        if (!core.ready || pending) return
        pending=app.call("localSnapshot",{},function(value,error){
            pending=0;errorText=error
            if(error.length)return
            info=value
            if(!value.scanning)loadGroups()
        })
    }
    function loadGroups() {
        if (!core.ready) return
        var current=++generation
        pendingGroups=app.call("localGroups",{view:["albums","artists","folders"][page.groupIndex]},function(value,error){
            if(!page || current!==generation)return
            pendingGroups=0;errorText=error
            if(!error.length)groups=value
        })
    }
    function scan() {
        var dialog=pageStack.push(Qt.resolvedUrl("ScanDirectoryDialog.qml"),{directory:info.directory})
        dialog.accepted.connect(function(){
            app.call("localScan",{directory:dialog.pathText},function(value,error){
                if(!page)return
                errorText=error
                if(!error.length)load()
            })
        })
    }
    function songs(view,id,keyword,title) {
        pageStack.push(Qt.resolvedUrl("LibrarySongsPage.qml"),{pageTitle:title,requestMethod:"localSongs",requestArgs:{view:view,id:id,keyword:keyword}})
    }
    Component.onCompleted: load()
    onStatusChanged: {if(status===PageStatus.Active)load()}
    Component.onDestruction: {++generation;if(pending)delete app.callbacks[pending];if(pendingGroups)delete app.callbacks[pendingGroups]}
    Timer { interval:1500;repeat:true;running:page.info.scanning && page.status===PageStatus.Active && Qt.application.active;onTriggered:page.load() }
    SilicaListView {
        id:list;anchors.fill:parent;anchors.bottomMargin:mini.visible?mini.height:0;model:page.groups
        PullDownMenu {
            MenuItem {text:"选择目录并扫描";enabled:core.ready && !page.info.scanning;onClicked:page.scan()}
            MenuItem {text:"刷新索引";enabled:!page.info.scanning;onClicked:page.load()}
        }
        header:Column {
            width:parent.width
            AppHeader {title:"本地音乐";description:page.info.count+" 首"}
            Label {x:Theme.horizontalPageMargin;width:parent.width-2*x;text:page.info.directory;wrapMode:Text.Wrap;color:Theme.secondaryColor;font.pixelSize:Theme.fontSizeSmall}
            Label {x:Theme.horizontalPageMargin;width:parent.width-2*x;text:page.errorText || page.info.error;visible:!!text.length;wrapMode:Text.Wrap;color:Theme.highlightColor}
            BusyIndicator {anchors.horizontalCenter:parent.horizontalCenter;running:page.info.scanning;visible:running}
            Label {x:Theme.horizontalPageMargin;width:parent.width-2*x;text:page.info.scanning?"正在扫描…":"解析 "+page.info.parsed+" · 复用 "+page.info.reused+" · 索引移除 "+page.info.removed;color:Theme.secondaryColor;font.pixelSize:Theme.fontSizeSmall}
            Button {anchors.horizontalCenter:parent.horizontalCenter;text:page.info.count>0?"全部歌曲":"选择目录并扫描";enabled:core.ready && !page.info.scanning;onClicked:{if(page.info.count>0)page.songs("songs","","","本地歌曲");else page.scan()}}
            SearchField {width:parent.width;placeholderText:"搜索本地歌曲、歌手或专辑";EnterKey.iconSource:"image://theme/icon-m-search";EnterKey.onClicked:{focus=false;page.songs("songs","",text,"本地搜索")}}
            ComboBox {id:grouping;currentIndex:page.groupIndex;width:parent.width;label:"分组";menu:ContextMenu{MenuItem{text:"专辑"}MenuItem{text:"歌手"}MenuItem{text:"文件夹"}} onCurrentIndexChanged:{page.groupIndex=currentIndex;page.loadGroups()}}
        }
        delegate:ListItem {
            width:list.width;contentHeight:Theme.itemSizeMedium
            Column {x:Theme.horizontalPageMargin;width:parent.width-2*x;anchors.verticalCenter:parent.verticalCenter
                Label{width:parent.width;text:modelData.title;textFormat:Text.PlainText;truncationMode:TruncationMode.Fade}
                Label{width:parent.width;text:modelData.count+" 首 · "+modelData.subtitle;truncationMode:TruncationMode.Fade;color:Theme.secondaryColor;font.pixelSize:Theme.fontSizeSmall}
            }
            onClicked:page.songs(["albums","artists","folders"][page.groupIndex],modelData.id,"",modelData.title)
        }
        VerticalScrollDecorator{}
    }
    AppFooter{id:mini;anchors.bottom:parent.bottom}
}
