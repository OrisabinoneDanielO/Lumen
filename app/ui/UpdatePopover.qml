import QtQuick
import QtQuick.Layouts

// Updates (optional, off until asked): checks whether a newer Lumen has been
// published and, if so, opens its download page. Lumen never downloads or
// installs anything by itself.
Rectangle {
    id: root

    required property QtObject updates
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
                text: qsTr("UPDATES")
            }
            LButton {
                text: "✕"
                variant: "ghost"
                implicitHeight: 28
                onClicked: root.closeRequested()
            }
        }
        LText {
            objectName: "updateStatus"
            Layout.fillWidth: true
            text: root.updates.status !== "" ? root.updates.status
                                              : qsTr("Lumen only looks for updates when you ask. It never installs anything by itself.")
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            font.pixelSize: 12
            color: root.updates.available ? Theme.live : Theme.text
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            LButton {
                objectName: "updateCheck"
                text: root.updates.checking ? qsTr("Checking…") : qsTr("Check now")
                enabled: !root.updates.checking
                implicitHeight: 28
                radius: 6
                onClicked: root.updates.check()
            }
            LButton {
                objectName: "updateDownload"
                visible: root.updates.available
                text: qsTr("Download %1").arg(root.updates.latestVersion)
                variant: "primary"
                implicitHeight: 28
                radius: 6
                onClicked: root.updates.openDownload()
            }
            Item { Layout.fillWidth: true }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
        RowLayout {
            Layout.fillWidth: true
            LText {
                Layout.fillWidth: true
                text: qsTr("Check when Lumen starts")
                font.pixelSize: 12
            }
            LButton {
                objectName: "updateAutomatic"
                text: root.updates.automatic ? qsTr("On") : qsTr("Off")
                variant: root.updates.automatic ? "primary" : "default"
                implicitHeight: 28
                radius: 6
                onClicked: root.updates.automatic = !root.updates.automatic
            }
        }
        LText {
            Layout.fillWidth: true
            text: qsTr("A check asks GitHub, where Lumen is published, for the newest version. Nothing about you or this computer is sent.")
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            font.pixelSize: 11
            color: Theme.textMuted
        }
    }
}
