import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// Main operator window: the five fixed regions from design/mockups/Main.html.
// Batch 1 shows them empty; batch 5 builds the real shell.
Window {
    id: window

    width: 1440
    height: 900
    minimumWidth: 1024
    minimumHeight: 640
    visible: true
    title: qsTr("Lumen Presenter")
    color: Theme.window

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RegionPlaceholder {
            objectName: "toolbar"
            title: qsTr("TOOLBAR")
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.toolbarHeight
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            RegionPlaceholder {
                objectName: "servicePlan"
                title: qsTr("SERVICE PLAN")
                Layout.preferredWidth: Theme.planWidth
                Layout.fillHeight: true
            }

            RegionPlaceholder {
                objectName: "slideGrid"
                title: qsTr("SLIDES")
                fill: Theme.window
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            RegionPlaceholder {
                objectName: "previewLive"
                title: qsTr("PREVIEW AND LIVE")
                Layout.preferredWidth: Theme.previewWidth
                Layout.fillHeight: true
            }
        }

        RegionPlaceholder {
            objectName: "library"
            title: qsTr("LIBRARY")
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.libraryHeight
        }
    }
}
