import QtQuick
import QtQuick.Layouts

// Right column: Preview (what Space sends next) and Live (what is on the
// Slide layer), transport buttons, the layer stack, and transition and
// message controls.
Rectangle {
    id: root

    required property QtObject controller
    readonly property string transitionName: {
        const list = root.controller.transitions
        for (let i = 0; i < list.length; ++i)
            if (list[i].key === root.controller.transition)
                return list[i].name
        return qsTr("Cut")
    }

    // Preview, Live, the Previous / Next buttons and the Transition row always
    // fit: the two frames shrink to the room left (about 210 px of labels,
    // gaps, buttons and the transition row); the layers scroll below them.
    readonly property real frameWidth: Math.max(160, Math.min(flick.width, (root.height - 32 - 210) / 2 * 16 / 9))
    signal messageRequested()
    // Escape: closes the Transition list (true if it was open).
    readonly property var openMenus: transitionMenu.visible ? [transitionMenu] : []
    function closeMenus() {
        const open = transitionMenu.visible
        transitionMenu.visible = false
        return open
    }
    // An output on another screen than it was given (from the Screens setup).
    property string placementWarning: ""

    color: Theme.panel

    Rectangle {
        width: 1
        height: parent.height
        color: Theme.border
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: 16
        anchors.leftMargin: 17
        clip: true
        contentHeight: column.height
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            width: parent.width
            spacing: 14

            Column {
                width: parent.width
                spacing: 6

                SectionLabel {
                    text: qsTr("PREVIEW (NEXT)")
                    color: Theme.link
                }
                SlideFrame {
                    objectName: "previewFrame"
                    width: root.frameWidth
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.controller.previewText
                    slideStyle: root.controller.previewStyle
                    footer: root.controller.previewFooter
                    poster: root.controller.previewPoster
                    timerText: root.controller.previewTimerText
                    elements: root.controller.previewElements
                    background: root.controller.previewBackground
                    frameColor: Theme.next

                    // The next video could not be opened.
                    Rectangle {
                        objectName: "previewProblem"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 3
                        height: previewProblemText.implicitHeight + 8
                        visible: root.controller.previewProblem !== ""
                        color: "#8A1C1C"
                        LText {
                            id: previewProblemText
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 6
                            text: root.controller.previewProblem
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                            color: "#FFFFFF"
                            font.pixelSize: 11
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 6

                Item {
                    width: parent.width
                    height: liveTag.height

                    SectionLabel {
                        id: liveTag
                        text: qsTr("LIVE")
                        font.bold: true
                        color: Theme.live
                    }
                    LText {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.controller.liveLabel
                        font.pixelSize: 12
                        color: Theme.textMuted
                    }
                }
                SlideFrame {
                    objectName: "liveFrame"
                    width: root.frameWidth
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.controller.liveText
                    slideStyle: root.controller.liveStyle
                    footer: root.controller.liveFooter
                    poster: root.controller.livePoster
                    timerText: root.controller.liveTimerText
                    elements: root.controller.liveElements
                    background: root.controller.liveBackground
                    frameColor: Theme.live
                    frameWidth: 3
                    fontSize: 15
                    bold: true

                    // What the audience really sees over the slide: Black,
                    // the Logo, or nothing at all when the output is off.
                    Rectangle {
                        objectName: "liveOverride"
                        anchors.fill: parent
                        anchors.margins: 3
                        visible: root.controller.blackOn || root.controller.logoOn
                        color: "#000000"
                        LText {
                            anchors.centerIn: parent
                            text: root.controller.blackOn ? qsTr("Black (F7)") : qsTr("Logo (F6)")
                            color: Theme.textMuted
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                    }
                    // A video on the screen that the output could not play.
                    Rectangle {
                        objectName: "liveProblem"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 3
                        height: liveProblemText.implicitHeight + 8
                        visible: root.controller.liveProblem !== ""
                        color: "#8A1C1C"
                        LText {
                            id: liveProblemText
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 6
                            text: root.controller.liveProblem
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                            color: "#FFFFFF"
                            font.pixelSize: 11
                        }
                    }
                    // The output is not where it was put.
                    Rectangle {
                        objectName: "livePlacement"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 3
                        height: placementText.implicitHeight + 8
                        visible: root.placementWarning !== "" && root.controller.outputConnected
                        color: Theme.warnBackground
                        LText {
                            id: placementText
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 6
                            text: root.placementWarning + " " + qsTr("See Plan ▾ → Screens…")
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                            color: Theme.warnText
                            font.pixelSize: 11
                        }
                    }
                    // Live goes to another computer's screen (Share panel).
                    Rectangle {
                        objectName: "liveRemoteScreen"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 3
                        height: Math.max(24, remoteText.implicitHeight + 6)
                        visible: root.controller.remoteScreen !== ""
                        color: "#CC17325A"
                        LText {
                            id: remoteText
                            anchors.centerIn: parent
                            width: parent.width - 8
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: qsTr("On %1's screen").arg(root.controller.remoteScreen)
                            color: "#FFFFFF"
                            font.pixelSize: 11
                        }
                    }
                    Rectangle {
                        objectName: "liveOutputOff"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 3
                        height: Math.max(24, outputOffText.implicitHeight + 6)
                        visible: !root.controller.outputConnected && root.controller.remoteScreen === ""
                        color: Theme.warnBackground
                        LText {
                            id: outputOffText
                            anchors.centerIn: parent
                            width: parent.width - 8
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: qsTr("Output off: the audience sees nothing")
                            color: Theme.warnText
                            font.pixelSize: 11
                        }
                    }
                }
                // The video on the screen: pause or play it, and drag along
                // the bar to jump. Its output reports where it is about
                // once a second; in between the bar moves on its own.
                RowLayout {
                    id: transport
                    objectName: "videoTransport"
                    visible: root.controller.liveVideo
                    width: parent.width
                    spacing: 8
                    property real position: root.controller.liveVideoPositionMs
                    readonly property real duration: root.controller.liveVideoDurationMs
                    Connections {
                        target: root.controller
                        function onVideoPositionChanged() { transport.position = root.controller.liveVideoPositionMs }
                    }
                    Timer {
                        interval: 100
                        repeat: true
                        running: transport.visible && root.controller.liveVideoPlaying && !seekMouse.pressed
                        onTriggered: transport.position = Math.min(transport.duration, transport.position + interval)
                    }
                    function clock(ms) {
                        const s = Math.max(0, Math.floor(ms / 1000))
                        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
                    }

                    LButton {
                        objectName: "videoPlayPause"
                        implicitHeight: 32
                        radius: 6
                        fontSize: 12
                        text: root.controller.liveVideoPlaying ? qsTr("Pause") : qsTr("Play")
                        onClicked: root.controller.toggleVideoPlaying()
                    }
                    Item {
                        objectName: "videoSeekBar"
                        Layout.fillWidth: true
                        implicitHeight: 32
                        Accessible.role: Accessible.Slider
                        Accessible.name: qsTr("Video position")
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: 6
                            radius: 3
                            color: Theme.raised
                            Rectangle {
                                width: transport.duration > 0 ? parent.width * Math.min(1, transport.position / transport.duration) : 0
                                height: parent.height
                                radius: 3
                                color: Theme.live
                            }
                        }
                        MouseArea {
                            id: seekMouse
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            preventStealing: true
                            function at(x) { return Math.max(0, Math.min(1, x / width)) * transport.duration }
                            onPressed: mouse => transport.position = at(mouse.x)
                            onPositionChanged: mouse => { if (pressed) transport.position = at(mouse.x) }
                            onReleased: mouse => root.controller.seekVideo(at(mouse.x))
                        }
                    }
                    LText {
                        objectName: "videoTime"
                        text: transport.clock(transport.position) + " / " + transport.clock(transport.duration)
                        font.pixelSize: 11
                        font.features: { "tnum": 1 }
                        color: Theme.textMuted
                    }
                }
                Row {
                    width: parent.width
                    spacing: 8

                    LButton {
                        objectName: "previousSlide"
                        width: (parent.width - parent.spacing) / 3
                        implicitHeight: 44
                        fontSize: 13
                        text: qsTr("Previous (←)")
                        onClicked: root.controller.previous()
                    }
                    LButton {
                        objectName: "nextSlide"
                        width: (parent.width - parent.spacing) * 2 / 3
                        implicitHeight: 44
                        fontSize: 13
                        variant: "primary"
                        text: qsTr("Next slide (Space)")
                        onClicked: root.controller.next()
                    }
                }
            }
            RowLayout {
                width: parent.width
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 4

                    LText {
                        text: qsTr("Transition")
                        font.pixelSize: 12
                        color: Theme.textMuted
                    }
                    Rectangle {
                        objectName: "transitionBox"
                        Layout.fillWidth: true
                        implicitHeight: 36
                        radius: 6
                        color: transitionArea.containsMouse ? Theme.selected : Theme.raised
                        border.color: transitionMenu.visible ? Theme.live : Theme.border
                        Accessible.role: Accessible.ComboBox
                        Accessible.name: qsTr("Transition: %1").arg(root.transitionName)

                        LText {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.right: arrow.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.controller.transition === "cut" ? root.transitionName
                                  : qsTr("%1 · %2 s").arg(root.transitionName).arg((root.controller.transitionMs / 1000).toFixed(1))
                        }
                        LText {
                            id: arrow
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: "▾"
                            color: Theme.textMuted
                        }
                        MouseArea {
                            id: transitionArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: transitionMenu.visible = !transitionMenu.visible
                        }
                    }
                }
                LButton {
                    objectName: "showMessageButton"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignBottom
                    radius: 6
                    text: qsTr("Show message…")
                    onClicked: root.messageRequested()
                }
            }

            Column {
                width: parent.width
                spacing: 4

                SectionLabel {
                    text: qsTr("LAYERS")
                    bottomPadding: 4
                }
                Repeater {
                    model: root.controller.layers
                    delegate: Rectangle {
                        id: layerRow
                        required property var modelData
                        width: parent ? parent.width : 0
                        height: 34
                        radius: 6
                        color: Theme.raised
                        Accessible.role: Accessible.StaticText
                        Accessible.name: modelData.name + ": " + modelData.value

                        LText {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: layerRow.modelData.name
                        }
                        LText {
                            anchors.right: audioStop.visible ? audioStop.left : parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, parent.width * 0.6)
                            horizontalAlignment: Text.AlignRight
                            text: layerRow.modelData.value
                            color: layerRow.modelData.active ? Theme.text : Theme.textMuted
                        }
                        // Music beside the slides: stopped here (or Clear All).
                        LButton {
                            id: audioStop
                            objectName: layerRow.modelData.key === "audio" ? "audioStop" : ""
                            visible: layerRow.modelData.key === "audio" && layerRow.modelData.active
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            implicitHeight: 26
                            radius: 6
                            fontSize: 11
                            text: qsTr("Stop")
                            onClicked: root.controller.clearAudio()
                        }
                    }
                }
            }

        }
    }

    // The transitions, and how long they take (opens from the Transition box).
    Rectangle {
        id: transitionMenu
        objectName: "transitionMenu"
        visible: false
        z: 20
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        height: menuColumn.implicitHeight + 16
        radius: 8
        color: Theme.window
        border.color: Theme.border

        Column {
            id: menuColumn
            anchors.fill: parent
            anchors.margins: 8
            spacing: 2

            Repeater {
                model: root.controller.transitions
                delegate: Rectangle {
                    required property var modelData
                    objectName: "transition-" + modelData.key
                    width: menuColumn.width
                    height: 30
                    radius: 6
                    color: modelData.key === root.controller.transition ? Theme.selected
                           : optionArea.containsMouse ? Theme.raised : "transparent"
                    LText {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData.name
                        font.weight: parent.modelData.key === root.controller.transition ? Font.DemiBold : Font.Normal
                    }
                    MouseArea {
                        id: optionArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.controller.transition = parent.modelData.key
                            if (parent.modelData.key === "cut")
                                transitionMenu.visible = false
                        }
                    }
                }
            }
            Row {
                visible: root.controller.transition !== "cut"
                spacing: 4
                topPadding: 6
                Repeater {
                    model: [{ ms: 300, name: qsTr("Fast") }, { ms: 600, name: qsTr("Normal") }, { ms: 1000, name: qsTr("Slow") }]
                    delegate: LButton {
                        required property var modelData
                        objectName: "transitionSpeed-" + modelData.ms
                        text: modelData.name
                        implicitHeight: 28
                        radius: 6
                        fontSize: 11
                        variant: root.controller.transitionMs === modelData.ms ? "primary" : "default"
                        onClicked: {
                            root.controller.transitionMs = modelData.ms
                            transitionMenu.visible = false
                        }
                    }
                }
            }
        }
    }

    // When the column is taller than the window: a thin bar shows it scrolls.
    Rectangle {
        objectName: "previewLiveScroll"
        visible: flick.contentHeight > flick.height + 1
        anchors.right: parent.right
        anchors.rightMargin: 3
        y: flick.y + flick.visibleArea.yPosition * flick.height
        width: 4
        height: Math.max(24, flick.visibleArea.heightRatio * flick.height)
        radius: 2
        color: Theme.border
    }
}
