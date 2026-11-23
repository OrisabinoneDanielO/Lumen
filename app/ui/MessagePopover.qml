import QtQuick
import QtQuick.Layouts

// Messages (batch 12, basic): a line of text in the banner at the top of the
// output ("Parent of child 42, please come to the nursery"), shown over the
// slides without touching them. Recent messages can be shown again with a
// click. Templates with fields (batch 14): pick one, fill in its fields and
// show it; any message can scroll as a ticker instead.
Rectangle {
    id: root

    required property QtObject controller
    property QtObject templates: null
    signal closeRequested()

    property bool ticker: false
    property int templateRow: -1
    property var fieldValues: ({})
    readonly property var templateFields: templates && templateRow >= 0 ? templates.fields(templateRow) : []

    function focusInput() { input.focusInput() }

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
                text: qsTr("MESSAGE")
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
            visible: root.controller.messageShown !== ""
            text: qsTr("On screen: %1").arg(root.controller.messageShown)
            font.pixelSize: 11
            color: Theme.live
        }
        // Templates with fields.
        Flow {
            Layout.fillWidth: true
            visible: root.templates !== null
            spacing: 6
            Repeater {
                model: root.templates ? root.templates.templates : null
                delegate: LButton {
                    required property int index
                    required property string name
                    objectName: "messageTemplate-" + index
                    text: name
                    implicitHeight: 28
                    radius: 6
                    fontSize: 11
                    variant: root.templateRow === index ? "primary" : "default"
                    onClicked: {
                        root.fieldValues = ({})
                        root.templateRow = root.templateRow === index ? -1 : index
                    }
                }
            }
        }
        LText {
            Layout.fillWidth: true
            visible: root.templateRow >= 0
            text: root.templateRow >= 0 ? root.templates.templates.get(root.templateRow).text : ""
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            color: Theme.textMuted
        }
        Repeater {
            model: root.templateFields
            delegate: SearchField {
                required property string modelData
                required property int index
                objectName: "messageField-" + index
                inputName: "messageFieldText-" + index
                Layout.fillWidth: true
                placeholder: modelData
                onTextChanged: {
                    const values = root.fieldValues
                    values[modelData] = text
                    root.fieldValues = values
                }
                onAccepted: showButton.clicked()
            }
        }
        SearchField {
            id: input
            objectName: "messageInput"
            inputName: "messageText"
            Layout.fillWidth: true
            placeholder: qsTr("Message to show over the slides")
            visible: root.templateRow < 0
            onAccepted: showButton.clicked()
            onEscaped: root.closeRequested()
        }
        RowLayout {
            Layout.fillWidth: true
            LButton {
                objectName: "messageTicker"
                text: root.ticker ? qsTr("Ticker ✓") : qsTr("Ticker")
                variant: root.ticker ? "primary" : "default"
                implicitHeight: 30
                radius: 6
                onClicked: root.ticker = !root.ticker
            }
            Item { Layout.fillWidth: true }
            LButton {
                objectName: "messageHide"
                text: qsTr("Hide")
                implicitHeight: 30
                radius: 6
                onClicked: root.controller.hideMessage()
            }
            LButton {
                id: showButton
                objectName: "messageShow"
                text: qsTr("Show")
                variant: "primary"
                implicitHeight: 30
                radius: 6
                onClicked: {
                    if (root.templateRow >= 0)
                        root.templates.show(root.templateRow, root.fieldValues, root.ticker)
                    else if (root.controller.showMessage(input.text, root.ticker))
                        input.text = ""
                }
            }
        }
        SectionLabel {
            visible: root.controller.recentMessages.length > 0
            text: qsTr("RECENT")
        }
        Repeater {
            model: root.controller.recentMessages
            delegate: Rectangle {
                id: recent
                required property string modelData
                required property int index
                objectName: "recentMessage-" + index
                Layout.fillWidth: true
                implicitHeight: 30
                radius: 6
                color: recentMouse.containsMouse ? Theme.raised : "transparent"
                LText {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    verticalAlignment: Text.AlignVCenter
                    text: recent.modelData
                    font.pixelSize: 12
                }
                MouseArea {
                    id: recentMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.controller.showMessage(recent.modelData)
                }
            }
        }
    }
}
