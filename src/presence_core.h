#pragma once

// Qt-free core of the presence pilot: hashing, the key schedule, the on-air
// slot format, a passive-observer model with the DP-3T style linkability
// check, and a room simulator. Nothing here touches a radio or the network;
// the backend feeds it observations from whichever "radio" is active.
//
// Design (see unlinkable-presence-pilot.md):
//   epoch E        = floor(unix / T)
//   anon token     = HMAC(seed_install, "presence/v1/anon" || E)[0:12]
//   pair tag       = HMAC(K_AB, "presence/v1/tag" || E || role)[0:12]
//   on-air slot    = magic[4] || payload[12]   (16 bytes, one per advert)
// Every beacon emits exactly kSlotsPerBeacon slots per epoch: the anon token,
// one tag per paired contact, random padding to the fixed count. A fixed count
// keeps the number of contacts off the air.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace presence {

using Bytes = std::vector<uint8_t>;
using Key32 = std::array<uint8_t, 32>;
using Slot16 = std::array<uint8_t, 16>;
using Addr6 = std::array<uint8_t, 6>;

constexpr int kSlotsPerBeacon = 4;
constexpr int kPayloadBytes = 12;
constexpr uint8_t kMagic[4] = {'L', 'P', '0', '1'};

// ---- primitives ----------------------------------------------------------
Bytes sha256(const uint8_t* data, size_t len);
Bytes hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t len);
void randomBytes(uint8_t* out, size_t len);
std::string hex(const uint8_t* d, size_t n);
bool unhex(const std::string& s, Bytes& out);
std::string base64url(const uint8_t* d, size_t n);
bool base64urlDecode(const std::string& s, Bytes& out);

// ---- key schedule --------------------------------------------------------
int64_t epochOf(int64_t unixSeconds, int64_t epochSeconds);
Slot16 anonSlot(const Key32& seed, int64_t epoch);
// role: 0 = the side that created the pairing code, 1 = the side that pasted it.
Slot16 pairSlot(const Key32& pairKey, int64_t epoch, int role);
// Padding is derived, not random: random padding would never repeat within an
// epoch and so would betray which slots are real.
Slot16 paddingSlot(const Key32& seed, int64_t epoch, int index);
bool hasMagic(const Slot16& s);

struct Contact {
    std::string name;
    Key32 key{};
    int myRole = 0;  // 0 creator, 1 joiner; the contact emits with the other role
};

// The slots one device puts on the air for a given epoch (fixed count).
std::vector<Slot16> beaconSlots(const Key32& seed, const std::vector<Contact>& contacts, int64_t epoch);

// Pairing codes: "pres1_" + base64url(0x01 || key32).
std::string encodePairingCode(const Key32& key);
bool decodePairingCode(const std::string& code, Key32& key);

// ---- observer model ------------------------------------------------------
struct Observation {
    int64_t t = 0;       // unix seconds (sim or wall clock)
    Addr6 addr{};        // controller address as seen on the air, or a per-message id on the network
    Slot16 slot{};
    bool viaNetwork = false;
};

struct LinkReport {
    int observations = 0;
    int addresses = 0;
    int distinctSlots = 0;
    int epochsCovered = 0;
    int slotsBridgingAddresses = 0;   // a payload seen under two addresses (address rotated, payload did not)
    int addressesBridgingEpochs = 0;  // an address seen across two epochs (payload rotated, address did not)
    int trails = 0;                   // connected components of the (address, slot) graph
    int longestTrailEpochs = 0;       // the longest span an observer can follow one device
    int64_t longestTrailSeconds = 0;
};

LinkReport analyse(const std::vector<Observation>& obs, int64_t epochSeconds);

// How many devices a passive observer can count in the window [fromT, +inf):
// observations are linked through shared addresses and shared slots (the
// intra-epoch linkability the definition accepts), and each connected group
// is one device. Exact under every address policy within one epoch.
int devicesSince(const std::vector<Observation>& obs, int64_t fromT);

// ---- simulator -----------------------------------------------------------
enum class AddressPolicy { Aligned, Drifting, Fixed };
const char* addressPolicyName(AddressPolicy p);
bool addressPolicyFromName(const std::string& s, AddressPolicy& out);

struct SimDevice {
    std::string name;
    Key32 seed{};
    std::vector<Contact> contacts;  // pairings with the local user, if any
    int64_t addrPeriod = 900;       // Drifting: seconds between address changes
    int64_t addrPhase = 0;          // Drifting: offset of the rotation clock
    int64_t skewSeconds = 0;        // clock skew relative to the room
    int64_t advIntervalMs = 1000;   // one slot every interval
    // runtime
    Addr6 addr{};
    int64_t addrKey = -1;           // identity of the current address interval
    int64_t nextAdvMs = 0;
    int slotIndex = 0;
};

struct SimRoom {
    std::vector<SimDevice> devices;
    int64_t epochSeconds = 900;
    AddressPolicy policy = AddressPolicy::Aligned;

    // Advance the room to nowMs and return every advertisement emitted since
    // the previous call, as a passive receiver would log them.
    std::vector<Observation> step(int64_t nowMs);
};

SimDevice makeSimDevice(const std::string& name);

}  // namespace presence
