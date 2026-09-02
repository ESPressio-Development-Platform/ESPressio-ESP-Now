# State Transport

ESPressio ESP-Now provides an optional adapter for the transport-neutral ESPressio State protocol.

```cpp
#include <ESPressio_ESPNowStateTransport.hpp>
```

The adapter maps ESP-NOW peer addresses to State `DeviceIdentifier` values and carries State update, acknowledgement, subscription, resynchronisation and disconnect traffic.

## Subscription enforcement

Incoming State must match local subscription policy before it is applied to `RemoteStateManager`. A valid ESP-NOW frame is not by itself permission to mutate State.

## Latest-only pending reliability

For each `(subscriber, StateType)` the transport retains at most the newest pending wire image. A newer revision supersedes an older pending revision rather than growing a historical queue.

This preserves State semantics over ESP-NOW instead of accidentally turning State into Event history.

## Acknowledgements

ACKs represent remote repository acceptance of a revision, not completion of arbitrary application work.

A stale ACK cannot clear a newer pending revision.

## Identity boundary

ESP-NOW MAC addresses are transport endpoints. State uses its own transport-neutral `DeviceIdentifier`; the adapter owns the mapping between them.

## Optional dependency

The ESP-NOW core does not acquire a mandatory State dependency merely because this adapter exists. Include and build it only when distributed State over ESP-NOW is required.