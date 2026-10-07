# Unlinkable presence on BLE mesh: the study, and what the Basecamp pilot simulates

> **Disclaimer.** This is personal, experimental research. It is not an official
> Logos publication and not an audited result. The pilot it describes is a hobby
> project provided as is, with no warranty of any kind and no affiliation with or
> endorsement by any of the projects it examines or interoperates with (BitChat,
> Briar, NYM, Logos, Basecamp). Facts about third-party software were read from
> their public repositories on the dates given and may have changed since.

*7 October 2026. This article presents the technical note "Unlinkable presence on
BLE mesh" (6 October 2026) and the Presence pilot in this repository, which
simulates the property the note finds missing. It is written for someone who
wants to reproduce the reading, challenge the assumptions, or extend the pilot.*

## 1. Scope and definitions

**D1. Unlinkable presence** is a radio-layer property. Given two BLE observations
at times t0 and t1, a passive receiver in range should not be able to decide
whether they were produced by the same device, beyond the information available
inside a single rotation epoch. Message confidentiality is a separate property.
So is sender and receiver unlinkability on an IP path.

**D2. Epoch.** The interval over which a device's on-air identifiers are held
constant. Linkability inside one epoch is accepted by D1; the property concerns
linkage across epoch boundaries.

**D3. Passive receiver.** Any device in radio range that logs advertisements
without transmitting. It sees, for every advertisement, the controller address
and the advertising payload, plus the packet's structure, length and timing.

**D4. Alignment.** Two identifiers travel in every advertisement: the controller
address (public, random static, or resolvable private address) and any stable
field in the payload (peer ID, long-term public key, nickname, service data).
They have to rotate together. If the address rotates and the payload identifier
does not, the payload bridges the address epochs. If the payload identifier
rotates and the address does not, the address bridges the payload epochs.
DP-3T and CEN treat this overlap as the linkability condition for proximity
identifiers. The note calls the condition *misalignment*, and it is sufficient
for linkage.

**Threat model.** The adversary is passive, in range, and may run the same open
source code as the target. It may hold a log from many places and many days. It
does not hold any device's secrets. Active adversaries (connect and probe, replay,
jamming) are outside D1 but are noted where a published result bears on the
design.

## 2. Systems under study

### 2.1 BitChat

Source: permissionlesstech/bitchat, WHITEPAPER.md v2.0 (6 July 2026),
docs/PEER-ID-ROTATION.md (merged 1 August 2026), PR #1704 (merged 24 September
2026), release v1.7.1 (31 July 2026). Code on `main` re-read on 6 October 2026.

Facts.

- **F-B1. Topology.** A node is simultaneously a GATT central and a GATT
  peripheral. It scans for BitChat advertisements, advertises its own service,
  and exchanges a compact binary packet. Relaying is a controlled flood: packets
  originate with TTL 7; dense nodes (at least 6 links) clamp broadcast TTL to 5;
  thin chains (at most 2 links) relay at the incoming depth. An LRU seen-set of
  1,000 entries with 5-minute expiry drops duplicates. Relays wait a random
  10 to 220 ms before forwarding.
- **F-B2. Packet.** Header: version, type, TTL, timestamp, flags; then an 8-byte
  sender ID, an optional 8-byte recipient ID, the payload, and an optional
  Ed25519 signature.
- **F-B3. Identity.** Each device holds two long-term key pairs in the platform
  keychain: a Curve25519 static key for Noise and an Ed25519 signing key. The
  on-air peer ID is `SHA-256(Noise static public key)[0:8]`. It is stable across
  sessions, reboots, and any reinstall that preserves the keychain, and changes
  only when the identity is replaced (the panic wipe). From 1.7.1 the identity
  keys are device-bound (`ThisDeviceOnly`), so an identity does not survive a
  backup restore.
- **F-B4. Announce.** Signed announcements carry the nickname, the Noise static
  public key and the Ed25519 public key in cleartext, plus up to 10
  direct-neighbour IDs with 60 s freshness. The whitepaper's own section 8 states
  the consequence: a passive receiver can enumerate participants and follow a
  device between places; unlinkable presence is not a current property. Origin
  packets leave at the default TTL, so hop distance identifies the originator.
- **F-B5. What does not rotate the peer ID.** The platform advertising
  identifier is not an input (no advertising SDK), so resetting it changes
  nothing. Controller address randomisation does not either: the payload still
  carries the stable ID and keys. A reboot does not. Aeroplane mode or RF
  shielding stops transmission and does not rotate the identity used on the next
  transmission.
- **F-B6. Sealed mail.** Live Noise sessions have forward secrecy. Courier
  envelopes do not: a courier tag is a 16-byte HMAC of the recipient's static
  public key and the UTC day, computable by any party that knows that key, and
  the key is broadcast in cleartext (F-B4). Compromise of the recipient static
  key exposes sealed but undelivered mail. Prekey forward secrecy for courier
  envelopes is listed as future work.
- **F-B7. The rotation draft.** `docs/PEER-ID-ROTATION.md` proposes
  `epoch = floor(unixSeconds / 3600)` (open parameter),
  `K_rot = HKDF-SHA256(ikm = noiseStaticPrivate, salt = "", info = "bitchat-peer-rotation-v1", L = 32)`,
  `peerID_e = HMAC-SHA256(K_rot, "bitchat-peer-id-v2" || uint32be(epoch))[0:8]`.
  Derivation is from the private key: deriving from the public key would let any
  observer who has seen that key compute every epoch ID. Implementations accept
  epoch-1, epoch and epoch+1. A second announcement type, announceV2 (0x2C),
  drops the static Noise keys, the Ed25519 keys, the nickname and the neighbour
  list; mutual favourites match on a pairwise tag from the shared secret; a
  signed binding inside the handshake ties the rotating ID to the static key
  after authentication. Rollout is phased behind capability bit 14. The draft
  states that rotating the peer ID alone is insufficient, because the first
  cleartext announce re-links the new ID to the static keys.
- **F-B8. Status on the wire.** The derivations and the announceV2 wire format
  are implemented and tested. Nothing emits announceV2. `BLEService` parses type
  0x2C and explicitly ignores it, with a comment that consuming it needs the
  replacement identity binding and a decision on how unverified presence appears
  in the peer list. Neither platform can ship the wire change alone.
- **F-B9. Service UUID rotation.** PR #1704 documents why rotating the service
  UUID that strangers scan for does not help: any value a stranger can
  pre-compute to find a device, an adversary running the same binary derives
  identically. The same note reports that GATT connect-and-probe identifies the
  app in about 3.7 s per device on a Raspberry Pi and is invariant under
  rotation, because the shape of the characteristic table is static.
- **F-B10. Security record.** CVE-2026-51376 is recorded against an iOS build
  (an unauthenticated MESSAGE packet inserted into the mesh gossip cache: denial
  of service by an adjacent attacker). The audit write-up of 28 January 2026
  reports a same-day patch; the CVE record lists no fixed version. 1.7.1 is later
  than the affected build. The version string in the CVE record does not match
  the project's release numbering, so the entry should be checked before it is
  cited further.

Assumptions BitChat makes, as read from the above.

- **AB1.** Strangers must be able to find a device without prior contact, so the
  advertisement must carry a value any peer can match. (F-B1, F-B9.)
- **AB2.** A long-lived peer ID is acceptable as the cost of AB1. (F-B3, F-B4.)
- **AB3.** Controller address randomisation by the platform is enough for the
  address half. (F-B5: the project does not manage the address.)
- **AB4.** Recognition between friends can be moved from cleartext keys to a
  pairwise tag, and the two changes have to ship together with the ID rotation.
  (F-B7.)
- **AB5.** A tag derived from a public value is acceptable for courier routing.
  (F-B6.) The draft's private-key derivation for the peer ID is the correction of
  this assumption for presence; the courier tag is not yet corrected.

### 2.2 Briar

Source: briarproject.org news post of 9 July 2026; briar-spec, BRP.md and
BTP.md; the Bluetooth transport plugin.

Facts.

- **F-R1. Contacts only.** Briar syncs only with contacts enrolled by QR code or
  link, over Bluetooth, local Wi-Fi, or Tor. It does not flood a public peer ID.
  It cannot use a non-contact as a relay.
- **F-R2. Pairwise secrets.** The Bramble Rendezvous Protocol derives a shared
  secret from the peers' key pairs and uses it to generate pseudo-random contact
  details known to both peers and to nobody else. The Bramble Transport Protocol
  keeps two keys per transport per contact, one for generating tags and one for
  encrypting streams, rotated per time period for forward secrecy; the period is
  at least 24 hours, to absorb clock differences.
- **F-R3. Bluetooth.** The Bluetooth transport uses a contact-specific service
  identifier derived from the shared secret, with the device non-discoverable.
  There is no broadcast that a stranger can match.
- **F-R4. Status.** The project has been in maintenance mode since 9 July 2026,
  receiving security and bug fixes only.

Assumptions Briar makes.

- **AR1.** Presence is only ever disclosed to a specific contact, through a value
  only that contact can compute. (F-R2, F-R3.)
- **AR2.** A rotation period of a day is short enough for the transport keys, so
  tags are stable within a day. (F-R2.) This is a forward-secrecy choice, not an
  unlinkability choice: within the period the tag links the device for anyone who
  can compute it, which by AR1 is one contact.
- **AR3.** Giving up stranger discovery is an acceptable price. (F-R1.)
- **AR4.** The platform's address randomisation is taken as given, as in AB3,
  with the difference that no Briar payload is matchable by a stranger in the
  first place, so misalignment reveals only that some Briar device is present.

### 2.3 Other systems, in brief

- **NYM** (Spl0itable/NYM), a BitChat-bridged client, has an opt-in Ghost Mode
  that replaces the Noise static key, the Ed25519 key, the advertised Bluetooth
  name, the nickname and the nostrLink with throwaway values and rotates them
  together on a jittered epoch of about 15 minutes. Retired identities remain
  decryptable for up to 8 rotations. Avatar and banner sharing is refused in the
  mode, because a repeated image re-links epochs faster than a key. This is a
  client policy; a stock peer still observes a stable identity from a non-ghost
  node, and the wire format is unchanged.
- **SimpleX and Cwtch** remove account identifiers on an IP or Tor path and
  define no BLE presence identifier.
- **GAEN and Find My** rotate BLE identifiers in production; both are proximity
  beacons, not multi-hop meshes.

## 3. Prior results

- Álvarez et al. (ARES 2019; JISA 54, 2020) showed that the periodic unique
  advertising address of a Bluetooth Mesh installation is sufficient for a phone
  app to localise a handset indoors, and that address randomisation on the mesh
  nodes stopped the positioning without affecting mesh operation. The identifier
  in that attack is the controller address, not an application peer ID.
- DP-3T, GAEN and TCN broadcast epoch-limited pseudonyms (EphID, RPI, CEN) and
  keep observed identifiers on the device. Linkability across a rotation requires
  the pseudonym epoch and the resolvable private address epoch to overlap. CEN
  decouples its ratchet from wall-clock time so it can be aligned with the
  controller. The DP-3T measurement logs recorded 38 of 160,556 cases in which
  the RPI stayed constant across a MAC rotation; those windows were under
  20 minutes, inside the RPI lifetime, and they are the overlap condition.
- ePrint 2020/1309 separates the DP-3T variants: the low-cost scheme derives the
  next day key by hashing the previous one and does not give trace
  unlinkability; DP3T-UNLINK is the variant with strong trace unlinkability
  across epochs.
- Shi et al. (USENIX Security 2024) model BLE untraceability in ProVerif and find
  four reuse conditions in pairing material (IRK, BD_ADDR, CSRK, ID_ADDR) that
  yield eight passive or active tracking attacks, each confirmed on at least two
  of 13 devices.
- Alghamdi, Verna and Mellia (arXiv 2609.26079, September 2026) show that
  advertising-layer metadata persists across RPA changes and that a decision
  tree can re-identify a target without a hand-written BLE signature. Payload
  rotation does not remove a stable advertisement shape, length, or timing.
- Heinrich et al. (USENIX Security 2021; ePrint 2021/893) showed that AirDrop's
  discovery leaks hashed contact identifiers that reverse in seconds, and
  replaced the exchange with private set intersection (PrivateDrop). This is the
  recognition problem of AB4 and AR1 solved in a different setting.

Table 1. Linkage condition is overlap between the address epoch and any stable
payload field.

| Identifier | Rotation | Linkage if the other layer is stable |
|---|---|---|
| Resolvable private address | Stack timeout (often 15 min) | Payload peer ID, key, or nickname bridges epochs |
| DP-3T EphID / GAEN RPI | Epoch, must match RPA | Misaligned MAC leaves a window of one epoch |
| BitChat peerID (shipped) | Identity replacement only | Stable by construction; announce keys dominate |
| BitChat peerID_e (draft) | Proposed 1 h, private-key HMAC | Cleartext announceV1 re-links on first packet |
| Courier recipient tag | UTC day, HMAC(public static) | Observer who saw the announce can recompute tags |
| Briar BTP tag | Per time period, at least 24 h | Computable by one contact only; strangers see no matchable value |

## 4. The Logos transport, and its boundary

Logos places metadata protection in the transport. The libp2p mixnet routes
through relays that shuffle and delay, with cover traffic. Since
logos-delivery-module v0.3.0 (30 September 2026) an application sets
`messagingOverrides.anonymityLevel` to `None`, `Preferred` or `Required` when
it creates its node; `Required` routes every send over a three-hop mix with
per-hop RLN rate limiting and fails rather than falling back, and needs a mix
pool of at least four nodes plus an exit. The testnet chat is documented as
sender-unlinkable over this path. Service discovery publishes a signed record of
address and capabilities into a Kademlia DHT; that record is intentionally
findable.

The boundary is exact. The mixnet hides correspondent pairing from a network
observer. It does not hide the fact that a local radio, or the local IP hop, is
emitting. Logos does not specify a BLE advertisement format, a rotating
proximity identifier, or an announce that withholds long-term keys. An
application that inherits the mixnet still has a presence problem if it
advertises a stable BLE payload. Network unlinkability and radio presence are
independent properties with independent fixes.

## 5. Platform constraints on the address half

- **macOS, CoreBluetooth.** A peripheral may place only a local name and a list
  of service UUIDs in its advertisement. The system rotates the resolvable
  private address on its own clock, about 15 minutes by the DP-3T measurements
  on Apple's stack, and the application can neither trigger nor observe the
  rotation. Basecamp's application bundle as shipped (0.3.0) carries no
  Bluetooth usage declaration, so a module that calls CoreBluetooth from inside
  it is terminated by the system.
- **Linux, BlueZ.** `Privacy = off` is the default in `main.conf`, so
  advertisements leave from the public address. With `Privacy = device` the
  stack uses resolvable private addresses on a kernel-side timer the application
  does not control from user space.
- **Owned radio.** Alignment in one step (new random address and new payload
  together, at the epoch boundary) is available where the application owns the
  controller: a second adapter driven over the HCI user channel, or a USB radio
  running purpose-built firmware.

## 6. What the pilot simulates

The pilot is a Basecamp `ui_qml` module. Its core is Qt-free C++17 and has no
radio driver. It implements the payload half of D4 and simulates the address
half under three policies, logs every advertisement as a passive receiver would,
and runs the linkability check over the log. The same slots can be published on
a Logos content topic through the delivery module at a chosen anonymity level.

**Key schedule.** Per device a 32-byte secret `seed`; per paired contact a
32-byte `K_AB` carried in a one-time pairing code; `T` = epoch length (900 s
default); `E = floor(unix / T)`.

```
anon_E        = HMAC-SHA256(seed, "presence/v1/anon" || uint64be(E))[0:12]
tag_{X,E}     = HMAC-SHA256(K_AB, "presence/v1/tag"  || uint64be(E) || role_X)[0:12]
pad_{E,i}     = HMAC-SHA256(seed, "presence/v1/pad"  || uint64be(E) || i)[0:12]
slot          = "LP01" || payload[12]                         (16 bytes, one per advertisement)
beacon_E      = [anon_E, tag_{me,E} per contact ..., pad ...]  (exactly 4 slots)
```

`role_X` is 0 for the side that created the pairing code and 1 for the side that
pasted it, so the two sides emit different tags and each can compute the other's.
Receivers accept E-1, E and E+1. Padding is derived rather than random so that
it repeats within the epoch like a real slot; the slot count is fixed so that it
does not vary with the number of contacts. The schedule follows F-B7 in deriving
from secret material and AR1 in deriving recognition from a pairwise secret. The
pairing secret is symmetric and trust-on-first-use; public-key pairing is not in
this build.

**Observer model.** Every advertisement is an observation (t, address, slot).
The analysis builds a graph with addresses and slot values as nodes and one edge
per observation, then reports: slots seen under more than one address (payload
bridging an address change), addresses seen in more than one epoch (address
bridging a payload change), the connected components (trails: what a passive
receiver can link together), and the longest trail in epochs and seconds. The
verdict is *unlinkable across epochs* when at least two epochs are covered, both
bridge counts are zero, and no trail spans more than one epoch. Headcount within
an epoch is the number of components among observations in that epoch.

**Simulator.** A room holds the user's own beacon and N synthetic strangers;
simulated friends carry the mirror of a pairing. Each device advertises one slot
per jittered interval, cycling through its four slots. Address policies:
*aligned* (a new random address at every epoch boundary, the owned-radio case),
*drifting* (a new address every 900 s on a per-device phase, the macOS case),
*fixed* (never, the stock BlueZ case). A simulated clock runs at 1x to 300x.

**Network twin.** With the radio set to network, the device publishes
`{v:1, e:E, s:[four slot hex strings]}` on `/logos-presence/1/room-<code>/json`
once per heartbeat (20 s) and at each epoch change, with the delivery node
created at the configured anonymity level. Received messages become four
observations whose pseudo-address is the first six bytes of the payload hash, so
one message is one device for the epoch and the next epoch's message is
unlinkable to it. Own echoes are dropped by payload hash.

## 7. Assumptions

Each assumption is a claim the pilot relies on. The tag names its origin.

- **A1 [BitChat F-B3, F-B4].** An 8-byte identifier derived from a long-term
  public key, or any long-term key sent in cleartext, links a device across every
  epoch regardless of address rotation.
- **A2 [BitChat F-B7, DP-3T].** A per-epoch identifier derived from secret
  material, with an announce that withholds long-term keys, removes cross-epoch
  linkability if and only if the controller address rotates at the same boundary.
- **A3 [BitChat F-B6].** Any value computable from a public key (the courier tag)
  is computable by every receiver that saw the announce, so a recognition value
  must derive from a secret the pair alone holds.
- **A4 [Briar F-R2].** A pairwise tag from a shared secret lets a contact
  recognise a device while a stranger learns only that a beacon of this kind is
  present.
- **A5 [Briar F-R1, BitChat AB1].** Stranger discovery and unlinkability cannot
  share one advertisement. The pilot therefore offers an anonymous count to
  strangers and recognition only to contacts.
- **A6 [Alghamdi 2026, Shi 2024].** Identifier rotation does not remove packet
  shape. A fixed slot count and a fixed slot length bound the residual to "a
  presence beacon of this kind", with contact count hidden.
- **A7 [BitChat F-B9].** An advertisement-only, non-connectable beacon removes
  the connect-and-probe fingerprint. (Simulated here; on macOS CoreBluetooth
  advertising may be connectable, which is G3 below.)
- **A8 [DP-3T, CEN].** Accepting E-1, E and E+1 absorbs clock skew of up to one
  epoch without extending a passive receiver's linkable window beyond one epoch,
  because acceptance is a receiver-side rule and changes nothing on the air.
- **A9 [Pilot].** Within one epoch, joining observations over shared addresses
  and shared slots counts devices exactly under every address policy; counting
  addresses alone over-counts under drift.
- **A10 [Platform, section 5].** macOS yields epoch-limited linkability of about
  one address period; default BlueZ yields full linkability; an owned radio
  yields the strict property.
- **A11 [Logos, section 4].** The mixnet unlinks publisher from message on the
  network path and leaves radio presence untouched; the network twin's
  unlinkability therefore rests on the payload schedule, not on the mixnet.
- **A12 [NYM].** Retired identities must stop being usable after a bounded
  number of epochs. The pilot keeps no retired material (no store-and-forward on
  the radio path), so the bound is zero; the assumption becomes relevant the
  moment late delivery is added.

## 8. Outcomes to check

Each outcome names the assumption it tests, the procedure on the Basecamp
install, the expected result, the result that falsifies it, and its status as of
7 October 2026. "CI" means asserted by the headless UI tests in this repository.

| # | Tests | Procedure | Expected | Falsified by | Status |
|---|---|---|---|---|---|
| O1 | A2 | Aligned policy, 3 strangers, 900 s epochs at 60x, run 10 epochs | 0 slot bridges, 0 address bridges, longest trail 1 epoch, trails = 4 per epoch | any bridge, or a trail of 2 epochs | CI asserts the verdict after 2 epochs; 10-epoch run pending |
| O2 | A2, A10 | Drifting policy, same room | bridges > 0 in both directions; longest trail grows with the ratio of address period to epoch | verdict stays unlinkable | CI asserts the flip; the proportion is unmeasured |
| O3 | A10 | Fixed policy, same room | trails = number of devices; longest trail = epochs observed | any device splitting into two trails | native core test; UI run pending |
| O4 | A4, A5 | Two instances (two user directories), exchange a pairing code | each sees the other as present within one epoch; a third instance without the key sees four indistinguishable slots | recognition fails, or the third instance distinguishes the tag slot | simulated friend asserted in CI; two-instance run pending |
| O5 | A6 | 0, 1, 2 and 3 contacts | exactly 4 distinct slots per trail per epoch in every case | any trail with a slot count that varies with contacts | pending |
| O6 | A9 | N strangers, F simulated friends, each policy | headcount = N + F + 1 from the first epoch in which every device has advertised | over-count under drift, or under-count under any policy | native core test; UI run pending |
| O7 | A11 | Two instances on logos.test, same room code, `Required` | both see network-sourced observations; verdict unlinkable; with no mix path the send fails and the panel says so; with `Preferred` it falls back | cross-epoch linkage of a publisher, or silent fallback under `Required` | not exercised; needs delivery_module installed and the mix pool up |
| O8 | A8 | One simulated device skewed by up to one epoch | still recognised; no bridge introduced | recognition lost, or a bridge appears | the core carries a skew field; no UI control yet |
| O9 | A10 | Sniffer (nRF52840 with the Nordic sniffer firmware) beside a Mac and a Linux box | macOS address period about 15 min, not steerable; BlueZ public address under defaults; owned radio aligns | any platform behaving otherwise | outside the pilot; not measured |

Outcomes O1 to O6 run on a single Basecamp with the simulated radio. O7 needs
two Basecamps and the testnet. O8 needs a one-line addition. O9 needs hardware.

## 9. Outstanding gaps in the pilot

- **G1. No radio.** The pilot proves the schedule and the observer argument and
  does not show that any Bluetooth stack rotates its address in step with a
  payload. The address policies are models of platforms, not measurements of
  them.
- **G2. No address control.** Even with a real driver, A2 cannot be met on macOS
  or on default BlueZ (section 5). The strict configuration needs an owned
  radio, and no driver for one exists in this repository.
- **G3. Non-connectable advertising unverified on macOS.** A7 is simulated. If
  CoreBluetooth advertising is always connectable, connect-and-probe (F-B9)
  remains available against a Mac even with no GATT services registered.
- **G4. Symmetric, trust-on-first-use pairing.** The code carries the shared
  secret itself. Whoever intercepts the code becomes the contact. Public-key
  pairing with an authenticated channel is not implemented.
- **G5. Shape and timing.** Slot count and length are fixed, but the simulated
  inter-advertisement interval is a model. The residual fingerprint of a real
  stack's timing (Alghamdi 2026) is untested.
- **G6. Network twin unexercised.** O7 has not run against the testnet. The
  delivery glue follows a module known to work on the same stack, and the
  anonymity key follows the published journey, but nothing here has been seen on
  the wire.
- **G7. No skew control.** O8 cannot be run from the interface.
- **G8. No export.** The observation log and the linkability report are shown on
  screen and not written to a file, so offline analysis of a long run, or
  comparison between two installs, is manual.
- **G9. Single built platform.** Only the darwin-arm64 package has been built
  and installed. The Linux packages come from the release workflow and are
  untested.
- **G10. Host constraints.** `delivery_module` is declared optional because
  Basecamp refuses to load a module whose required dependency is absent; the
  command-line installer drops the optional-dependency key from the installed
  manifest, so the host will not load `delivery_module` on the pilot's behalf.
  Basecamp carries no Bluetooth usage string, which blocks any future
  CoreBluetooth driver inside the shell until the host adds one.
- **G11. Retired material.** A12 is vacuous in this build because nothing is
  stored for late delivery. Any store-and-forward extension has to bound the
  decryptability window explicitly.

## 10. Reproduction

```
nix build .#integration-test -L         # five headless UI tests: load, connect, O1 verdict, O2 flip, simulated friend
c++ -std=c++17 -I src -o /tmp/core_test tests/core_test.cpp src/presence_core.cpp && /tmp/core_test
nix build .#lgx-portable                # package for Basecamp
```

Install the package into Basecamp's UI plugins directory (README), load the
module, and run O1 to O6 from the interface: the radio and policy controls are
in the left column, the headcount and friends in the centre, the observer tiles
and verdict on the right. For O4 and O7 launch a second Basecamp with
`--user-dir` pointing at an empty directory and install the package there too.

## References

- Technical note, "Unlinkable presence on BLE mesh", 6 October 2026.
- permissionlesstech/bitchat: WHITEPAPER.md v2.0 (6 July 2026), sections 3 to 5 and 8; docs/PEER-ID-ROTATION.md (PR #1487, 1 August 2026); PR #1704 (24 September 2026); release v1.7.1 (31 July 2026); PRIVACY_POLICY.md; `bitchat/Services/BLE/BLEService.swift` on `main`, 6 October 2026.
- CVE-2026-51376; audit note of 28 January 2026.
- briarproject.org, news post of 9 July 2026; briar-spec, BRP.md and BTP.md.
- Spl0itable/NYM, Ghost Mode.
- logos-co/logos-delivery-module releases v0.3.0 (30 September 2026) and v0.3.1 (5 October 2026); logos-co/logos-docs, journey "Use delivery and chat modules with libp2p-mix" (26 September 2026); docs.logos.co, "What is Logos", "About the Blend Network", "Logos Storage", retrieved 6 October 2026.
- Álvarez et al., "A Location Privacy Analysis of Bluetooth Mesh", ARES 2019; JISA 54, 2020.
- DP-3T/bt-measurements, linkability.md; ePrint 2020/1309.
- Shi et al., "Finding Traceability Attacks in the Bluetooth Low Energy Specification", USENIX Security 2024.
- Alghamdi, Verna and Mellia, "Learning to Link", arXiv 2609.26079, September 2026.
- Heinrich, Hollick, Schneider, Stute and Weinert, "PrivateDrop", USENIX Security 2021; ePrint 2021/893.
- Apple, CBPeripheralManager documentation; BlueZ `src/main.conf`, Privacy option.
