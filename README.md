![Moonlight MQTT](https://raw.githubusercontent.com/shlokbundele8/Moonlight-MQTT/assets/banner.webp)

# Moonlight MQTT

![MQTT](https://img.shields.io/badge/MQTT-5.0-blue)
![Platforms](https://img.shields.io/badge/platforms-ESP32%20%7C%20ESP8266%20%7C%20Arduino%20%7C%20Raspberry%20Pi-green)
![License](https://img.shields.io/badge/license-MIT-lightgrey)
![Version](https://img.shields.io/badge/version-0.1.0-orange)

A small, simple, and secure **MQTT 5.0** client for microcontrollers.

Moonlight MQTT runs on ESP32, ESP8266, Arduino, Raspberry Pi, and other common
boards. You get full MQTT 5.0 with all three QoS levels, retained messages, last
will, auto reconnect, and TLS, over WiFi, Ethernet, or GSM. You choose the
buffer size yourself and every call returns a clear result, so messages never
vanish without a reason.

## Contents

- [Why Moonlight MQTT](#why-moonlight-mqtt)
- [Features](#features)
- [Install](#install)
- [Dependencies](#dependencies)
- [Quick start](#quick-start)
- [API reference](#api-reference)
- [Transports](#transports)
- [Buffer sizes](#buffer-sizes)
- [Security](#security)
- [Result codes](#result-codes)
- [Examples](#examples)
- [Broker compatibility](#broker-compatibility)
- [License](#license)

## Why Moonlight MQTT

Moonlight MQTT gives you full MQTT 5.0 with a simple API that runs the same way
on many boards. It is built around a few clear ideas:

- **You own the buffer.** You set the size once in the constructor, so you
  always know how much memory the client uses, and large messages are never
  dropped by surprise.
- **No silent failures.** Every call returns a clear result code, so you always
  know what happened.
- **Honest state.** A keep alive and a liveness check mean `connected()` tells
  the truth.
- **It never freezes.** `loop()` is non blocking, so your program keeps running
  while messages flow.

## Features

- MQTT 5.0 protocol
- QoS 0 (at most once), QoS 1 (at least once), QoS 2 (exactly once, full handshake)
- Retained messages
- Last will and testament
- Keep alive with a true liveness check
- Auto reconnect with backoff
- TLS using the stack already on your board (mbedTLS, BearSSL, OpenSSL)
- Works over WiFi, Ethernet, and GSM
- Fixed buffers, no hidden memory use, no exceptions
- One header to include

## Install

### Arduino IDE
Library Manager support is planned. For now, download this repository as a ZIP
and use `Sketch > Include Library > Add .ZIP Library`.

### PlatformIO
Add this repository to your `lib_deps`, or clone it into your project `lib`
folder. The library ships a `library.json`.

### Linux and Raspberry Pi (CMake)
```bash
cmake -S . -B build
cmake --build build
```

## Dependencies

- None for the core on ESP32, ESP8266, and Arduino. It uses only the board's own
  network client (`WiFiClient`, `WiFiClientSecure`, `EthernetClient`).
- The GSM example uses the `TinyGSM` library.
- The Linux and Raspberry Pi TLS transport uses OpenSSL.

## Quick start

```cpp
#include <MoonlightMQTT.h>
using namespace moonlight;

WiFiClient net;                                // use WiFiClientSecure for TLS
ArduinoTransport<WiFiClient> transport(net);
StaticClient<1024, 512> mqtt(transport);       // 1 KB receive, 512 B send

void onMessage(const Message& m, void*) {
  // m.topic / m.topicLen and m.payload / m.payloadLen are valid only here
}

void setup() {
  net.connect("broker.example.com", 1883);     // open the socket first
  mqtt.onMessage(onMessage);

  ConnectOptions opts;
  opts.clientId = "my-device";
  opts.keepAlive = 30;

  if (mqtt.connect(opts) == OK) {
    mqtt.subscribe("devices/my-device/cmd", QOS1);
  }
}

void loop() {
  mqtt.loop();                                  // call this often
  mqtt.publish("devices/my-device/data", "42", QOS1);
}
```

## API reference

### Client

| Method | Description |
| --- | --- |
| `Client(transport, rxBuf, rxCap, txBuf, txCap)` | Build a client with your own buffers. |
| `StaticClient<RxCap, TxCap>(transport)` | A client that holds the buffers for you. |
| `onMessage(cb, user)` | Set the callback for incoming messages. |
| `onDisconnect(cb, user)` | Set the callback for a dropped connection. |
| `enableAutoReconnect(reopen, user, minMs, maxMs)` | Reconnect after a drop, with backoff. |
| `disableAutoReconnect()` | Turn auto reconnect off. |
| `connect(opts, timeoutMs)` | Do the MQTT handshake. Returns `OK` or a negative code. |
| `loop()` | Pump the client. Call often. Returns `OK` or a negative code. |
| `publish(topic, payload, len, qos, retain)` | Send a message. QoS 0, 1, 2. |
| `publish(topic, text, qos, retain)` | Same, for a C string payload. |
| `subscribe(filter, maxQos, timeoutMs)` | Subscribe. Returns the granted QoS or a negative code. |
| `unsubscribe(filter, timeoutMs)` | Unsubscribe. Returns `OK` or a negative code. |
| `disconnect(reasonCode)` | Send DISCONNECT and close the link. |
| `connected()` | True while the session is active. |
| `lastReasonCode()` | The last MQTT reason code from the broker. |

### ConnectOptions

| Field | Meaning |
| --- | --- |
| `clientId` | The client id string. |
| `username`, `password` | Login details, or leave null. |
| `keepAlive` | Seconds between pings. 0 turns keep alive off. |
| `cleanStart` | Start a fresh session, or resume an old one. |
| `sessionExpiry` | Seconds the session lives after you disconnect. |
| `receiveMaximum` | How many QoS 1 and 2 messages the broker may send at once. |
| `will` | A `Will` the broker sends if you drop without a clean disconnect. |

### Message

| Field | Meaning |
| --- | --- |
| `topic`, `topicLen` | The topic the message arrived on. |
| `payload`, `payloadLen` | The message bytes. |
| `qos`, `retain`, `dup` | Delivery details. |

## Transports

Moonlight talks to the network through a small `Transport` interface, so it does
not care how the bytes travel.

| Transport | Use for |
| --- | --- |
| `ArduinoTransport<T>` | Any Arduino client: `WiFiClient`, `WiFiClientSecure`, `EthernetClient`, or `TinyGsmClient` for cellular. |
| `PosixTransport` | Plain TCP on Linux and Raspberry Pi. |
| `TlsTransport` | TLS over OpenSSL on Linux and Raspberry Pi. |

## Buffer sizes

You own the two buffers. `StaticClient<RxCap, TxCap>` holds them for you.

- `RxCap` must fit the largest message you receive (topic plus payload plus a
  little header).
- `TxCap` must fit the largest message you send.

If a message does not fit, the call returns `ERR_BUFFER_FULL`. It is never
dropped in silence.

## Security

TLS is the recommended way to connect. Moonlight does not ship its own crypto.
It uses the TLS stack already on your board, which gets security updates from the
platform.

| Board | TLS stack | Client to use |
| --- | --- | --- |
| ESP32 | mbedTLS | `WiFiClientSecure` |
| ESP8266 | BearSSL | `WiFiClientSecure` |
| Linux, Raspberry Pi | OpenSSL | `moonlight::TlsTransport` |

Certificate checking is on by default on the Linux transport, and you should set
a CA certificate on ESP boards. See `examples/Esp32_WiFi_Secure`.

Very small boards like the Arduino Uno do not have the memory for TLS. That is a
hardware limit. Plain MQTT still works on them.

## Result codes

Every call returns one of these. Values at or above zero mean success.

| Code | Meaning |
| --- | --- |
| `OK` | Success. |
| `ERR_TRANSPORT` | The socket failed. |
| `ERR_BUFFER_FULL` | The message did not fit in the buffer. |
| `ERR_MALFORMED` | The received bytes did not parse. |
| `ERR_NOT_CONNECTED` | The operation needs an active session. |
| `ERR_TIMEOUT` | A reply did not arrive in time. |
| `ERR_PROTOCOL` | The broker broke the protocol or sent DISCONNECT. |
| `ERR_REFUSED` | The broker refused the request. See `lastReasonCode()`. |

## Examples

- `examples/Esp32_WiFi_Secure` secure MQTT over TLS on ESP32
- `examples/Esp8266_WiFi` plain MQTT on ESP8266
- `examples/Arduino_SIM900A` plain MQTT on an Arduino with a SIM900A GSM module

## Broker compatibility

Moonlight MQTT works with standard MQTT 5.0 brokers, such as Mosquitto, EMQX,
and HiveMQ.

## License

MIT. See [LICENSE](LICENSE).
