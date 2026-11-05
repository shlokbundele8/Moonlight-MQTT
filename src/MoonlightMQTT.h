// Moonlight MQTT - a small, simple, secure MQTT 5.0 client for microcontrollers.
// SPDX-License-Identifier: MIT
//
// Include this one header to use the library:
//     #include <MoonlightMQTT.h>
//
// It pulls in the core client and the transport adapters that fit the board
// you are building for. On Arduino and ESP you also get ArduinoTransport, which
// wraps any WiFiClient / WiFiClientSecure / EthernetClient / TinyGsmClient.
// On Linux / Raspberry Pi / desktop you also get PosixTransport (plain TCP) and
// TlsTransport (TLS over OpenSSL) for building and testing.
#ifndef MOONLIGHT_MQTT_H
#define MOONLIGHT_MQTT_H

#include "moonlight/Types.h"
#include "moonlight/Buffer.h"
#include "moonlight/Transport.h"
#include "moonlight/Client.h"

#if !defined(ARDUINO)
  #include "moonlight/PosixTransport.h"
  #include "moonlight/TlsTransport.h"
#endif

// Short alias so sketches can write `moonlight::Client` or just `Moonlight`.
namespace moonlight {}
namespace Moonlight = moonlight;

#endif  // MOONLIGHT_MQTT_H
