// Native unit test for the Qt-free core. Build and run without Nix:
//   c++ -std=c++17 -Wall -Wextra -I src -o /tmp/core_test tests/core_test.cpp src/presence_core.cpp && /tmp/core_test
#include "presence_core.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace presence;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static std::string shex(const Bytes& b) { return hex(b.data(), b.size()); }

int main()
{
    // SHA-256 and HMAC against published vectors.
    CHECK(shex(sha256(reinterpret_cast<const uint8_t*>("abc"), 3))
          == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(shex(sha256(reinterpret_cast<const uint8_t*>(""), 0))
          == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    {
        const std::string key = "Jefe";
        const std::string data = "what do ya want for nothing?";
        CHECK(shex(hmacSha256(reinterpret_cast<const uint8_t*>(key.data()), key.size(),
                              reinterpret_cast<const uint8_t*>(data.data()), data.size()))
              == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    }
    // Long input crosses the 55/56-byte padding boundary and the two-block tail.
    {
        const std::string s(56, 'a');
        CHECK(shex(sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size())).size() == 64);
        const std::string s2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        CHECK(shex(sha256(reinterpret_cast<const uint8_t*>(s2.data()), s2.size()))
              == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    }

    // base64url round trip, pairing code round trip, bad codes rejected.
    {
        uint8_t raw[33];
        for (int i = 0; i < 33; ++i) raw[i] = uint8_t(i * 7 + 3);
        Bytes back;
        CHECK(base64urlDecode(base64url(raw, 33), back) && back.size() == 33 && std::memcmp(back.data(), raw, 33) == 0);
        Key32 k{};
        randomBytes(k.data(), k.size());
        Key32 k2{};
        const std::string code = encodePairingCode(k);
        CHECK(code.rfind("pres1_", 0) == 0 && code.size() == 6 + 44);
        CHECK(decodePairingCode(code, k2) && k == k2);
        CHECK(decodePairingCode("  " + code + "\n", k2) && k == k2);
        CHECK(!decodePairingCode("pres1_abc", k2));
        CHECK(!decodePairingCode("os1_" + code.substr(6), k2));
        CHECK(!decodePairingCode(code.substr(0, 49) + "!", k2));
    }

    // Epochs.
    CHECK(epochOf(0, 900) == 0);
    CHECK(epochOf(899, 900) == 0);
    CHECK(epochOf(900, 900) == 1);
    CHECK(epochOf(-1, 900) == -1);

    // Beacon slots: fixed count, magic on every slot, stable within an epoch,
    // different across epochs, and the contact can predict the tag.
    {
        Key32 seed{}, kab{};
        randomBytes(seed.data(), seed.size());
        randomBytes(kab.data(), kab.size());
        Contact alice;
        alice.name = "Alice";
        alice.key = kab;
        alice.myRole = 0;
        const auto s1 = beaconSlots(seed, {alice}, 100);
        const auto s1b = beaconSlots(seed, {alice}, 100);
        const auto s2 = beaconSlots(seed, {alice}, 101);
        CHECK(int(s1.size()) == kSlotsPerBeacon);
        CHECK(s1 == s1b);
        for (const auto& s : s1) CHECK(hasMagic(s));
        for (size_t i = 0; i < s1.size(); ++i) CHECK(s1[i] != s2[i]);
        // Alice (role 1) expects my tag computed with my role (0).
        CHECK(s1[1] == pairSlot(kab, 100, 0));
        CHECK(s1[1] != pairSlot(kab, 100, 1));
        // Padding is deterministic for the epoch (so it is indistinguishable from real slots).
        CHECK(s1[2] == paddingSlot(seed, 100, 0) && s1[3] == paddingSlot(seed, 100, 1));
        // No contacts: still four slots.
        CHECK(int(beaconSlots(seed, {}, 5).size()) == kSlotsPerBeacon);
        // Five contacts: slots stay at four, selection rotates by epoch.
        std::vector<Contact> many;
        for (int i = 0; i < 5; ++i) { Contact c; c.name = "c" + std::to_string(i); randomBytes(c.key.data(), 32); many.push_back(c); }
        CHECK(int(beaconSlots(seed, many, 7).size()) == kSlotsPerBeacon);
        CHECK(beaconSlots(seed, many, 7)[1] == pairSlot(many[2].key, 7, 0));
    }

    // Simulator + observer. Three devices for five epochs of 60 s.
    auto runRoom = [](AddressPolicy policy, int64_t addrPeriod) {
        SimRoom room;
        room.epochSeconds = 60;
        room.policy = policy;
        for (int i = 0; i < 3; ++i) {
            SimDevice d = makeSimDevice("d" + std::to_string(i));
            d.advIntervalMs = 1000;
            d.addrPeriod = addrPeriod;
            d.addrPhase = 13 * (i + 1);
            room.devices.push_back(d);
        }
        std::vector<Observation> all;
        for (int64_t ms = 0; ms <= 300 * 1000; ms += 500) {
            auto o = room.step(ms);
            all.insert(all.end(), o.begin(), o.end());
        }
        return analyse(all, 60);
    };
    {
        const LinkReport a = runRoom(AddressPolicy::Aligned, 60);
        std::printf("aligned : obs=%d addrs=%d slots=%d epochs=%d bridgeSlots=%d bridgeAddrs=%d trails=%d longest=%d epochs (%llds)\n",
                    a.observations, a.addresses, a.distinctSlots, a.epochsCovered, a.slotsBridgingAddresses,
                    a.addressesBridgingEpochs, a.trails, a.longestTrailEpochs, (long long)a.longestTrailSeconds);
        CHECK(a.observations > 800);
        CHECK(a.epochsCovered == 6);  // t = 0..300 covers epochs 0..5
        CHECK(a.slotsBridgingAddresses == 0);
        CHECK(a.addressesBridgingEpochs == 0);
        CHECK(a.longestTrailEpochs == 1);
        CHECK(a.trails == 3 * 6);
    }
    {
        const LinkReport d = runRoom(AddressPolicy::Drifting, 45);
        std::printf("drifting: obs=%d addrs=%d slots=%d epochs=%d bridgeSlots=%d bridgeAddrs=%d trails=%d longest=%d epochs (%llds)\n",
                    d.observations, d.addresses, d.distinctSlots, d.epochsCovered, d.slotsBridgingAddresses,
                    d.addressesBridgingEpochs, d.trails, d.longestTrailEpochs, (long long)d.longestTrailSeconds);
        CHECK(d.slotsBridgingAddresses > 0);
        CHECK(d.addressesBridgingEpochs > 0);
        CHECK(d.longestTrailEpochs >= 4);
        CHECK(d.trails <= 3);
    }
    {
        const LinkReport f = runRoom(AddressPolicy::Fixed, 60);
        std::printf("fixed   : obs=%d addrs=%d slots=%d epochs=%d bridgeSlots=%d bridgeAddrs=%d trails=%d longest=%d epochs (%llds)\n",
                    f.observations, f.addresses, f.distinctSlots, f.epochsCovered, f.slotsBridgingAddresses,
                    f.addressesBridgingEpochs, f.trails, f.longestTrailEpochs, (long long)f.longestTrailSeconds);
        CHECK(f.addresses == 3);
        CHECK(f.addressesBridgingEpochs == 3);
        CHECK(f.longestTrailEpochs == 6);
        CHECK(f.trails == 3);
    }

    // Headcount within one epoch under every policy: 3 devices.
    for (AddressPolicy pol : {AddressPolicy::Aligned, AddressPolicy::Drifting, AddressPolicy::Fixed}) {
        SimRoom room;
        room.epochSeconds = 60;
        room.policy = pol;
        for (int i = 0; i < 3; ++i) { SimDevice d = makeSimDevice("h" + std::to_string(i)); d.advIntervalMs = 1000; d.addrPeriod = 25; d.addrPhase = 7 * i; room.devices.push_back(d); }
        std::vector<Observation> all;
        for (int64_t ms = 0; ms <= 59 * 1000; ms += 500) { auto o = room.step(ms); all.insert(all.end(), o.begin(), o.end()); }
        CHECK(devicesSince(all, 0) == 3);
        CHECK(devicesSince(all, 1000000) == 0);
    }

    // Coarse steps (15 s, as a 60x clock produces) across epoch boundaries must
    // not stamp a pre-boundary advert with the post-boundary address.
    {
        SimRoom room;
        room.epochSeconds = 60;
        room.policy = AddressPolicy::Aligned;
        for (int i = 0; i < 4; ++i) { SimDevice d = makeSimDevice("c" + std::to_string(i)); d.advIntervalMs = 1000 + 137 * i; room.devices.push_back(d); }
        std::vector<Observation> all;
        for (int64_t ms = 0; ms <= 600 * 1000; ms += 15000) { auto o = room.step(ms); all.insert(all.end(), o.begin(), o.end()); }
        const LinkReport r = analyse(all, 60);
        CHECK(r.epochsCovered >= 10);
        CHECK(r.addressesBridgingEpochs == 0);
        CHECK(r.slotsBridgingAddresses == 0);
        CHECK(r.longestTrailEpochs == 1);
    }

    // makeSimDevice records a base cadence separate from the scaled one.
    {
        SimDevice d = makeSimDevice("base");
        CHECK(d.advBaseMs >= 800 && d.advBaseMs <= 1310 && d.advIntervalMs == d.advBaseMs);
    }

    if (failures == 0)
        std::printf("core_test: all checks passed\n");
    else
        std::printf("core_test: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
