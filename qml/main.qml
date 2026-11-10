import QtQuick 2.0
import Sailfish.Silica 1.0

ApplicationWindow {
    id: app
    property var callbacks: ({})
    property string errorText: ""
    property int tabIndex: 0
    property var searchState: ({})
    property var discoveryState: ({})
    property var searchStates: ({})
    property var discoveryRequests: ({})
    property var discoveryPlatforms: []
    property int discoveryPlatformRequest: 0
    property int discoveryGeneration: 0
    signal discoveryReady(string platform, var result, string error)
    signal discoveryPlatformsReady(var result, string error)
    signal discoveryReset()
    function ensureDiscoveryPlatforms() {
        if(discoveryPlatforms.length){discoveryPlatformsReady(discoveryPlatforms,"");return 0}
        if(discoveryPlatformRequest)return discoveryPlatformRequest
        discoveryPlatformRequest=call("discoveryPlatforms",{},function(value,error){
            discoveryPlatformRequest=0
            if(!error.length)discoveryPlatforms=value
            discoveryPlatformsReady(value,error)
        })
        return discoveryPlatformRequest
    }
    function requestRecommendations(platform,refresh) {
        if(!refresh && discoveryState[platform]){discoveryReady(platform,discoveryState[platform],"");return 0}
        if(discoveryRequests[platform])return discoveryRequests[platform]
        var generation=discoveryGeneration
        var request=call("discoverySongs",{platform:platform,kind:"daily",refresh:refresh},function(value,error){
            if(generation!==discoveryGeneration)return
            delete discoveryRequests[platform]
            if(!error.length)discoveryState[platform]=value
            discoveryReady(platform,value,error)
        })
        discoveryRequests[platform]=request
        return request
    }
    property Item homePage: null
    function switchTab(index) {
        if (index < 0 || index > 3 || pageStack.busy) return false
        Qt.inputMethod.hide()
        tabIndex = index
        var tab=index
        if(homePage) {pageStack.pop(homePage,PageStackAction.Immediate);homePage.selectTab(tab)}
        else pageStack.replaceAbove(null,Qt.resolvedUrl("MainShellPage.qml"),{startIndex:tab},PageStackAction.Immediate)
        return true
    }
    function selectSlide(index) {
        if(!homePage || pageStack.busy)return false
        Qt.inputMethod.hide()
        return homePage.select(index)
    }
    function openUnique(file,name,properties) {
        if(pageStack.busy)return
        var existing=pageStack.find(function(page){return page.objectName===name})
        if(existing){if(existing!==pageStack.currentPage)pageStack.pop(existing);return}
        pageStack.push(Qt.resolvedUrl(file),properties || {})
    }
    function openPlayer(){openUnique("PlayerPage.qml","playerPage",{})}
    function openQueue(){openUnique("QueuePage.qml","queuePage",{})}
    function openDownloads() {
        if (!switchTab(3)) return false
        return homePage ? homePage.select(0) : false
    }
    // Used only by the no-window target-device validator.
    function validationState() {
        if (!validationMode) return ({})
        function footer(item) {
            if (item.objectName === "appFooter") return item
            if (!item.children) return null
            for(var i=0;i<item.children.length;++i){var found=footer(item.children[i]);if(found)return found}
            return null
        }
        var current=pageStack.currentPage
        var bar=current===homePage?homePage.bottomBar:current?footer(current):null
        var pane=homePage?homePage.currentPane:null
        return {page:current===homePage?homePage.activeRoot:current?current.objectName:"",depth:pageStack.depth,busy:pageStack.busy,tab:tabIndex,footer:!!bar,barHeight:bar?bar.height:0,slide:homePage?homePage.currentSlide:-1,slideCount:homePage?homePage.slideCount:0,pane:pane?pane.objectName:"",platform:pane && typeof pane.platformIndex==="number"?pane.platformIndex:-1,filter:pane && typeof pane.filterIndex==="number"?pane.filterIndex:-1,sharedMenu:homePage?(homePage.commonMenu.flickable===homePage.outerFlickable):false}
    }
    function validationChild() {
        if(validationMode)pageStack.push(Qt.resolvedUrl("LibrarySongsPage.qml"),{},PageStackAction.Immediate)
    }
    function validationStep(direction){if(validationMode && homePage)homePage.step(direction)}
    function call(method, args, callback) {
        var id = core.request(method, args || {})
        if (callback) callbacks[id] = callback
        return id
    }
    function time(ms) {
        var seconds = Math.max(0, Math.floor((ms || 0) / 1000))
        return Math.floor(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") + seconds % 60
    }
    Connections {
        target: core
        onAccountsChanged:{++app.discoveryGeneration;app.discoveryState=({});app.discoveryRequests=({});app.discoveryReset()}
        onCompleted: {
            var callback = app.callbacks[id]
            if (callback) { delete app.callbacks[id]; callback(value, error) }
            else if (error.length && core.ready) app.errorText = error
        }
    }
    initialPage: Component { MainShellPage {} }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations
    _defaultPageOrientations: Orientation.All
}
