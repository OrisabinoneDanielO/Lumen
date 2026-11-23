import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// Main operator window in Show mode, laid out as design/mockups/Main.html.
// The regions are static until batch 5 wires them up. With designPreview set
// (the --design-preview flag) they show the mockup's sample content; otherwise
// they show empty states, so the app never presents made-up service data.
Window {
    id: window

    property bool designPreview: false

    readonly property var idleLayers: [
        { name: qsTr("Messages"), value: qsTr("Off"), active: false },
        { name: qsTr("Props"), value: qsTr("Off"), active: false },
        { name: qsTr("Slide"), value: qsTr("Off"), active: false },
        { name: qsTr("Media"), value: qsTr("Off"), active: false },
        { name: qsTr("Background"), value: qsTr("Off"), active: false }
    ]

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

        Toolbar {
            objectName: "toolbar"
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            planName: window.designPreview ? SampleData.planName : qsTr("No service plan")
            planDetail: window.designPreview ? SampleData.planDetail
                                             : qsTr("Create or open a plan to begin")
            outputsConnected: window.designPreview
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ServicePlanPanel {
                objectName: "servicePlan"
                Layout.preferredWidth: Theme.planWidth
                Layout.fillHeight: true
                items: window.designPreview ? SampleData.planItems : []
                currentIndex: window.designPreview ? SampleData.currentPlanIndex : -1
            }

            SlideGridPanel {
                objectName: "slideGrid"
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: window.designPreview ? SampleData.itemTitle : ""
                detail: window.designPreview ? SampleData.itemDetail : ""
                arrangement: window.designPreview ? SampleData.arrangement : []
                slides: window.designPreview ? SampleData.slides : []
                liveIndex: window.designPreview ? SampleData.liveIndex : -1
                nextIndex: window.designPreview ? SampleData.nextIndex : -1
            }

            PreviewLivePanel {
                objectName: "previewLive"
                Layout.preferredWidth: Theme.previewWidth
                Layout.fillHeight: true
                nextText: window.designPreview ? SampleData.slides[SampleData.nextIndex].text : ""
                liveText: window.designPreview ? SampleData.slides[SampleData.liveIndex].text : ""
                liveLabel: window.designPreview ? SampleData.liveLabel : qsTr("Clear")
                layers: window.designPreview ? SampleData.layers : window.idleLayers
            }
        }

        LibraryPanel {
            objectName: "library"
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.libraryHeight
            songs: window.designPreview ? SampleData.songs : []
            selectedIndex: window.designPreview ? 0 : -1
        }
    }
}
