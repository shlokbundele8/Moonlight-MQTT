// moonlight - a small MQTT 5.0 client for microcontrollers and Linux.
// SPDX-License-Identifier: MIT
#ifndef MOONLIGHT_TYPES_H
#define MOONLIGHT_TYPES_H

#include <stdint.h>
#include <stddef.h>

namespace moonlight {

// MQTT control packet types (high nibble of the fixed header byte 1).
enum PacketType : uint8_t {
  PKT_CONNECT     = 1,
  PKT_CONNACK     = 2,
  PKT_PUBLISH     = 3,
  PKT_PUBACK      = 4,
  PKT_PUBREC      = 5,
  PKT_PUBREL      = 6,
  PKT_PUBCOMP     = 7,
  PKT_SUBSCRIBE   = 8,
  PKT_SUBACK      = 9,
  PKT_UNSUBSCRIBE = 10,
  PKT_UNSUBACK    = 11,
  PKT_PINGREQ     = 12,
  PKT_PINGRESP    = 13,
  PKT_DISCONNECT  = 14,
  PKT_AUTH        = 15,
};

// Quality of Service levels.
enum QoS : uint8_t {
  QOS0 = 0,  // at most once
  QOS1 = 1,  // at least once
  QOS2 = 2,  // exactly once
};

// A subset of MQTT 5.0 property identifiers that this client reads or writes.
enum Property : uint8_t {
  PROP_PAYLOAD_FORMAT_INDICATOR = 0x01,
  PROP_MESSAGE_EXPIRY_INTERVAL  = 0x02,
  PROP_CONTENT_TYPE             = 0x03,
  PROP_RESPONSE_TOPIC           = 0x08,
  PROP_CORRELATION_DATA         = 0x09,
  PROP_SESSION_EXPIRY_INTERVAL  = 0x11,
  PROP_ASSIGNED_CLIENT_ID       = 0x12,
  PROP_SERVER_KEEP_ALIVE        = 0x13,
  PROP_RECEIVE_MAXIMUM          = 0x21,
  PROP_MAXIMUM_QOS              = 0x24,
  PROP_RETAIN_AVAILABLE         = 0x25,
  PROP_USER_PROPERTY            = 0x26,
  PROP_MAXIMUM_PACKET_SIZE      = 0x27,
  PROP_TOPIC_ALIAS_MAXIMUM      = 0x22,
  PROP_TOPIC_ALIAS              = 0x23,
};

// Library result codes. Zero or positive values are MQTT reason codes; negative
// values are local (transport / buffer / protocol) errors.
enum Error : int16_t {
  OK                = 0,
  ERR_TRANSPORT     = -1,   // socket read/write/connect failed
  ERR_BUFFER_FULL   = -2,   // outgoing packet did not fit in the work buffer
  ERR_MALFORMED     = -3,   // received bytes did not parse
  ERR_NOT_CONNECTED = -4,   // operation needs an active session
  ERR_TIMEOUT       = -5,   // expected a reply and none arrived in time
  ERR_PROTOCOL      = -6,   // server violated the protocol
  ERR_REFUSED       = -7,   // CONNACK reason code was not success (see lastReasonCode)
};

// Last Will and Testament. The broker publishes this message on the client's
// behalf if the client drops without a clean DISCONNECT.
struct Will {
  const char*    topic      = nullptr;  // nullptr = no will
  const uint8_t* payload    = nullptr;
  uint32_t       payloadLen = 0;
  QoS            qos        = QOS0;
  bool           retain     = false;
};

// Options passed to Client::connect().
struct ConnectOptions {
  const char* clientId      = "";
  const char* username      = nullptr;  // nullptr = no username
  const char* password      = nullptr;  // nullptr = no password
  uint16_t    keepAlive     = 60;       // seconds; 0 disables keep alive
  bool        cleanStart    = true;
  uint32_t    sessionExpiry = 0;        // seconds; 0 = session ends on disconnect
  uint16_t    receiveMaximum = 0;       // 0 = omit (server default)
  Will        will;                     // leave will.topic null for no will
};

// A message delivered to the application by the onMessage callback.
struct Message {
  const char*    topic;
  uint16_t       topicLen;
  const uint8_t* payload;
  uint32_t       payloadLen;
  QoS            qos;
  bool           retain;
  bool           dup;
};

}  // namespace moonlight

#endif  // MOONLIGHT_TYPES_H
