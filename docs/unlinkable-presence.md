# Unlinkable presence: what the radio says about you, and a pilot built to test the fix

*6 October 2026. Companion to the technical note "Unlinkable presence on BLE mesh" and to the Presence pilot in this repository. Personal work, not an official Logos publication.*

## The ordinary thing

Walk into a room with a phone in your pocket and you have already introduced yourself. Not by name, and not to anyone you can see. The Bluetooth controller in the phone sends a short advertisement several times a second so that headphones, watches and nearby apps can find it. Every receiver in range logs that advertisement with the address it came from. Most of the time nothing is listening with intent, and when something is, the address changes every quarter of an hour on the better platforms, which is meant to be the end of the matter.

It stops being the end of the matter the moment an app puts something of its own in that advertisement. Mesh chat apps do this by design. They need strangers to find each other without a server, so they announce a peer identifier, and the identifier has to be stable long enough for a conversation to survive. The address underneath keeps rotating. The identifier on top does not. A listener who cares only has to read the part that stays still.

This article is about that layer: presence, the fact of being here, as distinct from the content of anything said. The question it asks is narrow. Given two advertisements heard at different times, can a passive listener decide whether the same device sent both? Where the answer is no beyond a short window, the property is called unlinkable presence. The note this article follows looked at what ships today, what the research says, and what the Logos stack contributes. This article adds a pilot you can run and a list of assumptions it exists to test.

## A name for the failure

Unlinkable presence has a precise shape. Two identifiers travel in every advertisement. One is the controller address, which the operating system or the chip rotates on its own schedule. The other is whatever the application places in the payload: a peer ID, a public key, a nickname, service data. The two have to rotate together. If the address changes and the payload does not, the payload bridges the two address periods. If the payload changes and the address does not, the address bridges the two payload periods. Either way the listener links the epochs. The contact tracing systems of 2020 treated this overlap as the definition of the linkability problem, and the DP-3T team measured it: in 38 of 160,556 observations the rolling identifier stayed constant across an address change, each window shorter than twenty minutes. Those windows are small, and they are exactly the condition.

So the property has two halves, and they belong to different owners. The payload half belongs to the app. The address half belongs to the platform. A protocol can get its own half right and still fail, because the platform underneath did not rotate at the same moment.

## What ships, and what it announces

BitChat is the most-installed example of a Bluetooth mesh chat, so the note read its protocol text closely. Its on-air peer identifier is the first eight bytes of the hash of the device's static Noise key. It survives sessions, reboots and reinstalls that keep the keychain, and changes only when the user replaces the identity. Signed announcements carry the nickname and both long-term public keys in cleartext, with a list of recent neighbours. The whitepaper's own privacy section says what follows: a passive receiver can enumerate participants and follow a device between places, and unlinkable presence is not a property of the current design.

The project knows this. A draft specification for peer ID rotation was merged on 1 August 2026. It derives a per-hour identifier from the private key, so that anyone who has seen the public key cannot compute the sequence, and it defines a second announcement type that withholds the static keys, the nickname and the neighbour list. Mutual favourites would recognise each other through a tag computed from a shared secret. The derivations and the wire format are implemented and tested. Nothing emits them. The receiving code accepts the new announcement type and drops it, with a comment explaining why, and the draft itself states the reason the two halves have to land together: rotating the identifier while still sending the cleartext announce re-links the new identifier on the first packet.

A later note in the same repository, merged on 24 September, closes off a tempting shortcut. Rotating the service identifier that strangers scan for achieves nothing, because any value a stranger can compute to find you, an adversary running the same open code computes identically. The same note reports that connecting to a device and probing its attribute table identifies the app in about four seconds and is unaffected by any rotation. That result shapes the pilot below: a presence beacon has to be an advertisement only, with nothing to connect to.

Around BitChat, one client has gone further on its own. NYM's Ghost Mode replaces every identifier in the announcement with throwaway values and rotates them together on a jittered fifteen-minute epoch, keeping retired identities decryptable for eight rotations and refusing to share avatars because a repeated image re-links epochs faster than a key does. It is a client policy. A stock peer in the same mesh still announces a stable identity, and the wire format is unchanged.

Briar never broadcasts a public identifier. It syncs only with contacts enrolled by QR code, and its transport protocols derive pseudo-random tags from each pairwise shared secret, rotated per time period. That is the recognition design the BitChat draft reinvents, published years earlier, and it comes at the cost of never using a stranger as a relay. The project entered maintenance mode on 9 July 2026. SimpleX and Cwtch remove account identifiers from the network path and define no radio presence at all.

## What the measurements say

The academic record points the same way from three directions. Álvarez and colleagues showed in 2019 that the periodic unique address of a Bluetooth mesh installation was enough for a phone app to locate a handset indoors, and that randomising the address stopped the positioning without disturbing the mesh. Shi and colleagues modelled the Bluetooth specification itself at USENIX Security 2024 and found four reuse conditions in the pairing material that yield eight tracking attacks, each confirmed on real devices. Alghamdi, Verna and Mellia reported in September 2026 that the metadata of an advertisement, its structure, length and timing, persists across address changes well enough for a decision tree to re-identify a target with no hand-written signature.

The third result matters for anyone designing the payload half. Identifier rotation removes the identifier. It does not remove the shape of the packet that carried it. A beacon whose length changes with the number of contacts, or whose timing follows a nickname, leaks through the rotation. The design below fixes the slot count for that reason.

Apple's AirDrop supplies the cautionary tale for recognition. Its discovery exchange sends hashes of the user's own contact identifiers, and the Darmstadt group showed in 2021 that those hashes reverse quickly enough to recover phone numbers from anyone in range who starts a share. Their replacement, PrivateDrop, does the recognition with private set intersection so that nothing reversible leaves the device. Recognising a friend without announcing who you are is the hard half of presence, and it has been solved before.

## What Logos contributes, and what it does not

Logos places metadata protection in the transport. Since the delivery module's 0.3.0 release on 30 September 2026, an application can ask for a message to be onion-routed through a three-hop mixnet with per-hop rate limiting, by setting an anonymity level of None, Preferred or Required when it creates its node. Required fails rather than falling back, which is the only setting under which a delivered message is necessarily an anonymous one. The testnet chat already runs this way. For the question of who published a given message, this is the strongest machinery in any of the systems the note examined.

It says nothing about radio presence. Logos defines no advertisement format, no rotating proximity identifier and no announcement that withholds long-term keys. An application that inherits the mixnet still has a presence problem if it advertises a stable payload over Bluetooth, because the mixnet hides the correspondent pairing on the network path and the radio is not on that path. The two properties are independent, and the note's clearest finding is that they need separate treatment. Logos covers one.

The desktop platforms set the limits for the other. On macOS an application can place only a local name and a list of service identifiers in its advertisement, the system rotates the address on its own clock at roughly fifteen minute intervals, and the application can neither trigger nor observe the rotation. The Basecamp shell as shipped also carries no Bluetooth usage declaration, so any module that touches the Bluetooth framework from inside it is terminated by the system before it sends anything. On Linux the BlueZ stack advertises from the fixed public address unless privacy is switched on in its configuration, which most installations never do. Strict alignment of both halves is possible today only where the application owns the radio outright: a second adapter driven directly, or a small USB radio running firmware written for the purpose.

## The design under test

The pilot in this repository implements the payload half and simulates the address half, so that the schedule and the observer argument can be exercised before anyone commits to hardware.

Every device holds a secret seed and, for each paired contact, a shared pairing key. Time is cut into epochs, fifteen minutes by default to match Apple's cadence. For each epoch the device computes an anonymous token as a keyed hash of the seed and the epoch number, and for each contact a tag as a keyed hash of the pairing key, the epoch number and a role byte, so that the two sides of a pairing emit different tags and each can predict the other's. Every advertisement carries one sixteen-byte slot: a fixed four-byte marker that says "a presence beacon" and twelve bytes of payload. A beacon emits exactly four slots per epoch, the token, then one tag per contact, then padding derived from the seed so that it repeats within the epoch like a real slot would. The count never varies with the number of contacts. Receivers accept the previous, current and next epoch to absorb clock skew. Nothing about other devices is written to disk.

The simulated room holds the user's own beacon and a configurable number of strangers, and runs under three address policies. Aligned rotates the address at the epoch boundary, which is what a dedicated radio would do. Drifting rotates it on a separate clock with a per-device offset, which is what macOS does. Fixed never rotates it, which is stock BlueZ. A passive sniffer logs every advertisement, and an observer panel runs the check the contact tracing teams ran: how many slots were seen under two addresses, how many addresses were seen in two epochs, and, by joining every advertisement that shares an address or a slot, how long is the longest trail one device leaves. The same slots can also be published on a Logos content topic through the delivery module at a chosen anonymity level, so that a second Basecamp in the same room sees the beacon and, with a pairing code, recognises it.

## Assumptions to test on the Basecamp install

Each of these is a claim the design makes. Each has a test that the pilot runs, or that a reader with two machines can run, and a result that would falsify it.

1. **Aligned rotation leaves trails one epoch long.** Under the aligned policy, over ten or more epochs, the observer panel reports zero slots bridging an address change, zero addresses bridging an epoch change, and a longest trail of one epoch. The sixty-times simulated clock makes ten fifteen-minute epochs pass in under three minutes. Any bridge, or any trail of two epochs, falsifies the schedule.

2. **A drifting address bridges epochs in proportion to the overlap.** Under the drifting policy the panel reports bridges in both directions and a longest trail that spans several epochs. The prediction is that the trail length tracks the ratio between the address period and the epoch length. The pilot's own test asserts the verdict flips to linkable; the proportion is the part still to be measured.

3. **A fixed address links everything.** Under the fixed policy the trail count equals the number of devices and the longest trail spans every epoch observed. This is the stock Linux outcome, and the reason the privacy setting matters.

4. **Paired contacts recognise each other, and nobody else can.** With a pairing code exchanged between two instances, each sees the other as present within one epoch of pairing. A third instance that holds no key sees the same beacons as four indistinguishable slots. The second half needs two machines or two user directories and is the first test to run on a real install.

5. **The fixed slot count hides the number of contacts.** The observer sees exactly four slots per trail per epoch whether the device has no contacts or three. A trail with a different slot count falsifies the padding rule.

6. **Headcount is exact within an epoch under every policy.** With N strangers and F simulated friends, the room reports N plus F plus one from the first epoch in which every device has advertised. The join over shared addresses and slots is what makes this hold under a drifting address, where counting addresses alone over-counts.

7. **The network twin carries the same property.** Two Basecamps on the test network, in the same room code, see each other's beacons tagged as network observations, and the observer verdict stays unlinkable, because each message's pseudo-address is a hash of its own payload and the payload changes every epoch. With the anonymity level set to Required and no mix path available, sending fails and the panel says so; with Preferred it falls back and the beacon is delivered without the mixnet. This is the one assumption that depends on infrastructure outside the pilot, and it has not been exercised yet.

8. **Clock skew up to one epoch is tolerated.** A device whose clock runs an epoch fast or slow is still recognised through the three-epoch acceptance window. The simulator carries a skew field for exactly this, and the next build should expose it as a control.

9. **The platforms behave as the note describes.** This cannot be tested inside the pilot and is listed so that nobody mistakes the simulation for a measurement: that macOS rotates the address on roughly a fifteen minute cadence and cannot be steered, that BlueZ advertises from the public address by default, and that a dedicated adapter or a small radio dongle can rotate both halves in one step. A sniffer and an afternoon settle each of them.

Two things the pilot does not test, by construction. It does not show that any Bluetooth stack rotates its address in step with a payload, because the radio is simulated. And it does not remove the fingerprint of the advertisement's shape and timing, which the 2026 measurement shows surviving rotation. The fixed slot count narrows that fingerprint to "a presence beacon of this kind" and no further.

## What this asks of the stacks

Nothing on the list of infrastructure the Gap Map keeps defines a rotating radio presence identifier, and nothing in the BitChat draft, the NYM client or the Briar protocols is on the wire for a stranger to receive today. The Logos mixnet answers the network half of the question as well as anything available, and leaves the radio half open. A specification for an aligned presence beacon, small enough to implement in a weekend and precise about which half the platform owns, is the missing piece, and the pilot is one concrete proposal for its payload layer, with its assumptions written down so that they can be broken.

The room you walked into at the start is still there, and the phone in your pocket is still talking. The pilot is an argument that it could say "I am here" and nothing else, for as long as you choose, and that the people who should recognise you would still know.

## References

- Technical note, "Unlinkable presence on BLE mesh", 6 October 2026, and the design memo behind this pilot.
- permissionlesstech/bitchat: WHITEPAPER.md v2.0; docs/PEER-ID-ROTATION.md (merged 1 August 2026); PR #1704, "why rotating the service UUID does not work" (merged 24 September 2026); release v1.7.1 (31 July 2026).
- Spl0itable/NYM, Ghost Mode.
- Briar: news post of 9 July 2026; briar-spec BRP.md and BTP.md.
- logos-co/logos-delivery-module v0.3.0 (30 September 2026) and v0.3.1 (5 October 2026); logos-co/logos-docs, journey "Use delivery and chat modules with libp2p-mix" (26 September 2026).
- Álvarez et al., "A Location Privacy Analysis of Bluetooth Mesh", ARES 2019; Journal of Information Security and Applications 54, 2020.
- DP-3T/bt-measurements, linkability.md; ePrint 2020/1309.
- Shi et al., "Finding Traceability Attacks in the Bluetooth Low Energy Specification", USENIX Security 2024.
- Alghamdi, Verna and Mellia, "Learning to Link", arXiv 2609.26079, September 2026.
- Heinrich, Hollick, Schneider, Stute and Weinert, "PrivateDrop", USENIX Security 2021; ePrint 2021/893.
- Apple, CBPeripheralManager documentation (supported advertisement keys); BlueZ main.conf, Privacy option.
