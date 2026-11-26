import QtQuick
import QtQuick.Layouts
import Lumen.Slides

// The basic theme editor (Library > Themes). Changes show at once in the
// preview, drawn exactly as the audience output draws it; Save applies them
// to every item using the theme.
Item {
    id: root

    required property QtObject themes
    signal closed()

    visible: false
    anchors.fill: parent
    z: 50

    function show() {
        visible = true
        dialog.forceActiveFocus()
    }
    function close() {
        if (root.themes.dirty)
            root.themes.revert()
        visible = false
        root.closed()
    }

    readonly property var s: themes.style

    // A row of mutually exclusive choices.
    component Choice: LButton {
        property var value
        property var selected
        implicitHeight: 30
        radius: 6
        fontSize: 12
        variant: value === selected ? "primary" : "default"
    }
    component Swatch: Rectangle {
        property color swatch
        property bool chosen: false
        signal picked()
        width: 26
        height: 26
        radius: 13
        color: swatch
        border.width: chosen ? 2 : 1
        border.color: chosen ? Theme.next : Theme.border
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.picked()
        }
    }
    component Field: RowLayout {
        property string label
        default property alias content: holder.data
        spacing: 10
        LText {
            Layout.preferredWidth: 96
            text: parent.label
            font.pixelSize: 12
            color: Theme.textMuted
        }
        Row {
            id: holder
            spacing: 6
        }
    }
    component Stepper: Row {
        property string key
        property int value
        property int step: 4
        spacing: 6
        LButton {
            implicitHeight: 30
            implicitWidth: 30
            radius: 6
            text: "−"
            onClicked: root.themes.setStyleValue(parent.key, parent.value - parent.step)
        }
        LText {
            width: 44
            height: 30
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: parent.value + " px"
        }
        LButton {
            implicitHeight: 30
            implicitWidth: 30
            radius: 6
            text: "+"
            onClicked: root.themes.setStyleValue(parent.key, parent.value + parent.step)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#99000000"
        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }
    }

    Rectangle {
        id: dialog
        objectName: "themeDialog"
        anchors.centerIn: parent
        width: Math.min(1100, parent.width - 48)
        height: Math.min(680, parent.height - 48)
        radius: 12
        color: Theme.panel
        border.color: Theme.border
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Themes")
        Keys.onEscapePressed: root.close()

        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                LText {
                    Layout.fillWidth: true
                    text: qsTr("Themes")
                    font.pixelSize: 15
                    font.bold: true
                }
                LButton {
                    implicitWidth: 36
                    variant: "ghost"
                    text: "×"
                    fontSize: 16
                    Accessible.name: qsTr("Close")
                    onClicked: root.close()
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                // Theme list.
                Column {
                    id: list
                    x: 12
                    y: 12
                    width: 220
                    spacing: 2
                    Repeater {
                        model: root.themes.themes
                        delegate: Rectangle {
                            id: entry
                            required property var modelData
                            required property int index
                            objectName: "theme-" + index
                            width: list.width
                            height: 46
                            radius: 8
                            color: index === root.themes.current ? Theme.selected : "transparent"
                            Column {
                                x: 10
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                LText {
                                    width: entry.width - 20
                                    text: entry.modelData.name
                                    font.weight: Font.Medium
                                }
                                LText {
                                    text: (entry.modelData.appliesTo === "song" ? qsTr("Songs")
                                           : entry.modelData.appliesTo === "scripture" ? qsTr("Scripture")
                                           : entry.modelData.appliesTo === "lower_third" ? qsTr("Lower thirds")
                                           : entry.modelData.appliesTo)
                                          + (entry.modelData.isDefault ? qsTr(" · default") : "")
                                    font.pixelSize: 11
                                    color: Theme.textMuted
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.themes.select(entry.index)
                            }
                        }
                    }
                    Item { width: 1; height: 8 }
                    LButton {
                        objectName: "duplicateTheme"
                        text: qsTr("Duplicate theme")
                        radius: 6
                        onClicked: root.themes.duplicate()
                    }
                }
                Rectangle { x: 244; width: 1; height: parent.height; color: Theme.border }

                // Settings.
                Flickable {
                    x: 265
                    y: 0
                    width: 420
                    height: parent.height
                    clip: true
                    contentHeight: form.height + 32
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: form
                        y: 16
                        width: parent.width
                        spacing: 12

                        Field {
                            label: qsTr("Name")
                            Rectangle {
                                width: 280
                                height: 32
                                radius: 6
                                color: Theme.window
                                border.color: nameInput.activeFocus ? Theme.next : Theme.border
                                TextInput {
                                    id: nameInput
                                    objectName: "themeName"
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    verticalAlignment: TextInput.AlignVCenter
                                    color: Theme.text
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 13
                                    text: root.themes.name
                                    onEditingFinished: root.themes.name = text
                                }
                            }
                        }
                        Field {
                            label: qsTr("Font")
                            Repeater {
                                model: root.themes.fonts
                                delegate: Choice {
                                    required property string modelData
                                    text: modelData
                                    value: modelData
                                    selected: root.s.font
                                    onClicked: root.themes.setStyleValue("font", modelData)
                                }
                            }
                            Choice {
                                objectName: "themeBold"
                                text: qsTr("Bold")
                                value: true
                                selected: root.s.weight >= 600
                                onClicked: root.themes.setStyleValue("bold", !(root.s.weight >= 600))
                            }
                        }
                        Field {
                            label: qsTr("Largest size")
                            Stepper { objectName: "themeMaxSize"; key: "maxSize"; value: root.s.maxSize || 72 }
                        }
                        Field {
                            label: qsTr("Smallest size")
                            Stepper { key: "minSize"; value: root.s.minSize || 40 }
                        }
                        Field {
                            label: qsTr("Text colour")
                            Repeater {
                                model: ["#FFFFFF", "#FFE7A3", "#BFD3FF", "#000000"]
                                delegate: Swatch {
                                    required property string modelData
                                    swatch: modelData
                                    chosen: String(root.s.colour).toUpperCase().endsWith(modelData.substring(1))
                                    onPicked: root.themes.setStyleValue("colour", modelData)
                                }
                            }
                        }
                        Field {
                            label: qsTr("Align")
                            Choice { text: qsTr("Left"); value: "left"; selected: root.s.align; onClicked: root.themes.setStyleValue("align", "left") }
                            Choice { text: qsTr("Centre"); value: "center"; selected: root.s.align; onClicked: root.themes.setStyleValue("align", "center") }
                            Choice { text: qsTr("Right"); value: "right"; selected: root.s.align; onClicked: root.themes.setStyleValue("align", "right") }
                        }
                        Field {
                            label: qsTr("Position")
                            Choice { text: qsTr("Top"); value: "top"; selected: root.s.valign; onClicked: root.themes.setStyleValue("valign", "top") }
                            Choice { text: qsTr("Middle"); value: "middle"; selected: root.s.valign; onClicked: root.themes.setStyleValue("valign", "middle") }
                            Choice { text: qsTr("Bottom"); value: "bottom"; selected: root.s.valign; onClicked: root.themes.setStyleValue("valign", "bottom") }
                        }
                        Field {
                            label: qsTr("Outline")
                            Choice { text: qsTr("Off"); value: 0; selected: root.s.outline; onClicked: root.themes.setStyleValue("outline", 0) }
                            Choice { text: qsTr("Thin"); value: 2; selected: root.s.outline; onClicked: root.themes.setStyleValue("outline", 2) }
                            Choice { text: qsTr("Thick"); value: 4; selected: root.s.outline; onClicked: root.themes.setStyleValue("outline", 4) }
                        }
                        Field {
                            label: qsTr("Shadow")
                            Choice { objectName: "themeShadowOn"; text: qsTr("On"); value: true; selected: root.s.shadow; onClicked: root.themes.setStyleValue("shadow", true) }
                            Choice { text: qsTr("Off"); value: false; selected: root.s.shadow; onClicked: root.themes.setStyleValue("shadow", false) }
                        }
                        Field {
                            label: qsTr("Background")
                            Repeater {
                                model: ["#14213D", "#1B1F2B", "#23262D", "#2E1F3A", "#000000"]
                                delegate: Swatch {
                                    required property string modelData
                                    objectName: "themeBackground-" + modelData.substring(1)
                                    swatch: modelData
                                    chosen: root.themes.hasBackground
                                            && String(root.themes.background).toUpperCase() === modelData.toUpperCase()
                                    onPicked: root.themes.setBackground(modelData)
                                }
                            }
                            Choice {
                                text: qsTr("None")
                                value: false
                                selected: root.themes.hasBackground
                                onClicked: root.themes.setBackground("transparent")
                            }
                        }
                        Field {
                            label: qsTr("Margins")
                            Choice { text: qsTr("Narrow"); value: 0.03; selected: Math.round((root.s.safeArea ? root.s.safeArea.x : 0.05) * 100) / 100; onClicked: root.themes.setStyleValue("margin", 0.03) }
                            Choice { text: qsTr("Normal"); value: 0.05; selected: Math.round((root.s.safeArea ? root.s.safeArea.x : 0.05) * 100) / 100; onClicked: root.themes.setStyleValue("margin", 0.05) }
                            Choice { text: qsTr("Wide"); value: 0.1; selected: Math.round((root.s.safeArea ? root.s.safeArea.x : 0.05) * 100) / 100; onClicked: root.themes.setStyleValue("margin", 0.1) }
                        }
                        Field {
                            label: qsTr("Footer")
                            Choice { text: qsTr("Below"); value: "below"; selected: root.s.footerPosition; onClicked: root.themes.setStyleValue("footerPosition", "below") }
                            Choice { text: qsTr("Above"); value: "above"; selected: root.s.footerPosition; onClicked: root.themes.setStyleValue("footerPosition", "above") }
                            Choice { text: qsTr("Off"); value: "none"; selected: root.s.footerPosition; onClicked: root.themes.setStyleValue("footerPosition", "none") }
                        }
                        Field {
                            label: qsTr("Footer size")
                            Stepper { key: "footerSize"; value: root.s.footerSize || 28; step: 2 }
                        }
                    }
                }
                Rectangle { x: 705; width: 1; height: parent.height; color: Theme.border }

                // Preview.
                Column {
                    x: 726
                    y: 16
                    width: parent.width - 746
                    spacing: 10

                    SectionLabel { text: qsTr("PREVIEW") }
                    Rectangle {
                        id: preview
                        objectName: "themePreview"
                        width: parent.width
                        height: Math.round(width * 9 / 16)
                        color: root.themes.hasBackground ? root.themes.background : "#000000"
                        border.color: Theme.border
                        clip: true

                        SlideContent {
                            anchors.fill: parent
                            kind: "text"
                            text: root.themes.previewText
                            style: root.themes.previewStyle
                            footer: root.themes.previewFooter
                            layerName: "slide"
                        }
                        // Safe area guide.
                        Rectangle {
                            readonly property var area: root.s.safeArea || ({ x: 0.05, y: 0.05, w: 0.9, h: 0.9 })
                            x: area.x * preview.width
                            y: area.y * preview.height
                            width: area.w * preview.width
                            height: area.h * preview.height
                            color: "transparent"
                            border.color: "#55FFFFFF"
                            border.width: 1
                        }
                    }
                    LText {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: Theme.textMuted
                        text: root.themes.isDefault
                              ? qsTr("Default theme for its kind. Saving updates every item that uses it.")
                              : qsTr("Saving updates every item that uses this theme.")
                    }
                    LButton {
                        objectName: "makeDefaultTheme"
                        visible: !root.themes.isDefault
                        radius: 6
                        text: qsTr("Make default")
                        onClicked: root.themes.makeDefault()
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 14
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 8
                LText {
                    Layout.fillWidth: true
                    visible: root.themes.dirty
                    text: qsTr("Unsaved changes")
                    font.pixelSize: 12
                    color: Theme.warnHint
                }
                Item { Layout.fillWidth: true; visible: !root.themes.dirty }
                LButton {
                    objectName: "revertTheme"
                    text: qsTr("Revert")
                    opacity: root.themes.dirty ? 1 : 0.5
                    onClicked: root.themes.revert()
                }
                LButton {
                    objectName: "saveTheme"
                    text: qsTr("Save")
                    variant: "primary"
                    opacity: root.themes.dirty ? 1 : 0.5
                    onClicked: root.themes.save()
                }
            }
        }
    }
}
