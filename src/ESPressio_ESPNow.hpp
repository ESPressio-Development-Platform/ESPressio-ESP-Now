#pragma once

#ifndef ESPRESSIO_ESPNOW_VERSION_MAJOR
#define ESPRESSIO_ESPNOW_VERSION_MAJOR 0
#endif

#ifndef ESPRESSIO_ESPNOW_VERSION_MINOR
#define ESPRESSIO_ESPNOW_VERSION_MINOR 6
#endif

#ifndef ESPRESSIO_ESPNOW_VERSION_PATCH
#define ESPRESSIO_ESPNOW_VERSION_PATCH 0
#endif

#ifndef ESPRESSIO_ESPNOW_VERSION_STRING
#define ESPRESSIO_ESPNOW_VERSION_STRING "0.6.0"
#endif

#include "ESPressio_ESPNowTypes.hpp"
#include "ESPressio_IESPNowTransportObserver.hpp"
#include "ESPressio_ESPNowPeerLiveness.hpp"
#include "ESPressio_ESPNowTransport.hpp"
#include "ESPressio_ESPNowClockSynchronizer.hpp"

/*
 * Higher-level integrations remain opt-in so the normal umbrella does not
 * acquire their dependencies merely because the implementations exist:
 *
 *   ESPressio_ESPNowEventTransport.hpp      -> ESPressio Event >=6.0.0 <7.0.0
 *   ESPressio_ESPNowEvents.hpp              -> ESPressio Event >=6.0.0 <7.0.0
 *   ESPressio_ESPNowTransportEventBridge.hpp-> ESPressio Event >=6.0.0 <7.0.0
 *   ESPressio_ESPNowCommandTransport.hpp    -> ESPressio Command >=0.4.0 <1.0.0
 *   ESPressio_ESPNowSecureTransport.hpp     -> ESPressio Security >=0.3.0 <1.0.0
 *
 * ESPressio Observable is a core dependency because transport and peer
 * lifecycle changes are observable directly from ESPNowTransport.
 */
