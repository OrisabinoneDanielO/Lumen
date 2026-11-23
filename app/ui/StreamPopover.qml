import QtQuick
import QtQuick.Layouts

// The stream output (batch 15): Slide and Props on transparency, for
// streaming software and switchers, as key and fill (two windows or screens)
// and, when the experimental NDI setting is on, as an NDI source.
Rectangle {
    id: root

    required property QtObject stream
    signal closeRequested()

    width: 420
    height: column.implicitHeight + 24
    radius: 10
    color: Theme.window
    border.color: Theme.border

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            SectionLabel {
                Layout.fillWidth: true
                text: qsTr("STREAM OUTPUT")
            }
            LButton {
                objectName: "streamToggle"
                text: root.stream.connected ? qsTr("Turn off") : qsTr("Turn on")
                variant: root.stream.connected ? "default" : "primary"
                implicitHeight: 28
                radius: 6
                onClicked: root.stream.toggle()
            }
            LButton {
                text: "✕"
                variant: "ghost"
                implicitHeight: 28
                onClicked: root.closeRequested()
            }
        }
        LText {
            Layout.fillWidth: true
            text: qsTr("Only the slide text and props, on transparency, for streaming software and switchers.")
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            font.pixelSize: 11
            color: Theme.textMuted
        }
        LText {
            Layout.fillWidth: true
            visible: root.stream.status !== ""
            text: root.stream.status
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            font.pixelSize: 11
            color: Theme.live
        }
        RowLayout {
            Layout.fillWidth: true
            LButton {
                objectName: "streamKeyFill"
                text: root.stream.keyFill ? qsTr("Key and fill ✓") : qsTr("Key and fill")
                variant: root.stream.keyFill ? "primary" : "default"
                implicitHeight: 30
                radius: 6
                onClicked: root.stream.keyFill = !root.stream.keyFill
            }
            LButton {
                objectName: "streamNdi"
                visible: root.stream.ndiAvailable
                text: root.stream.ndi ? qsTr("NDI ✓") : qsTr("NDI")
                variant: root.stream.ndi ? "primary" : "default"
                implicitHeight: 30
                radius: 6
                onClicked: root.stream.ndi = !root.stream.ndi
            }
        }
        SearchField {
            objectName: "streamNdiName"
            inputName: "streamNdiNameText"
            visible: root.stream.ndiAvailable && root.stream.ndi
            Layout.fillWidth: true
            text: root.stream.ndiName
            placeholder: qsTr("NDI source name")
            onAccepted: root.stream.ndiName = text
        }
        LText {
            Layout.fillWidth: true
            visible: !root.stream.ndiAvailable
            text: qsTr("NDI is experimental: turn on the experimental/ndi setting and install the NDI Runtime to try it.")
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            font.pixelSize: 11
            color: Theme.textMuted
        }
    }
}
