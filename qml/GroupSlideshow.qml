import QtQuick 2.0
import Sailfish.Silica 1.0

Item {
    id: group
    property int count: 4
    property int startIndex: 0
    property bool active: true
    property Component bodyComponent
    property int currentIndex: slides.currentIndex
    property Item currentPane: slides.currentItem ? slides.currentItem.pane : null
    function select(index) {if(index<0 || index>=count)return false;slides.currentIndex=index;return true}
    function step(direction) {if(direction>0)slides.incrementCurrentIndex();else slides.decrementCurrentIndex()}
    Component.onCompleted: select(startIndex)
    SlideshowView {
        id:slides
        objectName:"groupSlideshow"
        anchors.fill:parent
        clip:true
        model:group.count
        itemWidth:width;itemHeight:height
        highlightMoveDuration:validationMode?0:250
        delegate:Item {
            id:frame
            property int paneIndex:index
            property alias pane:content.item
            width:slides.itemWidth;height:slides.itemHeight
            clip:true
            Loader {
                id:content
                anchors.fill:parent
                property int platformIndex:frame.paneIndex
                property int filterIndex:frame.paneIndex
                property bool paneActive:group.active && slides.currentIndex===frame.paneIndex
                sourceComponent:group.bodyComponent
            }
        }
    }
}
