import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// The stage display (batch 13), laid out as design/mockups/Stage.html: the
// group and item on top with the service timer and the time of day, the
// slide on air (NOW) beside the one Space sends next (NEXT) and the song's
// arrangement, and a message to the stage at the bottom.
//
// It is drawn from the stage state the operator UI sends (see protocol.h);
// the clock and the timer are counted here, from this process's own clock.
// Sizes are for 720p and scale with the window.
//
// Layouts: "default" shows everything; "lyrics" shows NOW and NEXT large
// with the message; "clock" shows the time and timer large with NOW and the
// message.
Window {
    id: stageWindow

    required property QtObject scene
    readonly property var stage: scene.stage
    readonly property string layout: stage.layout || "default"
    readonly property real unit: height / 720
    readonly property var timer: stage.timer || null

    visible: false // placed on a screen by the C++ side, then shown
    title: "Lumen Stage"
    color: "#000000"

    function px(value) { return Math.max(1, Math.round(value * unit)) }
    function clockText(wallMs) {
        const now = new Date(wallMs)
        return Qt.formatTime(now, "hh:mm")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: px(36)
        anchors.rightMargin: px(36)
        anchors.topMargin: px(28)
        anchors.bottomMargin: px(28)
        spacing: px(20)

        // ---- top: group, item, timer and clock ----
        RowLayout {
            Layout.fillWidth: true
            visible: stageWindow.layout !== "lyrics"
            spacing: px(24)

            RowLayout {
                spacing: px(14)
                Layout.fillWidth: true
                visible: stageWindow.layout === "default"
                Rectangle {
                    visible: (stageWindow.stage.group || "") !== ""
                    implicitWidth: groupText.implicitWidth + px(24)
                    implicitHeight: groupText.implicitHeight + px(12)
                    radius: px(6)
                    color: "#1C3A37"
                    Text {
                        id: groupText
                        objectName: "stageGroup"
                        anchors.centerIn: parent
                        text: (stageWindow.stage.group || "").toUpperCase()
                        color: "#A6E6DC"
                        font.family: "Inter"
                        font.pixelSize: px(18)
                        font.weight: Font.Bold
                    }
                }
                Text {
                    objectName: "stageItem"
                    Layout.fillWidth: true
                    text: [stageWindow.stage.item || "", stageWindow.stage.detail || ""].filter(s => s !== "").join(" · ")
                    color: "#A3ACBA"
                    elide: Text.ElideRight
                    font.family: "Inter"
                    font.pixelSize: px(18)
                }
            }
            Item { Layout.fillWidth: stageWindow.layout !== "default" }

            ColumnLayout {
                visible: stageWindow.timer !== null
                spacing: 0
                Text {
                    Layout.alignment: Qt.AlignRight
                    text: stageWindow.timer ? stageWindow.timer.label.toUpperCase() : ""
                    color: "#A3ACBA"
                    font.family: "Inter"
                    font.pixelSize: px(stageWindow.layout === "clock" ? 22 : 13)
                    font.letterSpacing: px(1)
                }
                Text {
                    objectName: "stageTimer"
                    Layout.alignment: Qt.AlignRight
                    text: stageWindow.timer ? stageWindow.scene.timerText(stageWindow.timer.spec, stageWindow.scene.wallMs) : ""
                    color: "#F2C38B"
                    font.family: "Inter"
                    font.pixelSize: px(stageWindow.layout === "clock" ? 150 : 34)
                    font.weight: Font.Bold
                    font.features: { "tnum": 1 }
                }
            }
            ColumnLayout {
                spacing: 0
                Text {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("TIME")
                    color: "#A3ACBA"
                    font.family: "Inter"
                    font.pixelSize: px(stageWindow.layout === "clock" ? 22 : 13)
                    font.letterSpacing: px(1)
                }
                Text {
                    objectName: "stageClock"
                    Layout.alignment: Qt.AlignRight
                    text: stageWindow.clockText(stageWindow.scene.wallMs)
                    color: "#FFFFFF"
                    font.family: "Inter"
                    font.pixelSize: px(stageWindow.layout === "clock" ? 150 : 34)
                    font.weight: Font.Bold
                    font.features: { "tnum": 1 }
                }
            }
        }

        // ---- middle: NOW and NEXT ----
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: px(28)

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 3
                spacing: px(10)
                Text {
                    text: qsTr("NOW")
                    color: "#F08A24"
                    font.family: "Inter"
                    font.pixelSize: px(14)
                    font.weight: Font.Bold
                    font.letterSpacing: px(1.4)
                }
                Text {
                    objectName: "stageNow"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: stageWindow.stage.now || ""
                    color: "#FFFFFF"
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignTop
                    fontSizeMode: Text.Fit
                    minimumPixelSize: px(20)
                    font.family: "Inter"
                    font.pixelSize: px(stageWindow.layout === "lyrics" ? 72 : stageWindow.layout === "clock" ? 36 : 54)
                    font.weight: Font.Bold
                    lineHeight: 1.25
                }
            }

            Rectangle {
                visible: stageWindow.layout !== "clock"
                Layout.fillHeight: true
                implicitWidth: px(2)
                color: "#2B303A"
            }

            ColumnLayout {
                visible: stageWindow.layout !== "clock"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 2
                spacing: px(10)
                Text {
                    text: (stageWindow.stage.nextGroup || "") !== "" ? qsTr("NEXT · %1").arg(stageWindow.stage.nextGroup.toUpperCase())
                                                                     : qsTr("NEXT")
                    color: "#8DB4FF"
                    font.family: "Inter"
                    font.pixelSize: px(14)
                    font.weight: Font.Bold
                    font.letterSpacing: px(1.4)
                }
                Text {
                    objectName: "stageNext"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: stageWindow.stage.next || ""
                    color: "#C3CAD6"
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignTop
                    fontSizeMode: Text.Fit
                    minimumPixelSize: px(16)
                    font.family: "Inter"
                    font.pixelSize: px(stageWindow.layout === "lyrics" ? 48 : 32)
                    font.weight: Font.DemiBold
                    lineHeight: 1.3
                }
                Text {
                    visible: stageWindow.layout === "default" && arrangementRow.count > 0
                    text: qsTr("ARRANGEMENT")
                    color: "#A3ACBA"
                    font.family: "Inter"
                    font.pixelSize: px(14)
                    font.letterSpacing: px(1.1)
                }
                Flow {
                    visible: stageWindow.layout === "default"
                    Layout.fillWidth: true
                    spacing: px(8)
                    Repeater {
                        id: arrangementRow
                        model: stageWindow.stage.arrangement || []
                        delegate: Row {
                            id: chip
                            required property var modelData
                            required property int index
                            spacing: px(8)
                            Text {
                                visible: chip.index > 0
                                text: "·"
                                color: "#C3CAD6"
                                font.family: "Inter"
                                font.pixelSize: px(22)
                            }
                            Text {
                                objectName: "stageChip-" + chip.index
                                text: chip.modelData.label
                                color: chip.modelData.current ? "#FFFFFF" : "#C3CAD6"
                                font.family: "Inter"
                                font.pixelSize: px(22)
                                font.weight: chip.modelData.current ? Font.Bold : Font.Normal
                            }
                        }
                    }
                }
            }
        }

        // ---- bottom: a message to the stage ----
        Rectangle {
            Layout.fillWidth: true
            visible: (stageWindow.stage.message || "") !== ""
            implicitHeight: messageRow.implicitHeight + px(28)
            radius: px(10)
            color: "#3A2A1E"
            RowLayout {
                id: messageRow
                anchors.fill: parent
                anchors.leftMargin: px(20)
                anchors.rightMargin: px(20)
                spacing: px(16)
                Text {
                    text: qsTr("MESSAGE")
                    color: "#F2C38B"
                    font.family: "Inter"
                    font.pixelSize: px(14)
                    font.weight: Font.Bold
                    font.letterSpacing: px(1.4)
                }
                Text {
                    objectName: "stageMessage"
                    Layout.fillWidth: true
                    text: stageWindow.stage.message || ""
                    color: "#FFE3C4"
                    wrapMode: Text.WordWrap
                    font.family: "Inter"
                    font.pixelSize: px(26)
                    font.weight: Font.DemiBold
                }
            }
        }
    }

    // While the operator sets up the screens: which screen this is.
    IdentifyLabel {
        identify: stageWindow.scene.identify
    }
}
