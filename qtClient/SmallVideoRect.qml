import QtQuick
import QtQuick.Layouts
import QtMultimedia

Rectangle{
    color: "red"

    property QtObject videoWindow;

    objectName: "VideoRect"
    Layout.preferredHeight: 120
    Layout.preferredWidth: 160
    Layout.margins: 10
    Layout.alignment: Qt.AlignHCenter
    WindowContainer{
        objectName: "videoOutput"
        width: parent.width
        height: parent.height
        window: videoWindow
        anchors.fill: parent
    }
}
