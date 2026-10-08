import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    // Basecamp sizes the MDI subwindow from the root's sizeHint.
    implicitWidth: 1024
    implicitHeight: 720

    // Typed replica: auto-synced properties and callable slots.
    readonly property var backend: logos.module("presence_ui")
    property bool ready: false

    // --- backend state (auto-synced PROPs, defensively coerced) ---
    readonly property string radioMode: ready && backend && backend.radioMode ? backend.radioMode : "sim"
    readonly property string addressPolicy: ready && backend && backend.addressPolicy ? backend.addressPolicy : "aligned"
    readonly property string anonymityLevel: ready && backend && backend.anonymityLevel ? backend.anonymityLevel : "Preferred"
    readonly property string preset: ready && backend && backend.preset ? backend.preset : "logos.test"
    readonly property string roomCode: ready && backend && backend.roomCode ? backend.roomCode : "lobby"
    readonly property int epochSeconds: ready && backend && backend.epochSeconds !== undefined ? backend.epochSeconds : 900
    readonly property int simSpeed: ready && backend && backend.simSpeed !== undefined ? backend.simSpeed : 60
    readonly property int simPeers: ready && backend && backend.simPeers !== undefined ? backend.simPeers : 3
    readonly property var epoch: ready && backend && backend.epoch !== undefined ? backend.epoch : 0
    readonly property int epochSecondsLeft: ready && backend && backend.epochSecondsLeft !== undefined ? backend.epochSecondsLeft : 0
    readonly property string clockText: ready && backend && backend.clockText ? backend.clockText : ""
    readonly property int headcount: ready && backend && backend.headcount !== undefined ? backend.headcount : 0
    readonly property string friendsJson: ready && backend && backend.friendsJson ? backend.friendsJson : "[]"
    readonly property string contactsJson: ready && backend && backend.contactsJson ? backend.contactsJson : "[]"
    readonly property string beaconJson: ready && backend && backend.beaconJson ? backend.beaconJson : "[]"
    readonly property string observationsJson: ready && backend && backend.observationsJson ? backend.observationsJson : "[]"
    readonly property string linkReportJson: ready && backend && backend.linkReportJson ? backend.linkReportJson : "{}"
    readonly property string networkState: ready && backend && backend.networkState ? backend.networkState : "offline"
    readonly property string networkInfo: ready && backend && backend.networkInfo ? backend.networkInfo : ""
    readonly property string identityLabel: ready && backend && backend.identityLabel ? backend.identityLabel : ""
    readonly property string statusText: ready && backend && backend.status ? backend.status : ""
    readonly property string lastPairingCode: ready && backend && backend.lastPairingCode ? backend.lastPairingCode : ""

    function parseJson(s, fallback) {
        try { return JSON.parse(s) } catch (e) { return fallback }
    }
    readonly property var friends: parseJson(friendsJson, [])
    readonly property var contacts: parseJson(contactsJson, [])
    readonly property var beacon: parseJson(beaconJson, [])
    readonly property var observations: parseJson(observationsJson, [])
    readonly property var report: parseJson(linkReportJson, {})

    // --- local UI state: dialogs are in-scene overlays (Popup does not paint
    // reliably inside Basecamp's embedded QQuickWidget) ---
    property bool showPairing: false
    property bool showAddContact: false
    property bool showIdentityConfirm: false
    property string noticeText: ""

    // --- palette ---
    readonly property color cBg: "#0E1013"
    readonly property color cCard: "#15181C"
    readonly property color cCardAlt: "#191C20"
    readonly property color cBorder: "#2A2F35"
    readonly property color cText: "#E8EAED"
    readonly property color cMuted: "#6E747A"
    readonly property color cAccent: "#C9A96A"
    readonly property color cGreen: "#5FA97E"
    readonly property color cRed: "#C96A6A"
    readonly property color cBlue: "#6A9AC9"

    function fmtDuration(s) {
        s = Math.max(0, Math.round(s))
        if (s >= 3600) return Math.floor(s / 3600) + " h " + Math.floor((s % 3600) / 60) + " min"
        if (s >= 60) return Math.floor(s / 60) + " min " + (s % 60) + " s"
        return s + " s"
    }
    function epochLabel(sec) {
        if (sec % 3600 === 0) return (sec / 3600) + " h"
        if (sec % 60 === 0) return (sec / 60) + " min"
        return sec + " s"
    }

    Connections {
        target: logos
        function onViewModuleReadyChanged(moduleName, isReady) {
            if (moduleName === "presence_ui")
                root.ready = isReady && root.backend !== null
        }
    }
    Component.onCompleted: {
        root.ready = root.backend !== null && logos.isViewModuleReady("presence_ui")
    }
    Connections {
        target: root.backend
        enabled: root.backend !== null
        function onPairingCodeReady(code) { root.showPairing = true }
        function onNotice(text) { root.noticeText = text; noticeTimer.restart() }
    }
    Timer { id: noticeTimer; interval: 6000; onTriggered: root.noticeText = "" }

    // Dark-styled controls; the default Basic style ships light-gray chrome.
    component DarkButton: Button {
        id: db
        property bool danger: false
        property bool accent: false
        font.pixelSize: 12
        leftPadding: 12
        rightPadding: 12
        topPadding: 7
        bottomPadding: 7
        contentItem: Text {
            text: db.text
            font: db.font
            color: !db.enabled ? "#4A5057" : db.danger ? root.cRed : db.accent ? root.cAccent : root.cText
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            implicitHeight: 30
            radius: 6
            color: db.down ? "#262B31" : db.hovered ? "#22262B" : "#1D2126"
            border.color: db.accent ? root.cAccent : root.cBorder
        }
    }
    component DarkField: TextField {
        color: root.cText
        placeholderTextColor: "#4E5762"
        leftPadding: 10
        rightPadding: 10
        font.pixelSize: 12
        background: Rectangle {
            implicitHeight: 32
            radius: 6
            color: "#10131A"
            border.color: parent.activeFocus ? root.cAccent : root.cBorder
        }
    }
    component Card: Rectangle {
        default property alias content: cardCol.data
        property string title: ""
        Layout.fillWidth: true
        implicitHeight: cardCol.implicitHeight + 24
        radius: 10
        color: root.cCard
        border.color: root.cBorder
        ColumnLayout {
            id: cardCol
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
        }
    }
    component CardTitle: Text {
        font.pixelSize: 11
        font.bold: true
        font.letterSpacing: 1
        color: root.cMuted
    }
    component Seg: Rectangle {
        id: seg
        // A segmented choice: model = [{label, value}], current = value
        property var model: []
        property string current: ""
        signal picked(string value)
        Layout.fillWidth: true
        height: 30
        radius: 7
        color: root.cCardAlt
        border.color: root.cBorder
        RowLayout {
            anchors.fill: parent
            anchors.margins: 3
            spacing: 3
            Repeater {
                model: seg.model
                Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 5
                    color: seg.current === modelData.value ? "#262B31" : "transparent"
                    border.color: seg.current === modelData.value ? root.cAccent : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: modelData.label
                        font.pixelSize: 11
                        font.bold: seg.current === modelData.value
                        color: seg.current === modelData.value ? root.cText : root.cMuted
                    }
                    MouseArea { anchors.fill: parent; onClicked: seg.picked(modelData.value) }
                }
            }
        }
    }
    component Tile: Rectangle {
        id: tile
        property string value: ""
        property string label: ""
        property color valueColor: root.cText
        Layout.fillWidth: true
        implicitHeight: 54
        radius: 8
        color: root.cCardAlt
        border.color: root.cBorder
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 2
            Text { text: tile.value; font.pixelSize: 18; font.bold: true; color: tile.valueColor }
            Text { text: tile.label; font.pixelSize: 10; color: root.cMuted; elide: Text.ElideRight; Layout.fillWidth: true }
        }
    }

    Rectangle { anchors.fill: parent; color: root.cBg }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        // ---------------------------------------------------------- header
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                spacing: 1
                Text { text: "Presence"; font.pixelSize: 20; font.bold: true; color: root.cText }
                Text { text: "Unlinkable presence pilot: rotating beacons, friend recognition, observer view"; font.pixelSize: 11; color: root.cMuted }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                radius: 6; color: root.cCardAlt; border.color: root.cBorder
                implicitHeight: 30; implicitWidth: epochRow.implicitWidth + 20
                RowLayout {
                    id: epochRow
                    anchors.centerIn: parent
                    spacing: 10
                    Text { text: root.clockText; font.pixelSize: 11; color: root.cMuted }
                    Text { text: "epoch " + root.epoch; font.pixelSize: 11; font.bold: true; color: root.cAccent }
                    Text { text: "rotates in " + root.fmtDuration(root.epochSecondsLeft); font.pixelSize: 11; color: root.cText }
                }
            }
            Rectangle {
                radius: 6; implicitHeight: 30; implicitWidth: connText.implicitWidth + 20
                color: root.ready ? "#17251D" : "#2A2016"
                border.color: root.ready ? root.cGreen : root.cAccent
                Text { id: connText; anchors.centerIn: parent; text: root.ready ? "Connected" : "Connecting to backend..."; font.pixelSize: 11; color: root.ready ? root.cGreen : root.cAccent }
            }
        }

        // ---------------------------------------------------------- body
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // ======================================== left: radio and identity
            Flickable {
                id: leftFlick
                Layout.preferredWidth: 262
                Layout.maximumWidth: 262
                Layout.fillHeight: true
                contentHeight: leftCol.implicitHeight
                clip: true
                ColumnLayout {
                    id: leftCol
                    width: leftFlick.width
                    spacing: 10

                    Card {
                        CardTitle { text: "RADIO" }
                        Seg {
                            model: [{ label: "SIMULATED", value: "sim" }, { label: "NETWORK", value: "network" }, { label: "BOTH", value: "both" }]
                            current: root.radioMode
                            onPicked: function(v) { if (root.backend) root.backend.applyRadioMode(v) }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: root.cMuted
                            text: root.radioMode === "sim"
                                  ? "A room of synthetic beacons plus your own. A passive sniffer logs every advert."
                                  : root.radioMode === "network"
                                    ? "Your beacon is published on a Logos content topic. Other Basecamps in the room see it."
                                    : "Both: the simulated room and the Logos network, on real time."
                        }
                    }

                    Card {
                        visible: root.radioMode !== "network"
                        CardTitle { text: "SIMULATED ROOM" }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Strangers"; font.pixelSize: 12; color: root.cText; Layout.fillWidth: true }
                            DarkButton { text: "−"; implicitWidth: 34; onClicked: if (root.backend) root.backend.applySimPeers(root.simPeers - 1) }
                            Text { text: root.simPeers; font.pixelSize: 13; font.bold: true; color: root.cText; horizontalAlignment: Text.AlignHCenter; Layout.preferredWidth: 24 }
                            DarkButton { text: "+"; implicitWidth: 34; onClicked: if (root.backend) root.backend.applySimPeers(root.simPeers + 1) }
                        }
                        Text { text: "Simulated clock"; font.pixelSize: 12; color: root.cText }
                        Seg {
                            model: [{ label: "1x", value: "1" }, { label: "10x", value: "10" }, { label: "60x", value: "60" }, { label: "300x", value: "300" }]
                            current: String(root.simSpeed)
                            enabled: root.networkState === "offline"
                            opacity: enabled ? 1 : 0.5
                            onPicked: function(v) { if (root.backend) root.backend.applySimSpeed(parseInt(v)) }
                        }
                        Text { visible: root.networkState !== "offline"; text: "Real time while the network is up."; font.pixelSize: 10; color: root.cMuted }
                        Text { text: "Address policy"; font.pixelSize: 12; color: root.cText }
                        Seg {
                            model: [{ label: "ALIGNED", value: "aligned" }, { label: "DRIFTING", value: "drifting" }, { label: "FIXED", value: "fixed" }]
                            current: root.addressPolicy
                            onPicked: function(v) { if (root.backend) root.backend.applyAddressPolicy(v) }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: root.cMuted
                            text: root.addressPolicy === "aligned"
                                  ? "Address and payload rotate together at the epoch boundary. The strict driver."
                                  : root.addressPolicy === "drifting"
                                    ? "The address rotates on its own clock, like macOS. The overlap bridges epochs."
                                    : "The address never changes, like stock BlueZ. Everything links."
                        }
                    }

                    Card {
                        CardTitle { text: "EPOCH" }
                        Seg {
                            model: [{ label: "1 MIN", value: "60" }, { label: "5 MIN", value: "300" }, { label: "15 MIN", value: "900" }, { label: "1 H", value: "3600" }]
                            current: String(root.epochSeconds)
                            onPicked: function(v) { if (root.backend) root.backend.applyEpochSeconds(parseInt(v)) }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11; color: root.cMuted
                               text: "15 minutes matches the address cadence of Apple's stack. Shorter epochs make the demo faster." }
                    }

                    Card {
                        visible: root.radioMode !== "sim"
                        CardTitle { text: "LOGOS NETWORK" }
                        RowLayout {
                            Layout.fillWidth: true
                            DarkField { id: roomField; Layout.fillWidth: true; placeholderText: "room code"; text: root.roomCode }
                            DarkButton { text: "Set"; onClicked: if (root.backend) root.backend.applyRoomCode(roomField.text) }
                        }
                        Text { text: "Network preset (fixed when the node starts)"; font.pixelSize: 11; color: root.cText }
                        Seg {
                            model: [{ label: "LOGOS.TEST", value: "logos.test" }, { label: "LOGOS.DEV", value: "logos.dev" }]
                            current: root.preset
                            onPicked: function(v) { if (root.backend) root.backend.applyPreset(v) }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 10; color: root.cMuted
                               text: "logos.test needs a funded RLN membership to send; logos.dev has no rate-limit gate. Whichever app creates the node first fixes its preset." }
                        Text { text: "Anonymity level (fixed when the node starts)"; font.pixelSize: 11; color: root.cText }
                        Seg {
                            model: [{ label: "NONE", value: "None" }, { label: "PREFERRED", value: "Preferred" }, { label: "REQUIRED", value: "Required" }]
                            current: root.anonymityLevel
                            onPicked: function(v) { if (root.backend) root.backend.applyAnonymityLevel(v) }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            DarkButton { text: "Connect"; accent: true; enabled: root.networkState === "offline" || root.networkState === "error"; onClicked: if (root.backend) root.backend.connectNetwork() }
                            DarkButton { text: "Disconnect"; enabled: root.networkState !== "offline"; onClicked: if (root.backend) root.backend.disconnectNetwork() }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: root.networkState
                                font.pixelSize: 11; font.bold: true
                                color: root.networkState === "connected" ? root.cGreen : root.networkState === "error" ? root.cRed : root.cAccent
                            }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11; color: root.cMuted; text: root.networkInfo }
                    }

                    Card {
                        CardTitle { text: "IDENTITY AND CONTACTS" }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Seed fingerprint"; font.pixelSize: 12; color: root.cText; Layout.fillWidth: true }
                            Text { text: root.identityLabel; font.pixelSize: 12; font.family: "Menlo"; color: root.cAccent }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 10; color: root.cMuted
                               text: "Shown only to you. Nothing derived from it goes on the air unchanged." }
                        RowLayout {
                            Layout.fillWidth: true
                            DarkButton { text: "Pairing code"; accent: true; Layout.fillWidth: true; onClicked: if (root.backend) root.backend.createPairingCode() }
                            DarkButton { text: "Add contact"; Layout.fillWidth: true; onClicked: root.showAddContact = true }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            DarkButton { text: "Add sim friend"; Layout.fillWidth: true; visible: root.radioMode !== "network"; onClicked: if (root.backend) root.backend.addSimulatedFriend() }
                            DarkButton { text: "New identity"; danger: true; Layout.fillWidth: true; onClicked: root.showIdentityConfirm = true }
                        }
                        Repeater {
                            model: root.contacts
                            RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 6
                                Rectangle { width: 8; height: 8; radius: 4; color: modelData.sim ? root.cBlue : root.cAccent }
                                Text { text: modelData.name; font.pixelSize: 12; color: root.cText; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: modelData.role; font.pixelSize: 10; color: root.cMuted }
                                DarkButton { text: "×"; implicitWidth: 28; danger: true; onClicked: if (root.backend) root.backend.removeContact(modelData.name) }
                            }
                        }
                        Text { visible: root.contacts.length === 0; text: "No contacts yet."; font.pixelSize: 11; color: root.cMuted }
                    }
                }
            }

            // ======================================== centre: in this room
            ColumnLayout {
                Layout.preferredWidth: 290
                Layout.maximumWidth: 300
                Layout.fillHeight: true
                spacing: 10

                Card {
                    CardTitle { text: "IN THIS ROOM" }
                    Text { text: "In this room"; visible: false }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Text { text: root.headcount; font.pixelSize: 44; font.bold: true; color: root.cText }
                        ColumnLayout {
                            spacing: 2
                            Text { text: root.headcount === 1 ? "beacon so far this epoch" : "beacons so far this epoch"; font.pixelSize: 13; color: root.cText }
                            Text { text: "you included while your beacon is on"; font.pixelSize: 10; color: root.cMuted }
                            Text { text: "next epoch: a new count, no carry-over"; font.pixelSize: 10; color: root.cMuted }
                        }
                    }
                }

                Card {
                    CardTitle { text: "FRIENDS NEARBY" }
                    Repeater {
                        model: root.friends
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle { width: 10; height: 10; radius: 5; color: modelData.here ? root.cGreen : "#3A3F45" }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Text { text: modelData.name; font.pixelSize: 12; font.bold: modelData.here; color: root.cText; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text {
                                    font.pixelSize: 10; color: root.cMuted
                                    text: modelData.here
                                          ? "here now, via " + modelData.source
                                          : modelData.agoSeconds >= 0 ? "last seen " + root.fmtDuration(modelData.agoSeconds) + " ago"
                                                                      : "not seen in the last two epochs"
                                }
                            }
                        }
                    }
                    Text {
                        visible: root.friends.length === 0
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: "Pair with someone (or add a simulated friend) and they appear here when their tag is on the air. Nobody else can tell."
                        font.pixelSize: 11; color: root.cMuted
                    }
                }

                Card {
                    Layout.fillHeight: true
                    CardTitle { text: "YOUR BEACON THIS EPOCH" }
                    Repeater {
                        model: root.beacon
                        ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 0
                            Text { text: modelData.hex; font.pixelSize: 10; font.family: "Menlo"; color: root.cAccent; elide: Text.ElideRight; Layout.fillWidth: true }
                            Text { text: modelData.label; font.pixelSize: 10; color: root.cMuted }
                        }
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 10; color: root.cMuted
                           text: "Four 16-byte slots, always four: the labels exist only here. An observer sees four equal-looking values that all change next epoch." }
                    Item { Layout.fillHeight: true }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 10; color: root.cMuted; text: root.statusText }
                }
            }

            // ======================================== right: observer view
            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                CardTitle { text: "OBSERVER VIEW" }
                Text { text: "Observer view"; visible: false }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11; color: root.cMuted
                       text: "What a passive sniffer in the room logs, and the DP-3T linkability check over it: did any payload survive an address change, did any address survive an epoch change, and how long is the longest trail one device leaves." }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 8
                    rowSpacing: 8
                    Tile { value: String(root.report.observations || 0); label: "adverts" }
                    Tile { value: String(root.report.addresses || 0); label: "addresses" }
                    Tile { value: String(root.report.slots || 0); label: "slots" }
                    Tile { value: String(root.report.epochs || 0); label: "epochs" }
                    Tile { value: String(root.report.slotsBridgingAddresses || 0); label: "slot bridges"; valueColor: (root.report.slotsBridgingAddresses || 0) > 0 ? root.cRed : root.cGreen }
                    Tile { value: String(root.report.addressesBridgingEpochs || 0); label: "address bridges"; valueColor: (root.report.addressesBridgingEpochs || 0) > 0 ? root.cRed : root.cGreen }
                    Tile { value: String(root.report.trails || 0); label: "trails" }
                    Tile {
                        value: (root.report.longestTrailEpochs || 0) + (root.report.longestTrailEpochs === 1 ? " epoch" : " epochs")
                        label: "longest trail"
                        valueColor: (root.report.longestTrailEpochs || 0) > 1 ? root.cRed : root.cGreen
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Linkability"; font.pixelSize: 11; font.bold: true; color: root.cText }
                    Text { text: root.report.unlinkable ? "verdict-unlinkable" : (root.report.epochs || 0) >= 2 && (root.report.observations || 0) > 0 ? "verdict-linkable" : "verdict-pending"; visible: false }
                    Text { text: (root.report.epochs || 0) >= 2 ? "epochs-two-plus" : "epochs-one"; visible: false }
                    Text { text: (root.report.observations || 0) > 0 ? "observations-present" : "observations-absent"; visible: false }
                    Text { text: root.friends.some(function(f) { return f.here }) ? "friend-here" : "friend-absent"; visible: false }
                    Item { Layout.fillWidth: true }
                    Text { text: "over " + (root.report.epochs || 0) + (root.report.epochs === 1 ? " epoch" : " epochs") + ", longest trail " + root.fmtDuration(root.report.longestTrailSeconds || 0); font.pixelSize: 11; color: root.cMuted }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: verdictText.implicitHeight + 20
                    radius: 8
                    color: root.report.unlinkable ? "#17251D" : (root.report.epochs || 0) >= 2 && (root.report.observations || 0) > 0 ? "#2A1A1A" : root.cCardAlt
                    border.color: root.report.unlinkable ? root.cGreen : (root.report.epochs || 0) >= 2 && (root.report.observations || 0) > 0 ? root.cRed : root.cBorder
                    Text {
                        id: verdictText
                        anchors.fill: parent; anchors.margins: 10
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: root.cText
                        text: root.report.verdict || ""
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Latest adverts (newest first)"; font.pixelSize: 11; color: root.cMuted; Layout.fillWidth: true }
                    DarkButton { text: "Clear log"; onClicked: if (root.backend) root.backend.clearObservations() }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "time"; font.pixelSize: 10; color: root.cMuted; Layout.preferredWidth: 60 }
                    Text { text: "epoch"; font.pixelSize: 10; color: root.cMuted; Layout.preferredWidth: 64 }
                    Text { text: "address"; font.pixelSize: 10; color: root.cMuted; Layout.preferredWidth: 120 }
                    Text { text: "slot"; font.pixelSize: 10; color: root.cMuted; Layout.fillWidth: true }
                    Text { text: "via"; font.pixelSize: 10; color: root.cMuted; Layout.preferredWidth: 52 }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.observations
                    spacing: 2
                    delegate: RowLayout {
                        required property var modelData
                        width: ListView.view.width
                        spacing: 8
                        Text { text: modelData.time; font.pixelSize: 10; font.family: "Menlo"; color: root.cText; Layout.preferredWidth: 60 }
                        Text { text: modelData.epoch; font.pixelSize: 10; font.family: "Menlo"; color: root.cAccent; Layout.preferredWidth: 64 }
                        Text { text: modelData.addr; font.pixelSize: 10; font.family: "Menlo"; color: root.cText; Layout.preferredWidth: 120 }
                        Text { text: modelData.slot; font.pixelSize: 10; font.family: "Menlo"; color: root.cMuted; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        Text { text: modelData.src; font.pixelSize: 10; color: modelData.src === "network" ? root.cBlue : root.cMuted; Layout.preferredWidth: 52 }
                    }
                }
            }
        }

        // ---------------------------------------------------------- footer
        RowLayout {
            Layout.fillWidth: true
            Text { text: root.noticeText; font.pixelSize: 11; color: root.cAccent; Layout.fillWidth: true; elide: Text.ElideRight }
            Text { text: "0.1.1 · simulated radio · not audited"; font.pixelSize: 10; color: root.cMuted }
        }
    }

    // ----------------------------------------------------------------------
    // Pairing code dialog (in-scene overlay)
    Item {
        anchors.fill: parent
        visible: root.showPairing
        z: 100
        Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.6; MouseArea { anchors.fill: parent; onClicked: root.showPairing = false } }
        Rectangle {
            width: 440
            anchors.centerIn: parent
            height: pairCol.implicitHeight + 40
            radius: 12
            color: root.cCard
            border.color: root.cBorder
            MouseArea { anchors.fill: parent }
            ColumnLayout {
                id: pairCol
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12
                Text { text: "Pairing code"; font.pixelSize: 16; font.bold: true; color: root.cText }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11; color: root.cMuted
                       text: "Hand this to one person, over Logos Chat or across a table. It is the shared secret: whoever holds it becomes this contact. Then rename the pending entry by pasting the same code under Add contact with their name." }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: codeEdit.implicitHeight + 16
                    radius: 6; color: "#10131A"; border.color: root.cBorder
                    TextEdit {
                        id: codeEdit
                        anchors.fill: parent; anchors.margins: 8
                        text: root.lastPairingCode
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        font.pixelSize: 12; font.family: "Menlo"
                        color: root.cAccent
                        selectionColor: root.cAccent
                        selectedTextColor: root.cBg
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    DarkButton { text: "Copy"; accent: true; onClicked: { codeEdit.selectAll(); codeEdit.copy(); codeEdit.deselect() } }
                    Item { Layout.fillWidth: true }
                    DarkButton { text: "Close"; onClicked: root.showPairing = false }
                }
            }
        }
    }

    // Add contact dialog
    Item {
        anchors.fill: parent
        visible: root.showAddContact
        z: 100
        Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.6; MouseArea { anchors.fill: parent; onClicked: root.showAddContact = false } }
        Rectangle {
            width: 440
            anchors.centerIn: parent
            height: addCol.implicitHeight + 40
            radius: 12
            color: root.cCard
            border.color: root.cBorder
            MouseArea { anchors.fill: parent }
            ColumnLayout {
                id: addCol
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12
                Text { text: "Add contact"; font.pixelSize: 16; font.bold: true; color: root.cText }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11; color: root.cMuted
                       text: "Paste the pairing code you were given and name the person. From the next epoch you recognise each other's tags." }
                DarkField { id: nameField; Layout.fillWidth: true; placeholderText: "Name" }
                DarkField { id: codeField; Layout.fillWidth: true; placeholderText: "pres1_..." }
                RowLayout {
                    Layout.fillWidth: true
                    DarkButton {
                        text: "Add"; accent: true
                        onClicked: {
                            if (root.backend) root.backend.addContact(nameField.text, codeField.text)
                            nameField.text = ""; codeField.text = ""
                            root.showAddContact = false
                        }
                    }
                    Item { Layout.fillWidth: true }
                    DarkButton { text: "Cancel"; onClicked: root.showAddContact = false }
                }
            }
        }
    }

    // New identity confirmation
    Item {
        anchors.fill: parent
        visible: root.showIdentityConfirm
        z: 100
        Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.6; MouseArea { anchors.fill: parent; onClicked: root.showIdentityConfirm = false } }
        Rectangle {
            width: 400
            anchors.centerIn: parent
            height: idCol.implicitHeight + 40
            radius: 12
            color: root.cCard
            border.color: root.cBorder
            MouseArea { anchors.fill: parent }
            ColumnLayout {
                id: idCol
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12
                Text { text: "New identity?"; font.pixelSize: 16; font.bold: true; color: root.cText }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11; color: root.cMuted
                       text: "A fresh seed, every pairing forgotten, the observer log cleared. Contacts will have to pair with you again. Nothing on the air will link the new identity to the old one." }
                RowLayout {
                    Layout.fillWidth: true
                    DarkButton { text: "Replace identity"; danger: true; onClicked: { if (root.backend) root.backend.newIdentity(); root.showIdentityConfirm = false } }
                    Item { Layout.fillWidth: true }
                    DarkButton { text: "Keep"; onClicked: root.showIdentityConfirm = false }
                }
            }
        }
    }
}
