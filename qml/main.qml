import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import SidPlayer 1.0

// ── SID-Player — ultramodern, Glas, Live-Wellenform, Playlist ──

ApplicationWindow {
    id: root
    width: 1200
    height: 760
    visible: true
    title: "PrismaSID — " + sidBackend.title
    color: "#0a0e14"
    minimumWidth: 950
    minimumHeight: 640

    // ── Farbwelt ──
    readonly property color bgDeep: "#0a0e14"
    readonly property color bgPanel: "#121826"
    readonly property color glassBorder: "#2a3b5c"
    readonly property color accent: "#4fc3f7"
    readonly property color accent2: "#b388ff"
    readonly property color textMain: "#e8eef7"
    readonly property color textDim: "#8fa3bf"

    // ── Wellenform-Daten ──
    property var waveSamples: []
    onWaveSamplesChanged: {
        // waveCanvas (C++ WaveformItem) zeichnet sich selbst bei setSamples — kein requestPaint nötig
    }

    // Orte-Liste für den Browser (JS-Kopie vom Backend — robust gegen Signal-Timing)
    property var placesList: []
    function refreshPlaces() {
        placesList = sidBackend.places
        // Fallback: System-Orte wenn leer

        if (placesList.length === 0) {
            placesList = [
                { name: "🏠 Home", path: "/home/shadowwrath" },
                { name: "🖴 NAS", path: "/mnt/nas" },
                { name: "💿 /", path: "/" }
            ]
        }
    }

    Connections {
        target: sidBackend
        function onWaveformReady(samples) {
            root.waveSamples = samples
        }
    }

    // ── Hintergrund ──
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0a0e14" }
            GradientStop { position: 0.6; color: "#0d1526" }
            GradientStop { position: 1.0; color: "#0a0e14" }
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width * 0.7
        height: 200
        radius: 100
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.31, 0.77, 0.97, 0.10) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // ── Haupt-Layout: Player links, Playlist rechts ──
    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 20

        // ═══════════ Player-Ansicht ═══════════
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // Header
            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                Rectangle {
                    width: 44; height: 44
                    radius: 12
                    color: Qt.rgba(0.31, 0.77, 0.97, 0.12)
                    border.color: glassBorder
                    border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: "🎵"
                        font.pixelSize: 20
                    }
                }

                ColumnLayout {
                    spacing: 1
                    Text {
                        text: "PRISMASID"
                        font.pixelSize: 19
                        font.weight: Font.DemiBold
                        font.letterSpacing: 2
                        color: textMain
                    }
                    Text {
                        text: sidBackend.isPlaying ? "▶ läuft" : "bereit"
                        font.pixelSize: 12
                        color: sidBackend.isPlaying ? accent : textDim
                    }
                }

                Item { Layout.fillWidth: true }

                // Chip-Umschalter
                Text {
                    text: "CHIP"
                    font.pixelSize: 10
                    font.letterSpacing: 1
                    color: textDim
                    verticalAlignment: Text.AlignVCenter
                }
                RowLayout {
                    spacing: 5
                    Repeater {
                        model: ["AUTO", "MOS6581", "MOS8580"]
                        delegate: Rectangle {
                            property bool active: sidBackend.chipModel === modelData
                            width: chipLabel.width + 20
                            height: 28
                            radius: 8
                            color: active ? Qt.rgba(0.31, 0.77, 0.97, 0.22) : "transparent"
                            border.color: active ? accent : glassBorder
                            border.width: active ? 1.5 : 1
                            Text {
                                id: chipLabel
                                anchors.centerIn: parent
                                text: modelData
                                font.pixelSize: 11
                                color: active ? accent : textDim
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: sidBackend.selectChip(modelData)
                            }
                        }
                    }
                }

                // Stereo/Mono-Schalter
                Text {
                    text: "AUSGANG"
                    font.pixelSize: 10
                    font.letterSpacing: 1
                    color: textDim
                    verticalAlignment: Text.AlignVCenter
                }
                Rectangle {
                    width: 82
                    height: 28
                    radius: 14
                    color: sidBackend.stereoMode ? Qt.rgba(0.70, 0.53, 1.0, 0.18)
                                                 : Qt.rgba(1,1,1,0.05)
                    border.color: sidBackend.stereoMode ? accent2 : glassBorder
                    border.width: 1

                    Row {
                        anchors.centerIn: parent
                        spacing: 10
                        Text {
                            text: "MONO"
                            font.pixelSize: 10
                            font.weight: sidBackend.stereoMode ? Font.Normal : Font.DemiBold
                            color: sidBackend.stereoMode ? textDim : accent2
                        }
                        Text {
                            text: "STEREO"
                            font.pixelSize: 10
                            font.weight: sidBackend.stereoMode ? Font.DemiBold : Font.Normal
                            color: sidBackend.stereoMode ? accent2 : textDim
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: sidBackend.stereoMode = !sidBackend.stereoMode
                    }
                }

                // Visualisierungs-Umschalter
                Text {
                    text: "VIZ"
                    font.pixelSize: 10
                    font.letterSpacing: 1
                    color: textDim
                    verticalAlignment: Text.AlignVCenter
                }
                RowLayout {
                    spacing: 5
                    Repeater {
                        model: [
                            { label: "WAVE", mode: 0 },
                            { label: "BARS", mode: 1 },
                            { label: "MIX", mode: 2 }
                        ]
                        delegate: Rectangle {
                            property bool active: waveCanvas.vizMode === modelData.mode
                            width: lbl.width + 20
                            height: 28
                            radius: 8
                            color: active ? Qt.rgba(0.70, 0.53, 1.0, 0.22) : "transparent"
                            border.color: active ? accent2 : glassBorder
                            border.width: active ? 1.5 : 1
                            Text {
                                id: lbl
                                anchors.centerIn: parent
                                text: modelData.label
                                font.pixelSize: 11
                                color: active ? accent2 : textDim
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: waveCanvas.vizMode = modelData.mode
                            }
                        }
                    }
                }

                // Auto-Weiter-Schalter (Song-Ende → nächster Subsong/Track)
                Text {
                    text: "AUTO"
                    font.pixelSize: 10
                    font.letterSpacing: 1
                    color: textDim
                    verticalAlignment: Text.AlignVCenter
                }
                Rectangle {
                    width: 76
                    height: 28
                    radius: 14
                    color: sidBackend.autoAdvance ? Qt.rgba(0.31, 0.77, 0.97, 0.18)
                                                  : Qt.rgba(1,1,1,0.05)
                    border.color: sidBackend.autoAdvance ? accent : glassBorder
                    border.width: 1
                    Row {
                        anchors.centerIn: parent
                        spacing: 10
                        Text {
                            text: "AUS"
                            font.pixelSize: 10
                            font.weight: sidBackend.autoAdvance ? Font.Normal : Font.DemiBold
                            color: sidBackend.autoAdvance ? textDim : accent
                        }
                        Text {
                            text: "AN"
                            font.pixelSize: 10
                            font.weight: sidBackend.autoAdvance ? Font.DemiBold : Font.Normal
                            color: sidBackend.autoAdvance ? accent : textDim
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: sidBackend.autoAdvance = !sidBackend.autoAdvance
                    }
                }
            }

            // Wellenform-Display (Canvas — performant, kein Repeater-Ruckeln)
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: 200
                radius: 16
                color: Qt.rgba(0.07, 0.10, 0.17, 0.85)
                border.color: glassBorder
                border.width: 1
                clip: true

                Repeater {
                    model: 9
                    Rectangle {
                        y: parent.height * index / 8
                        width: parent.width
                        height: 1
                        color: Qt.rgba(0.53, 0.64, 0.75, 0.06)
                    }
                }

                // Wellenform-Display (C++ Scene-Graph — GPU-gezeichnet, butterweich)
                WaveformItem {
                    id: waveCanvas
                    anchors.fill: parent
                    anchors.margins: 4
                    colorTop: "#4fc3f7"
                    colorBottom: "#b388ff"
                    Connections {
                        target: sidBackend
                        function onWaveformReady(samples) {
                            waveCanvas.setSamples(samples)
                        }
                    }
                    // Rainbow-Wanderung: Farben fließen langsam durch den Regenbogen
                    NumberAnimation on hueShift {
                        from: 0
                        to: 1.0
                        duration: 12000
                        running: sidBackend.isPlaying
                        loops: Animation.Infinite
                    }
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 1
                    color: Qt.rgba(0.53, 0.64, 0.75, 0.12)
                }

                Text {
                    anchors.centerIn: parent
                    text: root.waveSamples.length === 0 ? (sidBackend.isPlaying ? "…" : "SID laden oder Datei öffnen")
                                                        : ""
                    font.pixelSize: 13
                    color: textDim
                    opacity: 0.6
                }
            }

            // Song-Info
            RowLayout {
                Layout.fillWidth: true
                spacing: 24

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text {
                        text: sidBackend.title.length > 0 ? sidBackend.title : "—"
                        font.pixelSize: 24
                        font.weight: Font.DemiBold
                        color: textMain
                        elide: Text.ElideRight
                    }
                    Text {
                        text: sidBackend.author.length > 0
                              ? sidBackend.author + (sidBackend.copyright.length > 0 ? " · " + sidBackend.copyright : "")
                              : "—"
                        font.pixelSize: 13
                        color: textDim
                    }
                }

                ColumnLayout {
                    spacing: 5
                    Text {
                        text: "SUBSONGS  (" + sidBackend.subsongs + ")"
                        font.pixelSize: 10
                        font.letterSpacing: 1
                        color: textDim
                    }

                    // ── Subsong-Buttons (unter dem Label, wie gehabt) ──
                    Flow {
                        Layout.preferredWidth: 300
                        spacing: 5
                        Repeater {
                            model: sidBackend.subsongs
                            Rectangle {
                                property bool active: sidBackend.currentSubsong === (index + 1)
                                width: subLabel.width + 16
                                height: 24
                                radius: 6
                                color: active ? Qt.rgba(0.70, 0.53, 1.0, 0.20) : Qt.rgba(1,1,1,0.04)
                                border.color: active ? accent2 : glassBorder
                                border.width: active ? 1.5 : 1
                                Text {
                                    id: subLabel
                                    anchors.centerIn: parent
                                    text: index + 1
                                    font.pixelSize: 11
                                    color: active ? accent2 : textDim
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: sidBackend.selectSubsong(index + 1)
                                }
                            }
                        }
                    }

                    // ── Song-Detail-Kacheln (darunter, OHNE Strecken) ──
                    GridLayout {
                        Layout.preferredWidth: 300
                        Layout.topMargin: 8
                        columns: 2
                        rowSpacing: 6
                        columnSpacing: 6

                        Repeater {
                            model: [
                                { label: "SID", value: sidBackend.sidChips + (sidBackend.sidChips > 1 ? " Chips" : " Chip") },
                                { label: "MODELL", value: sidBackend.chipModel },
                                { label: "SYSTEM", value: sidBackend.c64Model },
                                { label: "LAUFZEIT", value: sidBackend.tuneLengthSec > 0 ? sidBackend.tuneLengthSec + "s" : "∞" }
                            ]
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                radius: 8
                                color: Qt.rgba(1,1,1,0.04)
                                border.color: glassBorder
                                border.width: 1
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 0
                                    Text {
                                        text: modelData.label
                                        font.pixelSize: 8
                                        font.letterSpacing: 1
                                        color: textDim
                                    }
                                    Text {
                                        text: modelData.value
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        color: textMain
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Effekt-Panel (ausklappbar, dezent) ──
            Rectangle {
                id: fxPanel
                visible: false
                Layout.fillWidth: true
                Layout.preferredHeight: 46
                radius: 12
                color: Qt.rgba(0.07, 0.10, 0.17, 0.85)
                border.color: glassBorder
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 12

                    Text { text: "🎛"; font.pixelSize: 14 }

                    Text { text: "Reverb"; font.pixelSize: 11; color: textDim }
                    Slider {
                        id: reverbSlider
                        from: 0; to: 1; stepSize: 0.01
                        Layout.preferredWidth: 120
                        value: sidBackend.fxReverb
                        onMoved: sidBackend.fxReverb = value
                    }

                    Text { text: "Echo"; font.pixelSize: 11; color: textDim }
                    Slider {
                        id: echoSlider
                        from: 0; to: 1; stepSize: 0.01
                        Layout.preferredWidth: 120
                        value: sidBackend.fxEcho
                        onMoved: sidBackend.fxEcho = value
                    }

                    Text { text: "Spatial"; font.pixelSize: 11; color: textDim }
                    Slider {
                        id: spatialSlider
                        from: 0; to: 1; stepSize: 0.01
                        Layout.preferredWidth: 120
                        value: sidBackend.fxSpatial
                        onMoved: sidBackend.fxSpatial = value
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: "100% pur ohne Effekte — Regler nach rechts = mehr"
                        font.pixelSize: 9
                        color: textDim
                    }
                }
            }

            // Steuerleiste (direkt unter Song-Info — kein Leerraum mehr)
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Rectangle {
                    width: prevLabel.width + 34
                    height: 42
                    radius: 12
                    color: Qt.rgba(1,1,1,0.05)
                    border.color: glassBorder
                    border.width: 1
                    Text {
                        id: prevLabel
                        anchors.centerIn: parent
                        text: "⏮"
                        font.pixelSize: 15
                        color: textMain
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: sidBackend.prevTrack()
                    }
                }

                Rectangle {
                    width: openLabel.width + 36
                    height: 42
                    radius: 12
                    color: Qt.rgba(1,1,1,0.05)
                    border.color: glassBorder
                    border.width: 1
                    Text {
                        id: openLabel
                        anchors.centerIn: parent
                        text: "📂  Öffnen"
                        font.pixelSize: 13
                        color: textMain
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            browserWindow.mode = "open"
                            root.refreshPlaces()
                            sidBackend.goHome()
                            browserWindow.show()
                        }
                    }
                }

                Rectangle {
                    width: 120
                    height: 42
                    radius: 12
                    color: sidBackend.isPlaying ? Qt.rgba(0.70, 0.53, 1.0, 0.18)
                                                : Qt.rgba(0.31, 0.77, 0.97, 0.18)
                    border.color: sidBackend.isPlaying ? accent2 : accent
                    border.width: 1.5
                    Text {
                        anchors.centerIn: parent
                        text: sidBackend.isPlaying ? "⏹  Stopp" : "▶  Play"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: sidBackend.isPlaying ? accent2 : accent
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (sidBackend.isPlaying) sidBackend.stop()
                            else sidBackend.play()
                        }
                    }
                }

                Rectangle {
                    width: nextLabel.width + 34
                    height: 42
                    radius: 12
                    color: Qt.rgba(1,1,1,0.05)
                    border.color: glassBorder
                    border.width: 1
                    Text {
                        id: nextLabel
                        anchors.centerIn: parent
                        text: "⏭"
                        font.pixelSize: 15
                        color: textMain
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: sidBackend.nextTrack()
                    }
                }

                Item { Layout.fillWidth: true }

                // ── WAV-Export-Button ──
                Rectangle {
                    id: exportBtn
                    width: exportLabel.width + 34
                    height: 42
                    radius: 12
                    color: Qt.rgba(1,1,1,0.05)
                    border.color: glassBorder
                    border.width: 1
                    Text {
                        id: exportLabel
                        anchors.centerIn: parent
                        text: "💾 WAV"
                        font.pixelSize: 12
                        color: textMain
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (sidBackend.filePath.length === 0) return
                            exportDialog.open()
                        }
                    }
                }

                // ── Effekt-Button + Panel (Reverb/Echo/Spatial) ──
                Rectangle {
                    id: fxBtn
                    width: fxLabel.width + 34
                    height: 42
                    radius: 12
                    color: fxPanel.visible ? Qt.rgba(1,1,1,0.10) : Qt.rgba(1,1,1,0.05)
                    border.color: fxPanel.visible ? accent : glassBorder
                    border.width: 1
                    Text {
                        id: fxLabel
                        anchors.centerIn: parent
                        text: "🎛 Effekte"
                        font.pixelSize: 12
                        color: fxPanel.visible ? accent : textMain
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: fxPanel.visible = !fxPanel.visible
                    }
                }

                Text {
                    text: sidBackend.filePath.length > 0 ? sidBackend.filePath : ""
                    font.pixelSize: 10
                    color: textDim
                    elide: Text.ElideLeft
                    Layout.maximumWidth: 300
                }
            }
        }

        // ═══════════ Playlist ═══════════
        Rectangle {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            radius: 16
            color: Qt.rgba(0.07, 0.10, 0.17, 0.85)
            border.color: glassBorder
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "PLAYLIST  (" + sidBackend.playlistCount + ")"
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1.5
                        color: textMain
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        width: clearLabel.width + 20
                        height: 24
                        radius: 7
                        color: Qt.rgba(1,1,1,0.04)
                        border.color: glassBorder
                        border.width: 1
                        Text {
                            id: clearLabel
                            anchors.centerIn: parent
                            text: "✕ leeren"
                            font.pixelSize: 10
                            color: textDim
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: sidBackend.clearPlaylist()
                        }
                    }
                }

                // Leere Playlist: Live-Mini-Equalizer statt toter Fläche
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: sidBackend.playlistCount === 0
                    clip: true

                    // 7 animierte Equalizer-Balken (tanzen zum Sound)
                    Row {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: -14
                        spacing: 8
                        Repeater {
                            model: 7
                            Rectangle {
                                property int barIndex: index
                                width: 6
                                height: 60
                                radius: 3
                                color: Qt.rgba(0.31, 0.77, 0.97, 0.55)
                                y: (60 - height) / 2
                                SequentialAnimation on height {
                                    id: barAnim
                                    running: sidBackend.isPlaying
                                    loops: Animation.Infinite
                                    PauseAnimation { duration: barIndex * 90 }
                                    NumberAnimation { to: 10 + barIndex * 6; duration: 220; easing.type: Easing.InOutQuad }
                                    NumberAnimation { to: 50 + (barIndex % 3) * 8; duration: 260; easing.type: Easing.InOutQuad }
                                }
                            }
                        }
                    }

                    // Pulsierender Hinweis
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 22
                        text: "Playlist ist leer — füge Songs hinzu ✨"
                        font.pixelSize: 11
                        color: textDim
                        opacity: 0.8
                        SequentialAnimation on opacity {
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.35; duration: 1600; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 0.8; duration: 1600; easing.type: Easing.InOutSine }
                        }
                    }

                    // Dezente Note im Hintergrund
                    Text {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: 18
                        text: "🎵"
                        font.pixelSize: 40
                        opacity: 0.10
                    }
                }

                ListView {
                    id: playlistView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: sidBackend.playlistCount > 0
                    spacing: 4
                    clip: true
                    model: sidBackend.playlist
                    delegate: Rectangle {
                        width: playlistView.width
                        height: 34
                        radius: 8
                        color: index === sidBackend.playlistIndex
                               ? Qt.rgba(0.31, 0.77, 0.97, 0.14)
                               : (mouse.containsMouse ? Qt.rgba(1,1,1,0.05) : "transparent")
                        border.color: index === sidBackend.playlistIndex ? accent : "transparent"
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 8
                            spacing: 6
                            Text {
                                text: index + 1
                                font.pixelSize: 10
                                color: index === sidBackend.playlistIndex ? accent : textDim
                            }
                            Text {
                                Layout.fillWidth: true
                                text: {
                                    var parts = modelData.split("/")
                                    return parts[parts.length - 1].replace(".sid", "")
                                }
                                font.pixelSize: 12
                                color: index === sidBackend.playlistIndex ? accent : textMain
                                elide: Text.ElideRight
                            }
                            Text {
                                text: "✕"
                                font.pixelSize: 10
                                color: textDim
                                opacity: mouse.containsMouse ? 1.0 : 0.3
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: sidBackend.removeFromPlaylist(index)
                                }
                            }
                        }
                        MouseArea {
                            id: mouse
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: sidBackend.playFromPlaylist(index)
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 36
                    radius: 9
                    color: Qt.rgba(0.31, 0.77, 0.97, 0.10)
                    border.color: glassBorder
                    border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: "＋ Zur Playlist hinzufügen"
                        font.pixelSize: 11
                        color: accent
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            browserWindow.mode = "add"
                            root.refreshPlaces()
                            sidBackend.goHome()
                            browserWindow.show()
                        }
                    }
                }
            }
        }
    }

    // ── Separates Browser-Fenster (Orte + Dateiliste) ──
    ApplicationWindow {
        id: browserWindow
        visible: false
        width: 820
        height: 560
        color: "#0a0e18"
        title: "PrismaSID — Datei-Browser"
        // Volle native Fenster-Flags — unter Windows gibt es sonst KEINEN Schließen-Button
        // (Qt rendert bei unvollständigen Flags eine eigene Titelleiste ohne Close)
        flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint
             | Qt.WindowMinimizeButtonHint | Qt.WindowMaximizeButtonHint
             | Qt.WindowCloseButtonHint

        // Modus: "open" = abspielen, "add" = zur Playlist
        property string mode: "open"

        // Eigene Farben (root-Properties gelten nur im Hauptfenster)
        readonly property color accent: "#4fc3f7"
        readonly property color accent2: "#b388ff"
        readonly property color textMain: "#e8eef7"
        readonly property color textDim: "#8fa3bf"
        readonly property color glassBorder: Qt.rgba(0.53, 0.64, 0.75, 0.18)

        // Orte-Liste (JS-Kopie vom Backend)
        property var placesList: []
        function refreshPlaces() {
            placesList = sidBackend.places
            if (placesList.length === 0) {
                placesList = [{ name: "🏠 Home", path: "/home/shadowwrath" }]
            }
        }
        onVisibleChanged: if (visible) refreshPlaces()

        Rectangle {
            anchors.fill: parent
            color: "#0a0e18"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 16

                // ═══ Orte-Leiste ═══
                Rectangle {
                    Layout.preferredWidth: 190
                    Layout.fillHeight: true
                    radius: 12
                    color: Qt.rgba(0.07, 0.10, 0.17, 0.9)
                    border.color: glassBorder
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Text {
                            text: "ORTE"
                            font.pixelSize: 10
                            font.letterSpacing: 2
                            color: textDim
                        }

                        Repeater {
                            model: browserWindow.placesList
                            delegate: Rectangle {
                                width: parent.width
                                height: 34
                                radius: 8
                                color: sidBackend.currentDir === modelData.path
                                       ? Qt.rgba(0.31, 0.77, 0.97, 0.14)
                                       : (placeMouse.containsMouse ? Qt.rgba(1,1,1,0.05) : "transparent")
                                border.color: sidBackend.currentDir === modelData.path ? accent : "transparent"
                                border.width: 1
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    text: (modelData.isSmb ? "🖴 " : "") + modelData.name
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    width: parent.width - 20
                                    color: sidBackend.currentDir === modelData.path ? accent : textMain
                                }
                                MouseArea {
                                    id: placeMouse
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: sidBackend.openPlace(modelData.path)
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                // ═══ Dateiliste ═══
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 10

                    // Pfad + Navigation
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            width: 38
                            height: 34
                            radius: 9
                            color: Qt.rgba(1,1,1,0.05)
                            border.color: glassBorder
                            border.width: 1
                            Text {
                                anchors.centerIn: parent
                                text: "⬆"
                                font.pixelSize: 14
                                color: textMain
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: sidBackend.goUp()
                            }
                        }

                        // Cover-Modus-Toggle
                        Rectangle {
                            width: 44
                            height: 34
                            radius: 9
                            color: sidBackend.coverMode ? Qt.rgba(0.70, 0.53, 1.0, 0.20) : Qt.rgba(1,1,1,0.05)
                            border.color: sidBackend.coverMode ? accent2 : glassBorder
                            border.width: 1
                            Text {
                                anchors.centerIn: parent
                                text: "🖼"
                                font.pixelSize: 14
                                color: sidBackend.coverMode ? accent2 : textMain
                            }
                            Text {
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "Cover"
                                font.pixelSize: 7
                                color: sidBackend.coverMode ? accent2 : textDim
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: sidBackend.coverMode = !sidBackend.coverMode
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 34
                            radius: 9
                            color: Qt.rgba(0.05, 0.08, 0.14, 0.9)
                            border.color: glassBorder
                            border.width: 1
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: sidBackend.currentDir
                                font.pixelSize: 12
                                color: textDim
                                elide: Text.ElideLeft
                            }
                        }
                    }

                    // Hinweis wenn Cover-Modus an (Cover müssen separat geladen werden)
                    Rectangle {
                        Layout.fillWidth: true
                        visible: sidBackend.coverMode
                        height: 30
                        radius: 8
                        color: Qt.rgba(0.70, 0.53, 1.0, 0.10)
                        border.color: Qt.rgba(0.70, 0.53, 1.0, 0.3)
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: "🖼 Cover-Modus an — Cover-Bilder (cover.jpg/png) müssen in den Album-Ordnern liegen oder separat geladen werden"
                            font.pixelSize: 10
                            color: accent2
                            elide: Text.ElideRight
                        }
                    }

                    // Einträge — Listen-Ansicht (Cover-Modus AUS)
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !sidBackend.coverMode
                        clip: true
                        spacing: 3
                        model: sidBackend.dirEntries
                        delegate: Rectangle {
                            width: parent.width
                            height: 38
                            radius: 8
                            color: entryMouse.containsMouse ? Qt.rgba(1,1,1,0.06) : "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 10
                                spacing: 10
                                Text {
                                    text: modelData.isDir ? "📁" : "🎵"
                                    font.pixelSize: 14
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    font.pixelSize: 13
                                    color: modelData.isDir ? textMain : accent
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: modelData.isDir ? "öffnen" : (browserWindow.mode === "open" ? "▶ spielen" : "＋")
                                    font.pixelSize: 10
                                    color: modelData.isDir ? textDim : accent2
                                }
                            }
                            MouseArea {
                                id: entryMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (modelData.isDir) {
                                        if (modelData.isSmb) {
                                            sidBackend.openPlace(modelData.path)
                                        } else {
                                            sidBackend.setDir(modelData.path)
                                        }
                                    } else if (browserWindow.mode === "open") {
                                        if (modelData.isSmb) {
                                            sidBackend.openSmbFile(modelData.path)
                                        } else {
                                            sidBackend.openDirOrFile(modelData.path)
                                        }
                                    } else {
                                        if (modelData.isSmb) {
                                            sidBackend.addSmbToPlaylist(modelData.path)
                                        } else {
                                            sidBackend.addDirOrFile(modelData.path)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Einträge — Kachel-Ansicht (Cover-Modus AN, Coverflow-Gefühl)
                    GridView {
                        id: entryGrid
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: sidBackend.coverMode
                        clip: true
                        cellWidth: 130
                        cellHeight: 150
                        cacheBuffer: 500
                        model: sidBackend.dirEntries
                        delegate: Rectangle {
                            width: 118
                            height: 140
                            radius: 12
                            color: entryMouse.containsMouse ? Qt.rgba(1,1,1,0.07) : Qt.rgba(0.05, 0.08, 0.14, 0.6)
                            border.color: entryMouse.containsMouse ? Qt.rgba(0.31, 0.77, 0.97, 0.4) : glassBorder
                            border.width: 1

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 6

                                // Cover / Icon-Bereich
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: 8
                                    clip: true
                                    color: modelData.coverColor ? modelData.coverColor : "#1a2233"

                                    // Cover-Bild wenn vorhanden
                                    Image {
                                        anchors.fill: parent
                                        source: modelData.coverImage ? modelData.coverImage : ""
                                        fillMode: Image.PreserveAspectCrop
                                        visible: status === Image.Ready
                                    }

                                    // Generierte Initialen wenn kein Cover
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.isDir ? (modelData.coverInitials ? modelData.coverInitials : "📁") : "🎵"
                                        font.pixelSize: modelData.isDir ? 30 : 34
                                        font.weight: Font.DemiBold
                                        color: "#ffffff"
                                        opacity: 0.85
                                        visible: !(modelData.coverImage && modelData.coverImage.length > 0) || !modelData.isDir
                                    }

                                    // Ordner-Symbol-Ecke
                                    Text {
                                        anchors.top: parent.top
                                        anchors.left: parent.left
                                        anchors.margins: 6
                                        text: modelData.isDir ? "📁" : ""
                                        font.pixelSize: 12
                                        opacity: 0.9
                                    }
                                }

                                // Name
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    font.pixelSize: 11
                                    color: modelData.isDir ? textMain : accent
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignHCenter
                                }
                            }

                            MouseArea {
                                id: entryMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (modelData.isDir) {
                                        if (modelData.isSmb) {
                                            sidBackend.openPlace(modelData.path)
                                        } else {
                                            sidBackend.setDir(modelData.path)
                                        }
                                    } else if (browserWindow.mode === "open") {
                                        if (modelData.isSmb) {
                                            sidBackend.openSmbFile(modelData.path)
                                        } else {
                                            sidBackend.openDirOrFile(modelData.path)
                                        }
                                    } else {
                                        if (modelData.isSmb) {
                                            sidBackend.addSmbToPlaylist(modelData.path)
                                        } else {
                                            sidBackend.addDirOrFile(modelData.path)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Aktion unten
                    Text {
                        text: sidBackend.loading ? "⏳ Lädt Netzwerkordner…"
                             : (browserWindow.mode === "open"
                                ? "Klick auf einen Song → spielt sofort"
                                : "Klick auf ＋ → fügt zur Playlist hinzu")
                        font.pixelSize: 10
                        color: sidBackend.loading ? accent : textDim
                    }

                    // Ordner komplett (samt Unterordner) zur Playlist
                    Rectangle {
                        Layout.fillWidth: true
                        visible: browserWindow.mode === "add"
                        height: 38
                        radius: 10
                        color: Qt.rgba(0.70, 0.53, 1.0, 0.12)
                        border.color: accent2
                        border.width: 1
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 10
                            spacing: 8
                            Text {
                                text: "📁"
                                font.pixelSize: 14
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "Ganzen Ordner + Unterordner zur Playlist"
                                font.pixelSize: 11
                                color: textMain
                                elide: Text.ElideRight
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: sidBackend.addFolderRecursive(sidBackend.currentDir)
                        }
                    }
                }
            }
        }
    }

    // ── Dialoge ──
    FileDialog {
        id: fileDialog
        title: "SID-Datei öffnen"
        nameFilters: ["SID-Dateien (*.sid *.mus)", "Alle Dateien (*)"]
        onAccepted: {
            sidBackend.loadFile(selectedFile)
            sidBackend.play()
        }
    }

    FileDialog {
        id: addDialog
        title: "Zur Playlist hinzufügen"
        nameFilters: ["SID-Dateien (*.sid *.mus)", "Alle Dateien (*)"]
        onAccepted: sidBackend.addToPlaylist(selectedFile)
    }

    // ── WAV-Export-Dialog ──
    FileDialog {
        id: exportDialog
        title: "WAV exportieren"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wav"
        nameFilters: ["WAV-Dateien (*.wav)"]
        onAccepted: sidBackend.exportWav(selectedFile)
    }

    // ── Export-Fortschritt (Toast unten rechts) ──
    Connections {
        target: sidBackend
        function onExportProgress(current, total) {
            exportToast.text = "💾 Exportiere Subsong " + current + "/" + total + " …"
            exportToast.visible = true
            exportTimer.restart()
        }
        function onExportFinished(success, filePath) {
            if (success) {
                exportToast.text = "✅ WAV fertig: " + filePath
            } else {
                exportToast.text = "❌ Export fehlgeschlagen"
            }
            exportToast.visible = true
            exportTimer.restart()
        }
        function onInfoMessage(message) {
            exportToast.text = message
            exportToast.visible = true
            exportTimer.restart()
        }
    }

    Timer {
        id: exportTimer
        interval: 5000
        onTriggered: exportToast.visible = false
    }

    Rectangle {
        id: exportToast
        visible: false
        z: 100
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        width: Math.max(320, exportToastText.width + 40)
        height: 44
        radius: 12
        color: Qt.rgba(0.10, 0.14, 0.24, 0.95)
        border.color: accent
        border.width: 1
        property string text: ""
        Text {
            id: exportToastText
            anchors.centerIn: parent
            text: exportToast.text
            font.pixelSize: 12
            color: textMain
            elide: Text.ElideRight
            width: parent.width - 30
        }
    }
}
