import QtQuick
import QtQuick.Layouts
import QtMultimedia

Rectangle{
    color: "red"
    objectName: "VideoRect"
    Layout.preferredHeight: 120
    Layout.preferredWidth: 160
    Layout.margins: 10
    Layout.alignment: Qt.AlignHCenter
    VideoOutput{
        objectName: "videoOutput"
        fillMode: VideoOutput.PreserveAspectFit
        anchors.fill: parent
    }
}
