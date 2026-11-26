import QtQuick

// The screen-setup label: while the operator chooses which screen is which,
// each screen shows its number, its name and what it is for, in large type
// above everything else. Hidden the rest of the time.
Rectangle {
    id: root

    // {number, name, role} from the operator; empty: hidden.
    required property var identify

    anchors.fill: parent
    z: 1000
    visible: root.identify !== undefined && root.identify.number !== undefined
    color: "#E60F1115"

    Column {
        anchors.centerIn: parent
        width: parent.width * 0.8
        spacing: root.height * 0.02

        Text {
            objectName: "identifyNumber"
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.visible ? String(root.identify.number) : ""
            color: "#F08A24"
            font.family: "Inter"
            font.weight: Font.Bold
            font.pixelSize: root.height * 0.4
        }
        Text {
            objectName: "identifyName"
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.visible ? (root.identify.name || "") : ""
            color: "#E6E8EC"
            font.family: "Inter"
            font.pixelSize: root.height * 0.05
        }
        Text {
            objectName: "identifyRole"
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.visible ? (root.identify.role || "") : ""
            color: "#9AA3B2"
            font.family: "Inter"
            font.weight: Font.Medium
            font.pixelSize: root.height * 0.045
        }
    }
}
