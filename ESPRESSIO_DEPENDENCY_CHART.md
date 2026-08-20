# ESPressio Dependency Chart — ESP-Now 0.4.0

ESPressio ESP-Now keeps core dependencies small and exposes higher-level integrations only when selected by the application.

```text
ESPressio ESP-Now 0.4.x
|
+-- required --> ESPressio Timing >= 2.2.2 < 3.0.0
|
+-- optional --> ESPressio Event >= 5.7.1 < 6.0.0
|                  +-- used by ESPNowEventTransport
|
+-- optional --> ESPressio Command >= 0.2.0 < 1.0.0
|                  +-- used by ESPNowCommandTransport
|
+-- optional --> ESPressio Security >= 0.1.0 < 1.0.0
                   +-- used by ESPNowSecureTransport
```

## Security Placement

```text
Event / Command / Clock Sync / application protocol
                         |
                         v
                 ESPressio Security
                         |
                         v
                ESPNowSecureTransport
                         |
                         v
                   ESPNowTransport
                         |
                         v
                       ESP-NOW
```

Security is deliberately below application protocol semantics and above the concrete ESP-NOW radio transport. Event, Command, and Timing therefore do not gain direct Security dependencies merely because their payloads may be protected.

## Optional Dependency Rule

The normal:

```cpp
#include <ESPressio_ESPNow.hpp>
```

does not include headers that introduce Event, Command, or Security dependencies.

Applications explicitly select:

```cpp
#include <ESPressio_ESPNowEventTransport.hpp>
#include <ESPressio_ESPNowCommandTransport.hpp>
#include <ESPressio_ESPNowSecureTransport.hpp>
```

and must then provide the corresponding library dependency.

## PlatformIO

Core:

```ini
lib_deps =
    https://github.com/Flowduino/ESPressio-ESP-Now@^0.4.0
    https://github.com/Flowduino/ESPressio-Timing@^2.2.2
```

Security integration:

```ini
lib_deps =
    https://github.com/Flowduino/ESPressio-ESP-Now@^0.4.0
    https://github.com/Flowduino/ESPressio-Security@^0.1.0
    https://github.com/Flowduino/ESPressio-Timing@^2.2.2
```

Command/Event dependencies are added only when those adapters are compiled.

## Version Policy

Dependency ranges stay within the currently supported major line so a future breaking major release is not selected automatically.
