// Moonlight MQTT - ESP32 over WiFi with TLS (secure).
//
// This is the recommended setup for ESP32. It uses WiFiClientSecure, which runs
// mbedTLS on the chip, so the connection is encrypted and the server is checked
// against the CA certificate below.
//
// Fill in your WiFi name, password, broker address, and the broker CA cert.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <MoonlightMQTT.h>

using namespace moonlight;

const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASS = "your-password";
const char* BROKER    = "your-broker.example.com";
const uint16_t PORT   = 8883;

// Paste your broker's CA certificate here (PEM). This is how the device knows
// it is talking to the real broker and not a fake one.
const char* CA_CERT = R"EOF(
-----BEGIN CERTIFICATE-----
...your broker CA here...
-----END CERTIFICATE-----
)EOF";

WiFiClientSecure net;
ArduinoTransport<WiFiClientSecure> transport(net);
StaticClient<2048, 1024> mqtt(transport);

// Reopen the secure socket after a drop. Moonlight calls this for auto reconnect.
bool reopenTls(void*) {
  net.setCACert(CA_CERT);
  return net.connect(BROKER, PORT) == 1;
}

void onMessage(const Message& m, void*) {
  Serial.print("message on ");
  Serial.write((const uint8_t*)m.topic, m.topicLen);
  Serial.print(": ");
  Serial.write(m.payload, m.payloadLen);
  Serial.println();
}

void setup() {
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.println("\nWiFi connected");

  net.setCACert(CA_CERT);
  if (!net.connect(BROKER, PORT)) { Serial.println("TLS connect failed"); return; }

  mqtt.onMessage(onMessage);
  mqtt.enableAutoReconnect(reopenTls, nullptr);

  ConnectOptions opts;
  opts.clientId   = "moonlight-esp32";
  opts.keepAlive  = 30;
  opts.cleanStart = true;

  int16_t rc = mqtt.connect(opts);
  if (rc != OK) { Serial.print("MQTT connect failed, code "); Serial.println(rc); return; }
  Serial.println("MQTT connected");

  mqtt.subscribe("moonlight/esp32/cmd", QOS1);
}

unsigned long lastPublish = 0;

void loop() {
  mqtt.loop();   // keep this running so pings, reconnect, and messages work

  if (mqtt.connected() && millis() - lastPublish > 5000) {
    lastPublish = millis();
    mqtt.publish("moonlight/esp32/hello", "hi from esp32", QOS1);
  }
}
