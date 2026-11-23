pragma Singleton
import QtQuick

// The sample content shown in design/mockups/Main.html. Used only when the app
// runs with --design-preview, so designers can compare the window with the
// mockup. Lyrics are public-domain hymns; scripture is the World English Bible.
QtObject {
    readonly property string planName: "Sunday Service"
    readonly property string planDetail: "[Service date] · 9 items"

    readonly property var planItems: [
        { kind: "TIMER", title: "Pre-service countdown", subtitle: "Announcement loop" },
        { kind: "SLIDES", title: "Welcome", subtitle: "Native slides" },
        { kind: "SONG", title: "Amazing Grace", subtitle: "John Newton" },
        { kind: "SONG", title: "Holy, Holy, Holy", subtitle: "Reginald Heber" },
        { kind: "BIBLE", title: "John 3:16-18", subtitle: "World English Bible" },
        { kind: "SLIDES", title: "Announcements", subtitle: "PowerPoint import" },
        { kind: "MEDIA", title: "Offering video", subtitle: "[Video file]" },
        { kind: "SLIDES", title: "[Sermon title]", subtitle: "Sermon points" },
        { kind: "SONG", title: "Be Thou My Vision", subtitle: "Trad. Irish" }
    ]
    readonly property int currentPlanIndex: 2

    readonly property string itemTitle: "Amazing Grace"
    readonly property string itemDetail: "John Newton · Public domain · Theme: Lyrics, centred"

    readonly property var arrangement: [
        { label: "V1", background: "#22304A", foreground: "#BFD3FF" },
        { label: "V2", background: "#1C3A37", foreground: "#A6E6DC" },
        { label: "V3", background: "#30264A", foreground: "#D9C6FF" },
        { label: "V4", background: "#3D2A1C", foreground: "#FFD0A8" },
        { label: "Tag", background: "#2B303A", foreground: "#E9ECF1" }
    ]

    readonly property var slides: [
        { group: "Verse 1", color: "#5B8DEF", text: "Amazing grace! How sweet the sound\nThat saved a wretch like me!" },
        { group: "Verse 1", color: "#5B8DEF", text: "I once was lost, but now am found;\nWas blind, but now I see." },
        { group: "Verse 2", color: "#3FB7A6", text: "'Twas grace that taught my heart to fear,\nAnd grace my fears relieved;" },
        { group: "Verse 2", color: "#3FB7A6", text: "How precious did that grace appear\nThe hour I first believed." },
        { group: "Verse 3", color: "#A779E9", text: "Through many dangers, toils and snares,\nI have already come;" },
        { group: "Verse 3", color: "#A779E9", text: "'Tis grace hath brought me safe thus far,\nAnd grace will lead me home." },
        { group: "Verse 4", color: "#E8833A", text: "When we've been there ten thousand years,\nBright shining as the sun," },
        { group: "Verse 4", color: "#E8833A", text: "We've no less days to sing God's praise\nThan when we'd first begun." },
        { group: "Tag", color: "#C9CED6", text: "Was blind, but now I see." }
    ]
    readonly property int liveIndex: 2
    readonly property int nextIndex: 3
    readonly property string liveLabel: "Verse 2 · slide 3"

    readonly property var layers: [
        { name: "Messages", value: "Off", active: false },
        { name: "Props", value: "Church logo bug", active: false },
        { name: "Slide", value: "Verse 2 · slide 3", active: true },
        { name: "Media", value: "Off", active: false },
        { name: "Background", value: "Motion loop: Slow light", active: false }
    ]

    readonly property var songs: [
        { title: "Amazing Grace", author: "John Newton", copyright: "Public domain", key: "G" },
        { title: "Be Thou My Vision", author: "Trad. Irish, tr. Mary Byrne", copyright: "Public domain", key: "Eb" },
        { title: "Holy, Holy, Holy", author: "Reginald Heber", copyright: "Public domain", key: "D" },
        { title: "It Is Well with My Soul", author: "Horatio Spafford", copyright: "Public domain", key: "C" }
    ]
}
