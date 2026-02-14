import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

Window {
    width: 640
    height: 480
    minimumHeight: 400
    minimumWidth: 600
    visible: true
    objectName: "wnd"
    title: qsTr("Hello World")
    RowLayout{
        anchors.fill: parent
        objectName: "MainArea"
        ColumnLayout{
            Layout.fillWidth: true
            Layout.horizontalStretchFactor: 4
            objectName: "VideoArea"
            ScrollView{
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                ScrollBar.horizontal.policy: ScrollBar.AsNeeded

                RowLayout{
                    Item{
                        Layout.preferredHeight: 120
                        Layout.preferredWidth: 0
                    }
                    id: videoStreams
                    objectName: "VideoStreams"
                }
            }
            Rectangle{
                color: "gray"
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }

        ColumnLayout{
            Layout.minimumWidth: 200
            Layout.maximumWidth: 500
            // Layout.preferredWidth: 200
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.horizontalStretchFactor: 1

            TextField{
                id: roomId
                Layout.fillWidth: true
                placeholderText: "Enter room id"
            }

            Button{
                id: connect
                visible: true
                text: "connect"
                onClicked: {
                    console.log("User entered:", roomId.text)
                    disconect.visible = !disconect.visible
                    videoSource.visible = !videoSource.visible
                    chat.visible = !chat.visible
                    connect.visible = !connect.visible
                    roomId.visible = !roomId.visible

                    conference_client.connectClient("ws://localhost:8085/ws",roomId.text)
                }
            }

            ComboBox {
                id: videoSource
                visible: false
                Layout.fillWidth: true
                model: ["Video from disk", "Webcamera"]
                currentIndex: 1
                onActivated: function(index) {
                    console.log("Video source:", model[index])
                }
            }

            Item {
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.minimumHeight: 0

                ListView {
                    id: chat
                    anchors.fill: parent

                    model: chatModel
                    spacing: 8
                    clip: true

                    delegate: Rectangle {
                        width: chat.width
                        height: textItem.paintedHeight + 20
                        color: "transparent"

                        Rectangle {
                            anchors {
                                right: mine ? parent.right : undefined
                                left: mine ? undefined : parent.left
                                margins: 10
                            }

                            radius: 10
                            color: mine ? "#4CAF50" : "#dddddd"
                            width: Math.min(textItem.paintedWidth + 20, chat.width * 0.75)
                            height: textItem.paintedHeight + 20

                            Text {
                                id: textItem
                                text: message
                                wrapMode: Text.Wrap
                                anchors.centerIn: parent
                                color: mine ? "white" : "black"
                            }
                        }
                    }

                    // Auto-scroll to bottom
                    onCountChanged: positionViewAtEnd()
                }
            }

            Button{
                id: disconect
                visible: false
                text: "Disconnect"
                onClicked: {
                    console.log("Disconnect")
                    disconect.visible = !disconect.visible
                    videoSource.visible = !videoSource.visible
                    chat.visible = !chat.visible
                    connect.visible = !connect.visible
                    roomId.visible = !roomId.visible
                }
            }
        }
    }
}
