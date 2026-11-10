import QtQuick 2.0
import Sailfish.Silica 1.0
Dialog {
    id:dialog
    property string directory:""
    property alias pathText:path.text
    canAccept:path.text.trim().length>0
    Component.onCompleted:path.text=directory
    onStatusChanged:{if(status===PageStatus.Active && !validationMode)focusTimer.restart()}
    Timer{id:focusTimer;interval:120;onTriggered:{if(dialog.status===PageStatus.Active){path.forceActiveFocus();Qt.inputMethod.show()}}}
    Column{width:parent.width;spacing:Theme.paddingLarge
        DialogHeader{title:"扫描音乐目录"}
        TextField{id:path;width:parent.width;label:"目录路径";placeholderText:"~/Music";maximumLength:4096;inputMethodHints:Qt.ImhNoPredictiveText|Qt.ImhNoAutoUppercase;EnterKey.iconSource:"image://theme/icon-m-enter-accept";EnterKey.onClicked:dialog.accept()}
        Label{x:Theme.horizontalPageMargin;width:parent.width-2*x;text:"递归读取该目录中的音频，保存歌曲索引、封面和歌词。可切换到其他页面等待扫描完成。";wrapMode:Text.Wrap;color:Theme.secondaryColor}
    }
}
