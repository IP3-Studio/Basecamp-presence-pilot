#pragma once

#include "rep_presence_ui_source.h"
#include "logos_ui_plugin_context.h"
#include "presence_core.h"

#include <QElapsedTimer>
#include <QSet>
#include <QString>
#include <QTimer>

#include <deque>
#include <functional>
#include <vector>

// The hand-written UI backend (universal authoring model). The *Plugin and
// *Interface classes are generated around it; this class implements the .rep
// slots and feeds the .rep properties, which QtRO pushes to the QML replica.
//
// It owns one identity seed, the contact list, a simulated room (synthetic
// beacons plus the user's own), and the Logos delivery glue that carries the
// same beacon over the network. Every advertisement, simulated or received,
// lands in one observation log, which is what the observer view analyses.
class PresenceUiBackend : public PresenceUiSimpleSource,
                          public LogosUiPluginContext
{
public:
    PresenceUiBackend();
    ~PresenceUiBackend() override;

    // .rep SLOTs
    void applyRadioMode(QString mode) override;
    void applyRoomCode(QString code) override;
    void applyEpochSeconds(int seconds) override;
    void applySimSpeed(int factor) override;
    void applySimPeers(int count) override;
    void applyAddressPolicy(QString policy) override;
    void applyAnonymityLevel(QString level) override;
    void connectNetwork() override;
    void disconnectNetwork() override;
    void clearObservations() override;
    void createPairingCode() override;
    void addContact(QString name, QString code) override;
    void addSimulatedFriend() override;
    void removeContact(QString name) override;
    void newIdentity() override;

    void onContextReady() override;

private:
    struct StoredContact {
        presence::Contact c;
        bool sim = false;  // a synthetic peer in the simulated room carries the mirror key
    };

    // clock: simulated (accelerated) unless the network is up, then wall time
    bool realTime() const;
    qint64 nowMs() const;
    qint64 nowSeconds() const { return nowMs() / 1000; }
    bool simActive() const;
    bool networkWanted() const;

    void tick();
    void refreshUi();
    void rebuildRoom();
    void ingest(const std::vector<presence::Observation>& obs);
    void resetLog(const QString& why);

    // persistence
    QString dataDir() const;
    void loadIdentity();
    void saveSeed();
    void loadContacts();
    void saveContacts();
    void loadSettings();

    // network (delivery_module)
    QString topic() const;
    void ensureDelivery(std::function<void(bool ok, QString detail)> done);
    void subscribeRoom();
    void publishBeacon();
    void handleNetworkMessage(const QByteArray& payload);
    void setNet(const QString& state, const QString& info);
    void teardownNetwork();

    // derived state for the view
    void updateBeacon();
    void updateFriends();
    void updateHeadcount();
    void updateObservations();
    void updateLinkReport();
    void updateStatus();
    QString contactsToJson() const;
    bool contactExists(const QString& name) const;
    std::vector<presence::Contact> plainContacts() const;
    void say(const QString& text);

    presence::Key32 m_seed{};
    std::vector<StoredContact> m_contacts;
    presence::SimRoom m_room;
    std::deque<presence::Observation> m_obs;

    QTimer m_tick;
    QTimer m_refresh;
    QTimer m_heartbeat;
    QElapsedTimer m_elapsed;
    qint64 m_simClockMs = 0;
    qint64 m_lastTickMs = 0;
    qint64 m_lastEpochSeen = -1;
    int m_simFriendCounter = 0;

    // network
    bool m_eventsArmed = false;
    bool m_deliveryReady = false;
    bool m_subscribed = false;
    bool m_nodeOwnedElsewhere = false;
    int m_netGen = 0;
    qint64 m_lastPublishedEpoch = -1;
    QSet<QString> m_ownPayloadHashes;
};
