# ESP-NOW Peer Ownership

ESPNowTransport owns the authoritative native ESP-NOW peer configuration used by higher-level ESPressio subsystems.

## Authoritative configuration

Subsystems such as mesh/discovery may establish a peer with explicit channel, interface, native encryption and LMK settings. A later consumer must not replace those settings merely because it also needs to communicate with the same MAC address.

`ESPNowClockSynchronizer` therefore treats peer registration as an existence requirement only:

- if the reference/source peer already exists in native ESP-NOW state, clock synchronization leaves it untouched;
- if the peer does not exist, clock synchronization may create a default peer so standalone clock-synchronizer use remains supported;
- Client/Reference role transitions must not downgrade encryption, replace the LMK, or rewrite channel/interface settings for an already configured peer.

This rule prevents clock synchronization from competing with mesh/security ownership of the same native peer and avoids role-dependent peer reconfiguration during periodic synchronization traffic.

## Hardware validation target

The regression was identified from repeatable boot-order asymmetry in the ESPressio EventConsole Lab. Validation should test both power-on orders, with WiFi disabled and enabled independently, while confirming that encrypted application Command/Event traffic remains bidirectional after Reference/Client election.

No version numbers are changed during the current mutable-development round.
