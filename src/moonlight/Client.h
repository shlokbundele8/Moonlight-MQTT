// moonlight - MQTT 5.0 client (client role only).
// SPDX-License-Identifier: MIT
#ifndef MOONLIGHT_CLIENT_H
#define MOONLIGHT_CLIENT_H

#include "Types.h"
#include "Transport.h"

// How many QoS 2 message ids can be in flight at once (one small slot each for
// incoming and outgoing). Override before including if you need more.
#ifndef MOONLIGHT_MAX_INFLIGHT
#define MOONLIGHT_MAX_INFLIGHT 8
#endif

namespace moonlight {

// Called for every PUBLISH the broker delivers. The Message (and the memory it
// points at) is only valid for the duration of the call; copy what you need.
typedef void (*MessageCallback)(const Message& msg, void* user);

// Called when the connection drops or the broker sends DISCONNECT.
typedef void (*DisconnectCallback)(int16_t reason, void* user);

// Called by loop() when it wants to reopen a dropped transport (auto reconnect).
// Reopen the socket here (for example WiFiClient.connect or PosixTransport::connect)
// and return true once the transport is connected again.
typedef bool (*ReopenCallback)(void* user);

class Client {
 public:
  // The two buffers are owned by the caller and must outlive the Client.
  //   rxBuf/rxCap: holds the largest incoming packet (topic + payload).
  //   txBuf/txCap: scratch space for building one outgoing packet.
  Client(Transport& transport,
         uint8_t* rxBuf, size_t rxCap,
         uint8_t* txBuf, size_t txCap);

  void onMessage(MessageCallback cb, void* user = nullptr) { onMsg_ = cb; onMsgUser_ = user; }
  void onDisconnect(DisconnectCallback cb, void* user = nullptr) { onDisc_ = cb; onDiscUser_ = user; }

  // Turn on auto reconnect. loop() will call reopen() after a drop, using an
  // exponential backoff between minMs and maxMs, then redo the MQTT handshake
  // with the options from the last connect(). The clientId/username/password
  // strings from those options must stay valid for the client's lifetime.
  void enableAutoReconnect(ReopenCallback reopen, void* user,
                           uint32_t minMs = 1000, uint32_t maxMs = 30000) {
    reopen_ = reopen; reopenUser_ = user;
    backoffMin_ = minMs; backoffMax_ = maxMs; backoff_ = minMs;
  }
  void disableAutoReconnect() { reopen_ = nullptr; }

  // Perform the MQTT handshake over an already open transport. Blocks until
  // CONNACK arrives or timeoutMs elapses. Returns OK or a negative Error.
  int16_t connect(const ConnectOptions& opts, uint32_t timeoutMs = 5000);

  // Pump the connection: read incoming packets, fire callbacks, send keep alive,
  // and (if enabled) auto reconnect. Call this often from your main loop.
  int16_t loop();

  // Publish a message. QoS 0, 1, and 2 are supported. Returns OK once sent;
  // QoS 1/2 acknowledgements are handled later inside loop().
  int16_t publish(const char* topic, const uint8_t* payload, uint32_t len,
                  QoS qos = QOS0, bool retain = false);
  int16_t publish(const char* topic, const char* payload, QoS qos = QOS0, bool retain = false);

  // Subscribe to a topic filter. Blocks for SUBACK up to timeoutMs. Returns the
  // granted QoS (0/1/2) on success, or a negative Error. A granted value of
  // 0x80 or higher from the broker is reported as ERR_REFUSED.
  int16_t subscribe(const char* topicFilter, QoS maxQos = QOS0, uint32_t timeoutMs = 5000);

  // Unsubscribe from a topic filter. Blocks for UNSUBACK up to timeoutMs.
  int16_t unsubscribe(const char* topicFilter, uint32_t timeoutMs = 5000);

  // Send DISCONNECT and close the transport. This also stops auto reconnect
  // until the next connect().
  void disconnect(uint8_t reasonCode = 0x00);

  bool    connected() const { return state_ == CONNECTED; }
  uint8_t lastReasonCode() const { return lastReason_; }

 private:
  enum State { DISCONNECTED, CONNECTED };
  enum RxStage { RX_TYPE, RX_LEN, RX_BODY };

  uint16_t nextPacketId();
  int16_t  pollOnce(uint32_t blockMs, uint8_t waitType);
  void     dispatch(uint8_t firstByte, const uint8_t* body, size_t bodyLen);
  void     fail(int16_t reason);
  int16_t  sendConnect(const ConnectOptions& opts);
  void     sendAck(uint8_t type, uint16_t packetId);   // PUBACK/PUBREC/PUBREL/PUBCOMP
  int16_t  tryReconnect();

  // Small fixed id sets for QoS 2 bookkeeping (no dynamic memory).
  struct IdSet {
    uint16_t ids[MOONLIGHT_MAX_INFLIGHT];
    uint8_t  count = 0;
    bool has(uint16_t id) const;
    bool add(uint16_t id);
    void remove(uint16_t id);
  };

  Transport& t_;
  uint8_t*   rx_;   size_t rxCap_;
  uint8_t*   tx_;   size_t txCap_;

  MessageCallback    onMsg_  = nullptr; void* onMsgUser_  = nullptr;
  DisconnectCallback onDisc_ = nullptr; void* onDiscUser_ = nullptr;
  ReopenCallback     reopen_ = nullptr; void* reopenUser_ = nullptr;

  State          state_       = DISCONNECTED;
  ConnectOptions opts_;                       // kept for auto reconnect
  bool           haveOpts_    = false;
  uint16_t       packetId_    = 0;
  uint16_t       keepAlive_   = 0;
  uint32_t       lastPing_    = 0;
  uint32_t       lastRx_      = 0;
  uint8_t        lastReason_  = 0;

  uint32_t backoffMin_  = 1000;
  uint32_t backoffMax_  = 30000;
  uint32_t backoff_     = 1000;
  uint32_t nextTry_     = 0;

  IdSet inQos2_;    // ids we received (PUBREC sent, waiting PUBREL)
  IdSet outQos2_;   // ids we sent (waiting PUBREC then PUBCOMP)

  // Reply capture for the blocking connect/subscribe/unsubscribe calls.
  uint8_t connackRc_  = 0xFF;
  int16_t subRc_      = -1;
  bool    unsubDone_  = false;

  // Incremental receive state (packets can arrive in pieces).
  RxStage  stage_       = RX_TYPE;
  uint8_t  rxFirst_     = 0;
  uint32_t rxRemaining_ = 0;
  size_t   rxFilled_    = 0;
  uint8_t  lenShift_    = 0;
  uint8_t  waitType_    = 0;
  bool     waitHit_     = false;
};

// Convenience: a Client that owns its two buffers, sized at compile time.
template <size_t RxCap = 1024, size_t TxCap = 512>
class StaticClient : public Client {
 public:
  explicit StaticClient(Transport& t)
      : Client(t, rxStore_, RxCap, txStore_, TxCap) {}
 private:
  uint8_t rxStore_[RxCap];
  uint8_t txStore_[TxCap];
};

}  // namespace moonlight

#endif  // MOONLIGHT_CLIENT_H
