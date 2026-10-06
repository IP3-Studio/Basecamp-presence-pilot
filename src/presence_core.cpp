#include "presence_core.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <unordered_map>

namespace presence {

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4), compact and allocation-free.

namespace {

constexpr uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void sha256Block(uint32_t h[8], const uint8_t block[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16)
             | (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = hh + S1 + ch + kSha256K[i] + w[i];
        const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void putU64BE(Bytes& b, uint64_t v)
{
    for (int i = 7; i >= 0; --i)
        b.push_back(uint8_t(v >> (i * 8)));
}

Slot16 slotFromMac(const Bytes& mac)
{
    Slot16 s{};
    std::memcpy(s.data(), kMagic, 4);
    std::memcpy(s.data() + 4, mac.data(), kPayloadBytes);
    return s;
}

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

}  // namespace

Bytes sha256(const uint8_t* data, size_t len)
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    size_t off = 0;
    for (; off + 64 <= len; off += 64)
        sha256Block(h, data + off);
    uint8_t tail[128] = {0};
    const size_t rem = len - off;
    std::memcpy(tail, data + off, rem);
    tail[rem] = 0x80;
    const size_t tailLen = rem < 56 ? 64 : 128;
    const uint64_t bits = uint64_t(len) * 8;
    for (int i = 0; i < 8; ++i)
        tail[tailLen - 1 - i] = uint8_t(bits >> (i * 8));
    sha256Block(h, tail);
    if (tailLen == 128)
        sha256Block(h, tail + 64);
    Bytes out(32);
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = uint8_t(h[i] >> 24);
        out[i * 4 + 1] = uint8_t(h[i] >> 16);
        out[i * 4 + 2] = uint8_t(h[i] >> 8);
        out[i * 4 + 3] = uint8_t(h[i]);
    }
    return out;
}

Bytes hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t len)
{
    uint8_t k[64] = {0};
    if (keyLen > 64) {
        const Bytes kh = sha256(key, keyLen);
        std::memcpy(k, kh.data(), 32);
    } else {
        std::memcpy(k, key, keyLen);
    }
    Bytes inner(64 + len);
    for (int i = 0; i < 64; ++i)
        inner[i] = k[i] ^ 0x36;
    std::memcpy(inner.data() + 64, data, len);
    const Bytes ih = sha256(inner.data(), inner.size());
    Bytes outer(64 + 32);
    for (int i = 0; i < 64; ++i)
        outer[i] = k[i] ^ 0x5c;
    std::memcpy(outer.data() + 64, ih.data(), 32);
    return sha256(outer.data(), outer.size());
}

void randomBytes(uint8_t* out, size_t len)
{
    if (FILE* f = std::fopen("/dev/urandom", "rb")) {
        const size_t got = std::fread(out, 1, len, f);
        std::fclose(f);
        if (got == len)
            return;
    }
    std::random_device rd;
    for (size_t i = 0; i < len; ++i)
        out[i] = uint8_t(rd());
}

std::string hex(const uint8_t* d, size_t n)
{
    static const char* digits = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s.push_back(digits[d[i] >> 4]);
        s.push_back(digits[d[i] & 15]);
    }
    return s;
}

bool unhex(const std::string& s, Bytes& out)
{
    if (s.size() % 2)
        return false;
    out.clear();
    out.reserve(s.size() / 2);
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < s.size(); i += 2) {
        const int hi = val(s[i]), lo = val(s[i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out.push_back(uint8_t(hi * 16 + lo));
    }
    return true;
}

std::string base64url(const uint8_t* d, size_t n)
{
    std::string s;
    size_t i = 0;
    for (; i + 3 <= n; i += 3) {
        const uint32_t v = (uint32_t(d[i]) << 16) | (uint32_t(d[i + 1]) << 8) | d[i + 2];
        s.push_back(kB64[(v >> 18) & 63]);
        s.push_back(kB64[(v >> 12) & 63]);
        s.push_back(kB64[(v >> 6) & 63]);
        s.push_back(kB64[v & 63]);
    }
    if (n - i == 1) {
        const uint32_t v = uint32_t(d[i]) << 16;
        s.push_back(kB64[(v >> 18) & 63]);
        s.push_back(kB64[(v >> 12) & 63]);
    } else if (n - i == 2) {
        const uint32_t v = (uint32_t(d[i]) << 16) | (uint32_t(d[i + 1]) << 8);
        s.push_back(kB64[(v >> 18) & 63]);
        s.push_back(kB64[(v >> 12) & 63]);
        s.push_back(kB64[(v >> 6) & 63]);
    }
    return s;
}

bool base64urlDecode(const std::string& s, Bytes& out)
{
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char c : s) {
        const char* p = std::strchr(kB64, c);
        if (!p || c == 0)
            return false;
        acc = (acc << 6) | uint32_t(p - kB64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t((acc >> bits) & 0xff));
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Key schedule

int64_t epochOf(int64_t unixSeconds, int64_t epochSeconds)
{
    if (epochSeconds <= 0)
        epochSeconds = 1;
    int64_t e = unixSeconds / epochSeconds;
    if (unixSeconds < 0 && unixSeconds % epochSeconds != 0)
        --e;
    return e;
}

Slot16 anonSlot(const Key32& seed, int64_t epoch)
{
    static const char* label = "presence/v1/anon";
    Bytes msg(label, label + std::strlen(label));
    putU64BE(msg, uint64_t(epoch));
    return slotFromMac(hmacSha256(seed.data(), seed.size(), msg.data(), msg.size()));
}

Slot16 pairSlot(const Key32& pairKey, int64_t epoch, int role)
{
    static const char* label = "presence/v1/tag";
    Bytes msg(label, label + std::strlen(label));
    putU64BE(msg, uint64_t(epoch));
    msg.push_back(uint8_t(role & 1));
    return slotFromMac(hmacSha256(pairKey.data(), pairKey.size(), msg.data(), msg.size()));
}

Slot16 paddingSlot(const Key32& seed, int64_t epoch, int index)
{
    static const char* label = "presence/v1/pad";
    Bytes msg(label, label + std::strlen(label));
    putU64BE(msg, uint64_t(epoch));
    msg.push_back(uint8_t(index));
    return slotFromMac(hmacSha256(seed.data(), seed.size(), msg.data(), msg.size()));
}

bool hasMagic(const Slot16& s)
{
    return std::memcmp(s.data(), kMagic, 4) == 0;
}

std::vector<Slot16> beaconSlots(const Key32& seed, const std::vector<Contact>& contacts, int64_t epoch)
{
    std::vector<Slot16> slots;
    slots.push_back(anonSlot(seed, epoch));
    const int tagSlots = kSlotsPerBeacon - 1;
    if (!contacts.empty()) {
        // More contacts than slots: rotate which ones get a tag this epoch.
        const size_t start = contacts.size() > size_t(tagSlots)
            ? size_t(((epoch % int64_t(contacts.size())) + int64_t(contacts.size())) % int64_t(contacts.size()))
            : 0;
        for (size_t i = 0; i < contacts.size() && slots.size() < size_t(kSlotsPerBeacon); ++i) {
            const Contact& c = contacts[(start + i) % contacts.size()];
            slots.push_back(pairSlot(c.key, epoch, c.myRole));
        }
    }
    for (int i = 0; slots.size() < size_t(kSlotsPerBeacon); ++i)
        slots.push_back(paddingSlot(seed, epoch, i));
    return slots;
}

std::string encodePairingCode(const Key32& key)
{
    Bytes b;
    b.push_back(0x01);
    b.insert(b.end(), key.begin(), key.end());
    return "pres1_" + base64url(b.data(), b.size());
}

bool decodePairingCode(const std::string& codeIn, Key32& key)
{
    std::string code;
    for (char c : codeIn)
        if (!std::isspace(static_cast<unsigned char>(c)))
            code.push_back(c);
    const std::string prefix = "pres1_";
    if (code.compare(0, prefix.size(), prefix) != 0)
        return false;
    Bytes b;
    if (!base64urlDecode(code.substr(prefix.size()), b))
        return false;
    if (b.size() != 33 || b[0] != 0x01)
        return false;
    std::copy(b.begin() + 1, b.end(), key.begin());
    return true;
}

// ---------------------------------------------------------------------------
// Observer: the DP-3T linkability check plus trail length.
//
// Build a graph whose nodes are addresses and slot values, with one edge per
// observation. Within an epoch, everything one device emits is legitimately
// connected (that is intra-epoch linkability, accepted by the definition).
// Across epochs the graph must fall apart: a slot seen under two addresses, or
// an address seen in two epochs, is a bridge. The connected components are
// the trails a passive observer can follow; their span is what they learn.

namespace {

struct DSU {
    std::vector<int> p;
    explicit DSU(int n) : p(n) { for (int i = 0; i < n; ++i) p[i] = i; }
    int find(int x) { while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; } return x; }
    void unite(int a, int b) { a = find(a); b = find(b); if (a != b) p[a] = b; }
};

}  // namespace

LinkReport analyse(const std::vector<Observation>& obs, int64_t epochSeconds)
{
    LinkReport r;
    r.observations = int(obs.size());
    if (obs.empty())
        return r;

    std::map<std::string, int> addrId;
    std::map<std::string, int> slotId;
    std::vector<std::pair<int, int>> edges;
    edges.reserve(obs.size());
    std::vector<int64_t> epochs;
    epochs.reserve(obs.size());
    std::set<int64_t> allEpochs;
    std::map<int, std::set<int>> addrsPerSlot;
    std::map<int, std::set<int64_t>> epochsPerAddr;

    for (const Observation& o : obs) {
        const std::string a = hex(o.addr.data(), o.addr.size());
        const std::string s = hex(o.slot.data(), o.slot.size());
        const int ai = addrId.emplace(a, int(addrId.size())).first->second;
        const int si = slotId.emplace(s, int(slotId.size())).first->second;
        const int64_t e = epochOf(o.t, epochSeconds);
        edges.emplace_back(ai, si);
        epochs.push_back(e);
        allEpochs.insert(e);
        addrsPerSlot[si].insert(ai);
        epochsPerAddr[ai].insert(e);
    }
    r.addresses = int(addrId.size());
    r.distinctSlots = int(slotId.size());
    r.epochsCovered = int(allEpochs.size());
    for (const auto& kv : addrsPerSlot)
        if (kv.second.size() > 1)
            ++r.slotsBridgingAddresses;
    for (const auto& kv : epochsPerAddr)
        if (kv.second.size() > 1)
            ++r.addressesBridgingEpochs;

    const int nA = r.addresses;
    DSU dsu(nA + r.distinctSlots);
    for (const auto& e : edges)
        dsu.unite(e.first, nA + e.second);

    std::unordered_map<int, std::pair<int64_t, int64_t>> span;   // root -> min t, max t
    std::unordered_map<int, std::set<int64_t>> compEpochs;
    for (size_t i = 0; i < obs.size(); ++i) {
        const int root = dsu.find(edges[i].first);
        auto it = span.find(root);
        if (it == span.end())
            span[root] = {obs[i].t, obs[i].t};
        else {
            it->second.first = std::min(it->second.first, obs[i].t);
            it->second.second = std::max(it->second.second, obs[i].t);
        }
        compEpochs[root].insert(epochs[i]);
    }
    r.trails = int(span.size());
    for (const auto& kv : span) {
        const int64_t secs = kv.second.second - kv.second.first;
        const int eps = int(compEpochs[kv.first].size());
        if (eps > r.longestTrailEpochs || (eps == r.longestTrailEpochs && secs > r.longestTrailSeconds)) {
            r.longestTrailEpochs = eps;
            r.longestTrailSeconds = secs;
        }
    }
    return r;
}

int devicesSince(const std::vector<Observation>& obs, int64_t fromT)
{
    std::map<std::string, int> addrId;
    std::map<std::string, int> slotId;
    std::vector<std::pair<int, int>> edges;
    for (const Observation& o : obs) {
        if (o.t < fromT || !hasMagic(o.slot))
            continue;
        const int ai = addrId.emplace(hex(o.addr.data(), o.addr.size()), int(addrId.size())).first->second;
        const int si = slotId.emplace(hex(o.slot.data(), o.slot.size()), int(slotId.size())).first->second;
        edges.emplace_back(ai, si);
    }
    if (edges.empty())
        return 0;
    const int nA = int(addrId.size());
    DSU dsu(nA + int(slotId.size()));
    for (const auto& e : edges)
        dsu.unite(e.first, nA + e.second);
    std::set<int> roots;
    for (const auto& e : edges)
        roots.insert(dsu.find(e.first));
    return int(roots.size());
}

// ---------------------------------------------------------------------------
// Simulator

const char* addressPolicyName(AddressPolicy p)
{
    switch (p) {
    case AddressPolicy::Aligned: return "aligned";
    case AddressPolicy::Drifting: return "drifting";
    case AddressPolicy::Fixed: return "fixed";
    }
    return "aligned";
}

bool addressPolicyFromName(const std::string& s, AddressPolicy& out)
{
    if (s == "aligned") { out = AddressPolicy::Aligned; return true; }
    if (s == "drifting") { out = AddressPolicy::Drifting; return true; }
    if (s == "fixed") { out = AddressPolicy::Fixed; return true; }
    return false;
}

SimDevice makeSimDevice(const std::string& name)
{
    SimDevice d;
    d.name = name;
    randomBytes(d.seed.data(), d.seed.size());
    uint8_t r[4];
    randomBytes(r, sizeof r);
    d.addrPhase = int64_t((uint32_t(r[0]) << 8 | r[1]) % 900);
    d.advIntervalMs = 800 + int64_t(r[2]) * 2;  // 0.8 to 1.3 s, like a real stack's jittered interval
    return d;
}

std::vector<Observation> SimRoom::step(int64_t nowMs)
{
    std::vector<Observation> out;
    const int64_t T = epochSeconds > 0 ? epochSeconds : 1;
    for (SimDevice& d : devices) {
        const int64_t deviceNow = nowMs / 1000 + d.skewSeconds;
        const int64_t epoch = epochOf(deviceNow, T);
        int64_t key = 0;
        switch (policy) {
        case AddressPolicy::Aligned: key = epoch; break;
        case AddressPolicy::Drifting: key = epochOf(deviceNow + d.addrPhase, d.addrPeriod > 0 ? d.addrPeriod : 900); break;
        case AddressPolicy::Fixed: key = 0; break;
        }
        if (key != d.addrKey) {
            d.addrKey = key;
            randomBytes(d.addr.data(), d.addr.size());
            d.addr[0] = uint8_t((d.addr[0] & 0x3f) | 0x40);  // resolvable-private-address shape
        }
        if (d.nextAdvMs == 0)
            d.nextAdvMs = nowMs;
        const std::vector<Slot16> slots = beaconSlots(d.seed, d.contacts, epoch);
        int emitted = 0;
        while (d.nextAdvMs <= nowMs && emitted < 64) {
            Observation o;
            o.t = d.nextAdvMs / 1000;
            o.addr = d.addr;
            o.slot = slots[size_t(d.slotIndex % kSlotsPerBeacon)];
            o.viaNetwork = false;
            out.push_back(o);
            d.slotIndex = (d.slotIndex + 1) % kSlotsPerBeacon;
            d.nextAdvMs += d.advIntervalMs > 0 ? d.advIntervalMs : 1000;
            ++emitted;
        }
        if (d.nextAdvMs < nowMs)
            d.nextAdvMs = nowMs;  // a long pause: do not replay the backlog
    }
    std::sort(out.begin(), out.end(), [](const Observation& a, const Observation& b) { return a.t < b.t; });
    return out;
}

}  // namespace presence
