import QtQuick

// A border between two panels that can be dragged to resize them. Vertical
// (between columns) by default; horizontal sets a row border. moved(delta)
// reports how far the pointer went since the last move; double-click asks
// for the panels' default sizes.
Item {
    id: root

    property bool horizontal: false
    signal moved(real delta)
    signal reset()

    implicitWidth: horizontal ? 0 : 6
    implicitHeight: horizontal ? 6 : 0
    z: 5

    Rectangle {
        anchors.centerIn: parent
        width: root.horizontal ? parent.width : 2
        height: root.horizontal ? 2 : parent.height
        color: area.pressed ? Theme.live : area.containsMouse ? Theme.border : "transparent"
    }

    MouseArea {
        id: area
        anchors.fill: parent
        // A little wider than it looks, so it is easy to catch.
        anchors.margins: -3
        hoverEnabled: true
        cursorShape: root.horizontal ? Qt.SplitVCursor : Qt.SplitHCursor
        property real last: 0
        onPressed: mouse => last = root.horizontal ? mapToItem(null, mouse.x, mouse.y).y : mapToItem(null, mouse.x, mouse.y).x
        onPositionChanged: mouse => {
            if (!pressed)
                return
            const p = mapToItem(null, mouse.x, mouse.y)
            const now = root.horizontal ? p.y : p.x
            root.moved(now - last)
            last = now
        }
        onDoubleClicked: root.reset()
    }
}
