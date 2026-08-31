#pragma once

#ifndef ESPRESSIO_ESPNOW_VERSION_MAJOR
#define ESPRESSIO_ESPNOW_VERSION_MAJOR 0
#endif

#ifndef ESPRESSIO_ESPNOW_VERSION_MINOR
#define ESPRESSIO_ESPNOW_VERSION_MINOR 8
#endif

#ifndef ESPRESSIO_ESPNOW_VERSION_PATCH
#define ESPRESSIO_ESPNOW_VERSION_PATCH 0
#endif

#ifndef ESPRESSIO_ESPNOW_VERSION_STRING
#define ESPRESSIO_ESPNOW_VERSION_STRING "0.8.0"
#endif

#include "ESPressio_ESPNowTypes.hpp"
#include "ESPressio_IESPNowTransportObserver.hpp"
#include "ESPressio_ESPNowPeerLiveness.hpp"
#include "ESPressio_ESPNowTransport.hpp"
#include "ESPressio_ESPNowClockSynchronizer.hpp"

#if __has_include(<ESPressio_IRadio.hpp>)
#include "ESPressio_ESPNowRadio.hpp"
#endif

/*
 * Higher-level integrations remain opt-in so the normal umbrella does not
 * acquire their dependencies merely because the implementations exist:
 *
 *   ESPressio_ESPNowEventTransport.hpp      -> ESPressio Event
 *   ESPressio_ESPNowEvents.hpp              -> ESPressio Event
 *   ESPressio_ESPNowTransportEventBridge.hpp-> ESPressio Event
 *   ESPressio_ESPNowCommandTransport.hpp    -> ESPressio Command
 *   ESPressio_ESPNowSecureTransport.hpp     -> ESPressio Security
 *
 * ESPressio Observable is a core dependency because transport and peer
 * lifecycle changes are observable directly from ESPNowTransport.
 */
