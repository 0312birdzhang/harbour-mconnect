import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id:shell
    objectName:"mainShell"
    property int startIndex:0
    property int currentTab:0
    property var savedIndexes:[0,0,0,0]
    property int currentSlide:currentTab===0 || currentTab===2?0:groupLoader.item?groupLoader.item.currentIndex:0
    property string activeRoot:["exploreRoot","discoveryRoot","libraryRoot","downloadsRoot"][currentTab]
    property int slideCount:currentTab===0 || currentTab===2?1:currentTab===3?3:4
    property alias bottomBar:footer
    property alias commonMenu:sharedMenu
    property alias outerFlickable:outerScroll
    property Item currentPane:currentTab===0 || currentTab===2?groupLoader.item:groupLoader.item?(groupLoader.item.currentPane || null):null
    function selectTab(index) {
        if(index<0 || index>3)return false
        if (index !== currentTab) {
            if (currentTab === 0 && currentPane) currentPane.saveState()
            savedIndexes[currentTab] = currentTab === 0 && currentPane ? currentPane.platformIndex : currentSlide
            currentTab = index
        }
        app.tabIndex=index
        return true
    }
    function select(index) {return currentTab===0 || currentTab===2?index===0:groupLoader.item?groupLoader.item.select(index):false}
    function step(direction) {if((currentTab===1 || currentTab===3) && groupLoader.item)groupLoader.item.step(direction)}
    Component.onCompleted:{app.homePage=shell;selectTab(startIndex)}
    Component.onDestruction:{if(app.homePage===shell)app.homePage=null}
    SilicaFlickable {
        id:outerScroll
        objectName:"rootPageFlickable"
        anchors.fill:parent
        anchors.bottomMargin:footer.visible?footer.height:0
        contentHeight:height
        clip:true
    Column {
        id:header
        width:parent.width
        AppHeader {title:["探索","发现","音乐库","下载"][shell.currentTab];tools:true}
        PlatformTabs {
            width:parent.width
            visible:shell.currentTab===1 || shell.currentTab===3
            names:shell.currentTab===3?["全部","下载中","已完成"]:["网易云","QQ音乐","酷狗","哔哩哔哩"]
            currentIndex:shell.currentSlide
            onSelected:app.selectSlide(index)
        }
    }
    Loader {
        id:groupLoader
        anchors.top:header.bottom;anchors.bottom:parent.bottom
        width:parent.width
        clip:true
        sourceComponent:shell.currentTab===0?exploreComponent:shell.currentTab===1?discoveryGroup:shell.currentTab===2?libraryComponent:downloadsGroup
    }
    PullDownMenu {
        id:sharedMenu
        objectName:"sharedRootMenu"
        enabled:shell.currentTab!==2 && shell.status===PageStatus.Active
        visible:shell.currentTab!==2
        MenuItem{text:"音乐库";visible:shell.currentTab===0;onClicked:app.switchTab(2)}
        MenuItem{text:"正在播放";visible:shell.currentTab===0;enabled:!!core.state.song;onClicked:app.openPlayer()}
        MenuItem{text:"播放队列";visible:shell.currentTab===0;onClicked:app.openQueue()}
        MenuItem{text:"刷新推荐";visible:shell.currentTab===1;enabled:shell.currentPane && !shell.currentPane.pending;onClicked:if(shell.currentPane)shell.currentPane.load(true)}
        MenuItem{text:"缓存设置";visible:shell.currentTab===3;onClicked:pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))}
        MenuItem{text:shell.currentPane && shell.currentTab===3 && shell.currentPane.snapshot.paused?"继续下载队列":"暂停下载队列";visible:shell.currentTab===3;enabled:shell.currentPane && !shell.currentPane.acting;onClicked:if(shell.currentPane)shell.currentPane.control(shell.currentPane.snapshot.paused?"resumeQueue":"pauseQueue","")}
        MenuItem{text:"刷新";visible:shell.currentTab===3;enabled:shell.currentPane && !shell.currentPane.pending;onClicked:if(shell.currentPane)shell.currentPane.load()}
    }
    }
    // All page Components belong to this stable shell context.
    Component{id:discoveryGroup;GroupSlideshow{count:4;startIndex:shell.savedIndexes[1];active:shell.status===PageStatus.Active;bodyComponent:discoveryComponent}}
    Component{id:downloadsGroup;GroupSlideshow{count:3;startIndex:shell.savedIndexes[3];active:shell.status===PageStatus.Active;bodyComponent:downloadsComponent}}
    Component{id:exploreComponent;SearchPage{embedded:true;active:shell.currentTab===0 && shell.status===PageStatus.Active;platformIndex:shell.savedIndexes[0]}}
    Component{id:discoveryComponent;DiscoveryPage{embedded:true;active:parent?parent.paneActive:false;platformIndex:parent?parent.platformIndex:0}}
    Component{id:libraryComponent;LibraryPage{embedded:true;active:shell.status===PageStatus.Active}}
    Component{id:downloadsComponent;DownloadsPage{embedded:true;active:parent?parent.paneActive:false;filterIndex:parent?parent.filterIndex:0}}
    AppFooter{id:footer;anchors.bottom:parent.bottom}
}
