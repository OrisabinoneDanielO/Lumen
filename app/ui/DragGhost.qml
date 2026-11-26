import QtQuick

// What follows the pointer while something is dragged onto the service plan:
// a library song, video, picture or presentation, or a plan item being
// moved. One per window; it lives above everything else. The plan's
// DropArea reads `source` ("song", "media", "deck" or "plan") and `row`.
Rectangle {
    id: ghost

    property string source
    property int row: -1
    property alias text: label.text
    readonly property bool dragging: Drag.active

    // How far the pointer moves before a press becomes a drag.
    readonly property int threshold: 8

    // Starts a drag from `item`, pressed at (x, y) in its coordinates.
    function begin(item, x, y, source, row, title) {
        ghost.source = source
        ghost.row = row
        text = title
        place(item, x, y)
        Drag.active = true
    }
    function place(item, x, y) {
        const p = item.mapToItem(ghost.parent, x, y)
        ghost.x = p.x - 16
        ghost.y = p.y - height / 2
    }
    // Ends the drag; true if the plan took it.
    function finish() {
        const accepted = Drag.drop() !== Qt.IgnoreAction
        Drag.active = false
        source = ""
        row = -1
        return accepted
    }

    z: 1000
    visible: Drag.active
    width: Math.min(label.implicitWidth + 24, 280)
    height: 32
    radius: 6
    color: Theme.raised
    border.color: Theme.next
    border.width: 1
    opacity: 0.92
    Drag.dragType: Drag.Internal
    Drag.keys: ["lumen/plan"]
    Drag.hotSpot.x: 16
    Drag.hotSpot.y: height / 2

    LText {
        id: label
        anchors.verticalCenter: parent.verticalCenter
        x: 12
        width: parent.width - 24
        font.pixelSize: 12
    }
}
