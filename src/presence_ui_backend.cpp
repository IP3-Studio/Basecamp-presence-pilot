#include "presence_ui_backend.h"

// Generated umbrella: LogosModules (behind modules()) from
// metadata.json#dependencies — typed wrappers + typed event accessors.
#include "logos_sdk.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <map>
#include <cstring>

using presence::Contact;
using presence::Key32;
using presence::Observation;
using presence::Slot16;

namespace {

const QString kSettingsOrg = QStringLiteral("Logos");
const QString kSettingsApp = QStringLiteral("presence_ui");
const int kObservationCap = 8000;
const int kObservationRows = 40;
const int kHeartbeatMs = 60000;  // one per minute: inside an RLN epoch quota on logos.test
const int kPeersMax = 12;

QString slotHex(const Slot16& s) { return QString::fromStdString(presence::hex(s.data(), s.size())); }
QString addrHex(const presence::Addr6& a)
{
    const std::string h = presence::hex(a.data(), a.size());
    QString out;
    for (size_t i = 0; i < h.size(); i += 2) {
        if (!out.isEmpty()) out += QLatin1Char(':');
        out += QString::fromStdString(h.substr(i, 2)).toUpper();
    }
    return out;
}

bool hexToSlot(const QString& hx, Slot16& out)
{
    presence::Bytes b;
    if (!presence::unhex(hx.toStdString(), b) || b.size() != out.size())
        return false;
    std::copy(b.begin(), b.end(), out.begin());
    return presence::hasMagic(out);
}

QString cleanName(const QString& name)
{
    static const QRegularExpression bad(QStringLiteral("[\\x00-\\x1f]"));
    return name.trimmed().remove(bad).left(32);
}

QString cleanRoom(const QString& code)
{
    static const QRegularExpression bad(QStringLiteral("[^a-z0-9-]"));
    const QString c = code.trimmed().toLower().remove(bad).left(24);
    return c.isEmpty() ? QStringLiteral("lobby") : c;
}

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle

PresenceUiBackend::PresenceUiBackend()
{
    m_elapsed.start();
    m_simClockMs = QDateTime::currentMSecsSinceEpoch();
    m_lastTickMs = m_elapsed.elapsed();

    m_tick.setInterval(250);
    QObject::connect(&m_tick, &QTimer::timeout, &m_tick, [this]() { tick(); });

    m_refresh.setInterval(1000);
    QObject::connect(&m_refresh, &QTimer::timeout, &m_refresh, [this]() { refreshUi(); });

    m_heartbeat.setInterval(kHeartbeatMs);
    QObject::connect(&m_heartbeat, &QTimer::timeout, &m_heartbeat, [this]() { publishBeacon(); });

    loadIdentity();
    loadContacts();
    loadSettings();
    rebuildRoom();
    updateBeacon();
    updateStatus();

    m_tick.start();
    m_refresh.start();
}

PresenceUiBackend::~PresenceUiBackend()
{
    m_tick.stop();
    m_refresh.stop();
    m_heartbeat.stop();
}

void PresenceUiBackend::onContextReady()
{
    // Nothing to start eagerly: the delivery node is created only when the
    // user switches the radio to the network, so a simulated session never
    // touches the testnet.
}

// ---------------------------------------------------------------------------
// Clock

bool PresenceUiBackend::realTime() const
{
    return networkState() != QLatin1String("offline");
}

qint64 PresenceUiBackend::nowMs() const
{
    return realTime() ? QDateTime::currentMSecsSinceEpoch() : m_simClockMs;
}

bool PresenceUiBackend::simActive() const
{
    return radioMode() == QLatin1String("sim") || radioMode() == QLatin1String("both");
}

bool PresenceUiBackend::networkWanted() const
{
    return radioMode() == QLatin1String("network") || radioMode() == QLatin1String("both");
}

void PresenceUiBackend::tick()
{
    const qint64 realNow = m_elapsed.elapsed();
    const qint64 dt = realNow - m_lastTickMs;
    m_lastTickMs = realNow;
    if (!realTime())
        m_simClockMs += dt * qMax(1, simSpeed());

    if (simActive())
        ingest(m_room.step(nowMs()));

    const qint64 e = presence::epochOf(nowSeconds(), epochSeconds());
    if (e != m_lastEpochSeen) {
        m_lastEpochSeen = e;
        updateBeacon();
        if (m_subscribed)
            publishBeacon();
    }
}

void PresenceUiBackend::refreshUi()
{
    const qint64 now = nowSeconds();
    const qint64 e = presence::epochOf(now, epochSeconds());
    setEpoch(e);
    setEpochSecondsLeft(int((e + 1) * epochSeconds() - now));
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(now);
    setClockText(realTime()
                 ? dt.toString(QStringLiteral("HH:mm:ss")) + QStringLiteral(" real time")
                 : dt.toString(QStringLiteral("HH:mm:ss")) + QStringLiteral(" simulated, %1x").arg(qMax(1, simSpeed())));
    updateHeadcount();
    updateFriends();
    updateObservations();
    updateLinkReport();
}

// ---------------------------------------------------------------------------
// Simulated room

void PresenceUiBackend::rebuildRoom()
{
    // Devices persist across rebuilds. A rebuild only adds, removes or
    // re-parameterises; a stranger keeps its seed, address and cadence, so the
    // observer log stays consistent with the room (a fresh seed mid-epoch would
    // leave orphaned trails behind and inflate the headcount).
    std::map<std::string, presence::SimDevice> keep;
    for (const presence::SimDevice& d : m_room.devices)
        keep.emplace(d.name, d);
    auto take = [&](const std::string& name) {
        auto it = keep.find(name);
        if (it != keep.end())
            return it->second;
        return presence::makeSimDevice(name);
    };

    m_room.devices.clear();
    m_room.epochSeconds = epochSeconds();
    presence::AddressPolicy p = presence::AddressPolicy::Aligned;
    presence::addressPolicyFromName(addressPolicy().toStdString(), p);
    m_room.policy = p;
    const int speed = qMax(1, simSpeed());

    // The user's own beacon is part of the room: a sniffer logs it too.
    presence::SimDevice me = take("You");
    me.seed = m_seed;
    me.contacts = plainContacts();
    me.slotsEpoch = -1;  // contacts changed: recompute slots
    me.advIntervalMs = me.advBaseMs * speed;
    m_room.devices.push_back(me);

    // Simulated friends carry the mirror of the pairing (the other role).
    for (const StoredContact& sc : m_contacts) {
        if (!sc.sim)
            continue;
        presence::SimDevice d = take(sc.c.name);
        Contact mirror = sc.c;
        mirror.myRole = 1 - sc.c.myRole;
        d.contacts = {mirror};
        d.slotsEpoch = -1;
        d.advIntervalMs = d.advBaseMs * speed;
        m_room.devices.push_back(d);
    }

    for (int i = 0; i < simPeers(); ++i) {
        presence::SimDevice d = take("Stranger " + std::to_string(i + 1));
        d.contacts.clear();
        d.advIntervalMs = d.advBaseMs * speed;
        m_room.devices.push_back(d);
    }
}

void PresenceUiBackend::ingest(const std::vector<Observation>& obs)
{
    for (const Observation& o : obs)
        m_obs.push_back(o);
    while (int(m_obs.size()) > kObservationCap)
        m_obs.pop_front();
}

void PresenceUiBackend::resetLog(const QString& why)
{
    m_obs.clear();
    m_lastEpochSeen = -1;
    if (!why.isEmpty())
        say(why);
    refreshUi();
}

// ---------------------------------------------------------------------------
// .rep slots: radio and room

void PresenceUiBackend::applyRadioMode(QString mode)
{
    const QString m = mode.trimmed().toLower();
    if (m != QLatin1String("sim") && m != QLatin1String("network") && m != QLatin1String("both"))
        return;
    if (m == radioMode())
        return;
    setRadioMode(m);
    QSettings(kSettingsOrg, kSettingsApp).setValue(QStringLiteral("radioMode"), m);
    if (!networkWanted() && networkState() != QLatin1String("offline"))
        teardownNetwork();
    if (networkWanted() && networkState() == QLatin1String("offline"))
        connectNetwork();
    rebuildRoom();
    resetLog(QString());
    updateStatus();
}

void PresenceUiBackend::applyRoomCode(QString code)
{
    const QString c = cleanRoom(code);
    if (c == roomCode())
        return;
    const bool wasUp = m_subscribed;
    if (wasUp)
        teardownNetwork();
    setRoomCode(c);
    QSettings(kSettingsOrg, kSettingsApp).setValue(QStringLiteral("roomCode"), c);
    if (wasUp)
        connectNetwork();
    updateStatus();
}

void PresenceUiBackend::applyEpochSeconds(int seconds)
{
    const int s = qBound(10, seconds, 3600);
    if (s == epochSeconds())
        return;
    setEpochSeconds(s);
    QSettings(kSettingsOrg, kSettingsApp).setValue(QStringLiteral("epochSeconds"), s);
    rebuildRoom();
    updateBeacon();
    resetLog(QStringLiteral("Epoch length changed; the observer log starts again."));
    updateStatus();
}

void PresenceUiBackend::applySimSpeed(int factor)
{
    const int f = qBound(1, factor, 600);
    if (f == simSpeed())
        return;
    setSimSpeed(f);
    QSettings(kSettingsOrg, kSettingsApp).setValue(QStringLiteral("simSpeed"), f);
    rebuildRoom();
    resetLog(QString());
    updateStatus();
}

void PresenceUiBackend::applySimPeers(int count)
{
    const int c = qBound(0, count, kPeersMax);
    if (c == simPeers())
        return;
    setSimPeers(c);
    QSettings(kSettingsOrg, kSettingsApp).setValue(QStringLiteral("simPeers"), c);
    rebuildRoom();
    resetLog(QString());
    updateStatus();
}

void PresenceUiBackend::applyAddressPolicy(QString policy)
{
    presence::AddressPolicy p;
    const std::string s = policy.trimmed().toLower().toStdString();
    if (!presence::addressPolicyFromName(s, p) || QString::fromStdString(s) == addressPolicy())
        return;
    setAddressPolicy(QString::fromStdString(s));
    QSettings(kSettingsOrg, kSettingsApp).setValue(QStringLiteral("addressPolicy"), addressPolicy());
    rebuildRoom();
    resetLog(QStringLiteral("Address policy changed; the observer log starts again."));
    updateStatus();
}

void PresenceUiBackend::applyAnonymityLevel(QString level)
{
    const QString l = level.trimmed();
    if (l != QLatin1String("None") && l != QLatin1String("Preferred") && l != QLatin1String("Required"))
        return;
    if (l == anonymityLevel())
        return;
    setAnonymityLevel(l);
    QSettings(kSettingsOrg, kSettingsApp).setValue(QStringLiteral("anonymityLevel"), l);
    if (m_deliveryReady)
        say(QStringLiteral("The anonymity level is fixed when the node is created. Restart Basecamp for it to apply."));
    updateStatus();
}

void PresenceUiBackend::applyPreset(QString preset)
{
    const QString p = preset.trimmed();
    if (p != QLatin1String("logos.test") && p != QLatin1String("logos.dev"))
        return;
    if (p == this->preset())
        return;
    setPreset(p);
    QSettings(kSettingsOrg, kSettingsApp).setValue(QStringLiteral("preset"), p);
    if (m_deliveryReady)
        say(QStringLiteral("The network preset is fixed when the node is created. Restart Basecamp for it to apply."));
    updateStatus();
}

void PresenceUiBackend::clearObservations()
{
    resetLog(QString());
}

// ---------------------------------------------------------------------------
// .rep slots: identity and contacts

void PresenceUiBackend::createPairingCode()
{
    Key32 k{};
    presence::randomBytes(k.data(), k.size());
    const QString code = QString::fromStdString(presence::encodePairingCode(k));
    // The creator keeps the key under role 0 with a placeholder name until the
    // other side confirms; the user names the contact when they add the code.
    StoredContact sc;
    sc.c.key = k;
    sc.c.myRole = 0;
    int n = 1;
    while (contactExists(QStringLiteral("Pending %1").arg(n)))
        ++n;
    sc.c.name = QStringLiteral("Pending %1").arg(n).toStdString();
    m_contacts.push_back(sc);
    saveContacts();
    setContactsJson(contactsToJson());
    setLastPairingCode(code);
    emit pairingCodeReady(code);
    rebuildRoom();
    updateBeacon();
    say(QStringLiteral("Pairing code created. Hand it to one person; rename the contact once they have added it."));
}

void PresenceUiBackend::addContact(QString name, QString code)
{
    const QString n = cleanName(name);
    if (n.isEmpty()) {
        say(QStringLiteral("Give the contact a name first."));
        return;
    }
    Key32 k{};
    if (!presence::decodePairingCode(code.toStdString(), k)) {
        say(QStringLiteral("That is not a valid pairing code."));
        return;
    }
    // Pasting a code you created yourself just renames the pending entry.
    for (StoredContact& sc : m_contacts) {
        if (sc.c.key == k) {
            sc.c.name = n.toStdString();
            saveContacts();
            setContactsJson(contactsToJson());
            rebuildRoom();
            say(QStringLiteral("Contact renamed to %1.").arg(n));
            return;
        }
    }
    if (contactExists(n)) {
        say(QStringLiteral("You already have a contact called %1.").arg(n));
        return;
    }
    StoredContact sc;
    sc.c.name = n.toStdString();
    sc.c.key = k;
    sc.c.myRole = 1;
    m_contacts.push_back(sc);
    saveContacts();
    setContactsJson(contactsToJson());
    rebuildRoom();
    updateBeacon();
    say(QStringLiteral("Contact %1 added. You will recognise each other from the next epoch.").arg(n));
}

void PresenceUiBackend::addSimulatedFriend()
{
    static const char* names[] = {"Ada", "Bram", "Chen", "Dara", "Eli", "Femi", "Gwen", "Hana"};
    QString n;
    do {
        n = QStringLiteral("%1 (simulated)").arg(QLatin1String(names[m_simFriendCounter % 8]));
        if (m_simFriendCounter >= 8)
            n = QStringLiteral("%1 %2 (simulated)").arg(QLatin1String(names[m_simFriendCounter % 8])).arg(m_simFriendCounter / 8 + 1);
        ++m_simFriendCounter;
    } while (contactExists(n));
    StoredContact sc;
    sc.c.name = n.toStdString();
    presence::randomBytes(sc.c.key.data(), sc.c.key.size());
    sc.c.myRole = 0;
    sc.sim = true;
    m_contacts.push_back(sc);
    saveContacts();
    setContactsJson(contactsToJson());
    rebuildRoom();
    updateBeacon();
    say(QStringLiteral("%1 joined the simulated room with the mirror of your pairing.").arg(n));
}

void PresenceUiBackend::removeContact(QString name)
{
    const QString n = cleanName(name);
    const auto it = std::remove_if(m_contacts.begin(), m_contacts.end(),
                                   [&](const StoredContact& sc) { return QString::fromStdString(sc.c.name) == n; });
    if (it == m_contacts.end())
        return;
    m_contacts.erase(it, m_contacts.end());
    saveContacts();
    setContactsJson(contactsToJson());
    rebuildRoom();
    updateBeacon();
    say(QStringLiteral("Removed %1.").arg(n));
}

void PresenceUiBackend::newIdentity()
{
    presence::randomBytes(m_seed.data(), m_seed.size());
    saveSeed();
    m_contacts.clear();
    saveContacts();
    setContactsJson(contactsToJson());
    setLastPairingCode(QString());
    rebuildRoom();
    updateBeacon();
    const presence::Bytes h = presence::sha256(m_seed.data(), m_seed.size());
    setIdentityLabel(QString::fromStdString(presence::hex(h.data(), 4)));
    resetLog(QStringLiteral("New identity: a fresh seed, no contacts, and nothing on the air links to the old one."));
    updateStatus();
}

// ---------------------------------------------------------------------------
// Persistence

QString PresenceUiBackend::dataDir() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.logos-presence");
    const QString dir = base + QStringLiteral("/presence_ui");
    QDir().mkpath(dir);
    return dir;
}

void PresenceUiBackend::loadIdentity()
{
    QFile f(dataDir() + QStringLiteral("/seed.bin"));
    if (f.open(QIODevice::ReadOnly)) {
        const QByteArray b = f.readAll();
        if (b.size() == int(m_seed.size())) {
            std::memcpy(m_seed.data(), b.constData(), m_seed.size());
            const presence::Bytes h = presence::sha256(m_seed.data(), m_seed.size());
            setIdentityLabel(QString::fromStdString(presence::hex(h.data(), 4)));
            return;
        }
    }
    presence::randomBytes(m_seed.data(), m_seed.size());
    saveSeed();
    const presence::Bytes h = presence::sha256(m_seed.data(), m_seed.size());
    setIdentityLabel(QString::fromStdString(presence::hex(h.data(), 4)));
}

void PresenceUiBackend::saveSeed()
{
    QFile f(dataDir() + QStringLiteral("/seed.bin"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(reinterpret_cast<const char*>(m_seed.data()), int(m_seed.size()));
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
}

void PresenceUiBackend::loadContacts()
{
    m_contacts.clear();
    QFile f(dataDir() + QStringLiteral("/contacts.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    for (const QJsonValue& v : doc.array()) {
        const QJsonObject o = v.toObject();
        presence::Bytes kb;
        if (!presence::unhex(o.value(QStringLiteral("key")).toString().toStdString(), kb) || kb.size() != 32)
            continue;
        StoredContact sc;
        sc.c.name = cleanName(o.value(QStringLiteral("name")).toString()).toStdString();
        std::copy(kb.begin(), kb.end(), sc.c.key.begin());
        sc.c.myRole = o.value(QStringLiteral("role")).toInt(0) & 1;
        sc.sim = o.value(QStringLiteral("sim")).toBool(false);
        if (!sc.c.name.empty())
            m_contacts.push_back(sc);
    }
    setContactsJson(contactsToJson());
}

void PresenceUiBackend::saveContacts()
{
    QJsonArray arr;
    for (const StoredContact& sc : m_contacts) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), QString::fromStdString(sc.c.name));
        o.insert(QStringLiteral("key"), QString::fromStdString(presence::hex(sc.c.key.data(), sc.c.key.size())));
        o.insert(QStringLiteral("role"), sc.c.myRole);
        o.insert(QStringLiteral("sim"), sc.sim);
        arr.append(o);
    }
    QFile f(dataDir() + QStringLiteral("/contacts.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
}

void PresenceUiBackend::loadSettings()
{
    QSettings s(kSettingsOrg, kSettingsApp);
    setEpochSeconds(qBound(10, s.value(QStringLiteral("epochSeconds"), 900).toInt(), 3600));
    setSimSpeed(qBound(1, s.value(QStringLiteral("simSpeed"), 60).toInt(), 600));
    setSimPeers(qBound(0, s.value(QStringLiteral("simPeers"), 3).toInt(), kPeersMax));
    const QString pol = s.value(QStringLiteral("addressPolicy"), QStringLiteral("aligned")).toString();
    presence::AddressPolicy p;
    setAddressPolicy(presence::addressPolicyFromName(pol.toStdString(), p) ? pol : QStringLiteral("aligned"));
    setRoomCode(cleanRoom(s.value(QStringLiteral("roomCode"), QStringLiteral("lobby")).toString()));
    const QString lvl = s.value(QStringLiteral("anonymityLevel"), QStringLiteral("Preferred")).toString();
    setAnonymityLevel(lvl == QLatin1String("None") || lvl == QLatin1String("Required") ? lvl : QStringLiteral("Preferred"));
    const QString pre = s.value(QStringLiteral("preset"), QStringLiteral("logos.test")).toString();
    setPreset(pre == QLatin1String("logos.dev") ? pre : QStringLiteral("logos.test"));
    // The radio always starts simulated: joining the testnet is a click, not a default.
    setRadioMode(QStringLiteral("sim"));
}

// ---------------------------------------------------------------------------
// Network: the same beacon, carried over Logos Messaging
//
// One message per heartbeat and per epoch change on the room topic, holding
// the full slot set for the current epoch. Nothing in it identifies the
// sender; the mixnet (anonymityLevel) hides which node published it.

QString PresenceUiBackend::topic() const
{
    return QStringLiteral("/logos-presence/1/room-%1/json").arg(roomCode());
}

void PresenceUiBackend::setNet(const QString& state, const QString& info)
{
    const bool wasReal = realTime();
    setNetworkState(state);
    setNetworkInfo(info);
    if (wasReal != realTime()) {
        // The clock switches between simulated and wall time: the two logs do
        // not mix, so start again.
        m_simClockMs = QDateTime::currentMSecsSinceEpoch();
        rebuildRoom();
        resetLog(QString());
    }
    updateStatus();
}

void PresenceUiBackend::ensureDelivery(std::function<void(bool, QString)> done)
{
    if (!m_eventsArmed) {
        m_eventsArmed = true;
        modules().delivery_module.onMessageReceived(
            [this](const QString&, const QString& contentTopic, QByteArray payload,
                   const QString&, qlonglong) {
                if (m_subscribed && contentTopic == topic())
                    handleNetworkMessage(payload);
            });
        modules().delivery_module.onConnectionStateChanged(
            [this](const QString& status, qlonglong) {
                if (networkState() != QLatin1String("offline"))
                    setNetworkInfo(QStringLiteral("Node: %1").arg(status));
            });
        modules().delivery_module.onMessageError(
            [this](const QString&, const QString&, const QString& error, qlonglong) {
                if (networkState() != QLatin1String("offline"))
                    setNetworkInfo(QStringLiteral("Send failed: %1").arg(error.left(120)));
            });
        modules().delivery_module.onMessageSent(
            [this](const QString&, const QString&, qlonglong) {
                if (networkState() == QLatin1String("connected"))
                    setNetworkInfo(QStringLiteral("Beacon published on %1 (anonymity: %2)")
                                       .arg(roomCode(), m_nodeOwnedElsewhere ? QStringLiteral("set by another module") : anonymityLevel()));
            });
    }
    if (m_deliveryReady) {
        done(true, QString());
        return;
    }
    // createNode rejects duplicates and start is not idempotent: another
    // consumer (the Chat app) may already own the node, with its own anonymity
    // level. Both results are tolerated; subscribe is the call that must work.
    // No logLevel key: delivery 0.3.x rejects unknown options outright
    // ("Unrecognized configuration option(s) found: logLevel").
    const QString cfg = QStringLiteral(
        "{\"mode\":\"Core\",\"preset\":\"%1\","
        "\"messagingOverrides\":{\"anonymityLevel\":\"%2\"}}").arg(preset(), anonymityLevel());
    modules().delivery_module.createNodeAsync(cfg, [this, done](LogosResult created) {
        m_nodeOwnedElsewhere = !created.success;
        modules().delivery_module.startAsync([this, done](LogosResult) {
            m_deliveryReady = true;
            done(true, QString());
        });
    });
}

void PresenceUiBackend::connectNetwork()
{
    if (networkState() != QLatin1String("offline"))
        return;
    if (!networkWanted()) {
        setRadioMode(radioMode() == QLatin1String("sim") ? QStringLiteral("both") : QStringLiteral("network"));
        QSettings(kSettingsOrg, kSettingsApp).setValue(QStringLiteral("radioMode"), radioMode());
    }
    setNet(QStringLiteral("starting"), QStringLiteral("Starting the delivery node on %1...").arg(preset()));
    const int gen = ++m_netGen;
    ensureDelivery([this, gen](bool ok, QString detail) {
        if (gen != m_netGen)
            return;
        if (!ok) {
            setNet(QStringLiteral("error"), detail);
            return;
        }
        subscribeRoom();
    });
    QTimer::singleShot(10000, &m_tick, [this, gen]() {
        if (gen == m_netGen && networkState() == QLatin1String("starting"))
            setNet(QStringLiteral("error"),
                   QStringLiteral("The delivery module is not responding. Is delivery_module installed and loaded?"));
    });
}

void PresenceUiBackend::subscribeRoom()
{
    const int gen = m_netGen;
    modules().delivery_module.subscribeAsync(topic(), [this, gen](LogosResult r) {
        if (gen != m_netGen)
            return;
        if (!r.success) {
            setNet(QStringLiteral("error"), QStringLiteral("Could not subscribe to the room: %1").arg(r.getError()));
            return;
        }
        m_subscribed = true;
        setNet(QStringLiteral("connected"),
               QStringLiteral("Subscribed to room \"%1\". Anonymity level: %2.")
                   .arg(roomCode(), m_nodeOwnedElsewhere ? QStringLiteral("set by the module that created the node") : anonymityLevel()));
        m_lastPublishedEpoch = -1;
        publishBeacon();
        m_heartbeat.start();
    });
}

void PresenceUiBackend::teardownNetwork()
{
    ++m_netGen;
    m_heartbeat.stop();
    if (m_subscribed && m_deliveryReady)
        modules().delivery_module.unsubscribeAsync(topic(), [](LogosResult) {});
    m_subscribed = false;
    m_ownPayloadHashes.clear();
    setNet(QStringLiteral("offline"), QString());
}

void PresenceUiBackend::disconnectNetwork()
{
    if (networkState() == QLatin1String("offline"))
        return;
    teardownNetwork();
    if (radioMode() == QLatin1String("network"))
        setRadioMode(QStringLiteral("sim"));
    else if (radioMode() == QLatin1String("both"))
        setRadioMode(QStringLiteral("sim"));
    QSettings(kSettingsOrg, kSettingsApp).setValue(QStringLiteral("radioMode"), radioMode());
    rebuildRoom();
    updateStatus();
}

void PresenceUiBackend::publishBeacon()
{
    if (!m_subscribed)
        return;
    const qint64 e = presence::epochOf(nowSeconds(), epochSeconds());
    const std::vector<Slot16> slotSet = presence::beaconSlots(m_seed, plainContacts(), e);
    QJsonArray s;
    for (const Slot16& sl : slotSet)
        s.append(slotHex(sl));
    QJsonObject o;
    o.insert(QStringLiteral("v"), 1);
    o.insert(QStringLiteral("e"), double(e));
    o.insert(QStringLiteral("s"), s);
    const QByteArray payload = QJsonDocument(o).toJson(QJsonDocument::Compact);
    const presence::Bytes h = presence::sha256(reinterpret_cast<const uint8_t*>(payload.constData()), size_t(payload.size()));
    const QString hh = QString::fromStdString(presence::hex(h.data(), 16));
    if (m_ownPayloadHashes.size() > 64)
        m_ownPayloadHashes.clear();
    m_ownPayloadHashes.insert(hh);
    m_lastPublishedEpoch = e;
    // Log our own publication as the relay would see it, once; the echo is dropped.
    std::vector<Observation> own;
    for (const Slot16& sl : slotSet) {
        Observation ob;
        ob.t = nowSeconds();
        std::memcpy(ob.addr.data(), h.data(), ob.addr.size());
        ob.slot = sl;
        ob.viaNetwork = true;
        own.push_back(ob);
    }
    ingest(own);
    modules().delivery_module.sendAsync(topic(), payload, [](LogosResult) {});
}

void PresenceUiBackend::handleNetworkMessage(const QByteArray& payload)
{
    const presence::Bytes h = presence::sha256(reinterpret_cast<const uint8_t*>(payload.constData()), size_t(payload.size()));
    const QString hh = QString::fromStdString(presence::hex(h.data(), 16));
    if (m_ownPayloadHashes.contains(hh))
        return;
    const QJsonObject o = QJsonDocument::fromJson(payload).object();
    if (o.value(QStringLiteral("v")).toInt() != 1)
        return;
    const qint64 e = qint64(o.value(QStringLiteral("e")).toDouble(-1));
    const qint64 mine = presence::epochOf(nowSeconds(), epochSeconds());
    if (e < mine - 1 || e > mine + 1)
        return;
    const QJsonArray s = o.value(QStringLiteral("s")).toArray();
    if (s.size() != presence::kSlotsPerBeacon)
        return;
    std::vector<Observation> obs;
    for (const QJsonValue& v : s) {
        Observation ob;
        if (!hexToSlot(v.toString(), ob.slot))
            return;
        ob.t = nowSeconds();
        std::memcpy(ob.addr.data(), h.data(), ob.addr.size());
        ob.viaNetwork = true;
        obs.push_back(ob);
    }
    ingest(obs);
}

// ---------------------------------------------------------------------------
// Derived state

std::vector<Contact> PresenceUiBackend::plainContacts() const
{
    std::vector<Contact> out;
    out.reserve(m_contacts.size());
    for (const StoredContact& sc : m_contacts)
        out.push_back(sc.c);
    return out;
}

bool PresenceUiBackend::contactExists(const QString& name) const
{
    for (const StoredContact& sc : m_contacts)
        if (QString::fromStdString(sc.c.name) == name)
            return true;
    return false;
}

QString PresenceUiBackend::contactsToJson() const
{
    QJsonArray arr;
    for (const StoredContact& sc : m_contacts) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), QString::fromStdString(sc.c.name));
        o.insert(QStringLiteral("role"), sc.c.myRole == 0 ? QStringLiteral("code creator") : QStringLiteral("code joiner"));
        o.insert(QStringLiteral("sim"), sc.sim);
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void PresenceUiBackend::updateBeacon()
{
    const qint64 e = presence::epochOf(nowSeconds(), epochSeconds());
    const std::vector<Contact> cs = plainContacts();
    const std::vector<Slot16> slotSet = presence::beaconSlots(m_seed, cs, e);
    QJsonArray arr;
    for (size_t i = 0; i < slotSet.size(); ++i) {
        QJsonObject o;
        o.insert(QStringLiteral("hex"), slotHex(slotSet[i]));
        QString label;
        if (i == 0)
            label = QStringLiteral("anonymous token");
        else {
            // Which contact owns this slot: same rotation rule as beaconSlots.
            const int tagSlots = presence::kSlotsPerBeacon - 1;
            size_t start = 0;
            if (cs.size() > size_t(tagSlots))
                start = size_t(((e % qint64(cs.size())) + qint64(cs.size())) % qint64(cs.size()));
            const size_t idx = i - 1;
            if (idx < cs.size())
                label = QStringLiteral("tag for %1").arg(QString::fromStdString(cs[(start + idx) % cs.size()].name));
            else
                label = QStringLiteral("padding");
        }
        o.insert(QStringLiteral("label"), label);
        arr.append(o);
    }
    setBeaconJson(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void PresenceUiBackend::updateHeadcount()
{
    const qint64 e = presence::epochOf(nowSeconds(), epochSeconds());
    std::vector<Observation> v(m_obs.begin(), m_obs.end());
    setHeadcount(presence::devicesSince(v, e * epochSeconds()));
}

void PresenceUiBackend::updateFriends()
{
    const qint64 now = nowSeconds();
    const qint64 e = presence::epochOf(now, epochSeconds());
    const qint64 horizon = now - 2 * epochSeconds();
    QJsonArray arr;
    for (const StoredContact& sc : m_contacts) {
        Slot16 expected[3];
        for (int k = 0; k < 3; ++k)
            expected[k] = presence::pairSlot(sc.c.key, e - 1 + k, 1 - sc.c.myRole);
        qint64 lastSeen = -1;
        bool viaNet = false;
        for (auto it = m_obs.rbegin(); it != m_obs.rend(); ++it) {
            if (it->t < horizon)
                break;
            if (it->slot == expected[0] || it->slot == expected[1] || it->slot == expected[2]) {
                lastSeen = it->t;
                viaNet = it->viaNetwork;
                break;
            }
        }
        QJsonObject o;
        o.insert(QStringLiteral("name"), QString::fromStdString(sc.c.name));
        o.insert(QStringLiteral("sim"), sc.sim);
        o.insert(QStringLiteral("here"), lastSeen >= 0 && now - lastSeen <= epochSeconds());
        o.insert(QStringLiteral("agoSeconds"), lastSeen >= 0 ? double(now - lastSeen) : -1.0);
        o.insert(QStringLiteral("source"), lastSeen < 0 ? QString() : viaNet ? QStringLiteral("network") : QStringLiteral("radio"));
        arr.append(o);
    }
    setFriendsJson(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void PresenceUiBackend::updateObservations()
{
    QJsonArray arr;
    int n = 0;
    for (auto it = m_obs.rbegin(); it != m_obs.rend() && n < kObservationRows; ++it, ++n) {
        QJsonObject o;
        o.insert(QStringLiteral("time"), QDateTime::fromSecsSinceEpoch(it->t).toString(QStringLiteral("HH:mm:ss")));
        o.insert(QStringLiteral("epoch"), double(presence::epochOf(it->t, epochSeconds())));
        o.insert(QStringLiteral("addr"), it->viaNetwork ? QStringLiteral("msg ") + addrHex(it->addr).left(8) : addrHex(it->addr));
        o.insert(QStringLiteral("slot"), slotHex(it->slot));
        o.insert(QStringLiteral("src"), it->viaNetwork ? QStringLiteral("network") : QStringLiteral("radio"));
        arr.append(o);
    }
    setObservationsJson(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void PresenceUiBackend::updateLinkReport()
{
    std::vector<Observation> v(m_obs.begin(), m_obs.end());
    const presence::LinkReport r = presence::analyse(v, epochSeconds());
    QJsonObject o;
    o.insert(QStringLiteral("observations"), r.observations);
    o.insert(QStringLiteral("addresses"), r.addresses);
    o.insert(QStringLiteral("slots"), r.distinctSlots);
    o.insert(QStringLiteral("epochs"), r.epochsCovered);
    o.insert(QStringLiteral("slotsBridgingAddresses"), r.slotsBridgingAddresses);
    o.insert(QStringLiteral("addressesBridgingEpochs"), r.addressesBridgingEpochs);
    o.insert(QStringLiteral("trails"), r.trails);
    o.insert(QStringLiteral("longestTrailEpochs"), r.longestTrailEpochs);
    o.insert(QStringLiteral("longestTrailSeconds"), double(r.longestTrailSeconds));
    const bool unlinkable = r.epochsCovered >= 2 && r.slotsBridgingAddresses == 0
        && r.addressesBridgingEpochs == 0 && r.longestTrailEpochs <= 1;
    QString verdict;
    if (r.observations == 0)
        verdict = QStringLiteral("Nothing observed yet.");
    else if (r.epochsCovered < 2)
        verdict = QStringLiteral("One epoch so far. Linkability is only defined across epochs; wait for the next one.");
    else if (unlinkable)
        verdict = QStringLiteral("Unlinkable across epochs: over %1 epochs no address and no slot bridged a rotation, and no trail is longer than one epoch.")
                      .arg(r.epochsCovered);
    else
        verdict = QStringLiteral("Linkable: the observer can follow one device for %1 epochs (%2). %3 slot(s) stayed constant across an address change and %4 address(es) stayed constant across an epoch change.")
                      .arg(r.longestTrailEpochs)
                      .arg(r.longestTrailSeconds >= 3600 ? QStringLiteral("%1 h %2 min").arg(r.longestTrailSeconds / 3600).arg((r.longestTrailSeconds % 3600) / 60)
                                                         : QStringLiteral("%1 min %2 s").arg(r.longestTrailSeconds / 60).arg(r.longestTrailSeconds % 60))
                      .arg(r.slotsBridgingAddresses)
                      .arg(r.addressesBridgingEpochs);
    o.insert(QStringLiteral("unlinkable"), unlinkable);
    o.insert(QStringLiteral("verdict"), verdict);
    setLinkReportJson(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

void PresenceUiBackend::updateStatus()
{
    QStringList parts;
    if (simActive())
        parts << QStringLiteral("Simulated room: %1 stranger(s), %2 simulated friend(s), address policy %3.")
                     .arg(simPeers())
                     .arg(int(std::count_if(m_contacts.begin(), m_contacts.end(), [](const StoredContact& s) { return s.sim; })))
                     .arg(addressPolicy());
    if (networkWanted())
        parts << QStringLiteral("Network room \"%1\" on %2, anonymity %3: %4.")
                     .arg(roomCode(), preset(), anonymityLevel(), networkState());
    setStatus(parts.join(QLatin1Char(' ')));
}

void PresenceUiBackend::say(const QString& text)
{
    emit notice(text);
}
