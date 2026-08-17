// ============================================================================
// main.cpp — RadioLib LoRaWAN OTAA with session persistence
// Target: ESP32/ESP32-S3 + RadioLib (LoRaWAN)
// Requires:
//   - jgromes/RadioLib with LoRaWAN support
//   - Arduino-ESP32 core (Preferences)
//   - Your "config_node5.h" must define:
//       radio, node, joinEUI, devEUI, nwkKey, appKey, uplinkIntervalSeconds
//
// What this does
//   1) First run: OTAA join, then after first successful uplink it saves the
//      session (keys, nonces, FCnt) to NVS.
//   2) Reboot: restores session (no join), sends uplinks immediately, and saves
//      the session after every uplink to keep FCnt in sync.
//   3) Sends HTU21D data, the upload count, and last downlink RSSI once per wake.
//
// Tip: If you ever need to force a fresh join, temporarily clear NVS "lw"
//      namespace (see optional factory reset snippet below).
// ============================================================================
// --- add this include at top ---
// REPLACE WHOLE FILE

#include <Arduino.h>
#include <Preferences.h>
#include "config_node5.h"    // radio, node, joinEUI/devEUI/keys, Region/subBand
#include "DS18B20Reader.h"
#include "HeltecBoard.h"     // your board helpers (VEXT/OLED/batt/deepSleep)
#include "HTU21DReader.h"
#include "UplinkSettings.h"

// ---------- LoRaWAN session persistence ----------
Preferences _prefs;  // NVS namespace "lw"

// Try to restore saved session (nonces + session buffers).
// Returns true if RadioLib reports SESSION_RESTORED.
static bool lwRestore(LoRaWANNode& n) {
  if (!_prefs.begin("lw", false)) return false;

  if (_prefs.getBytesLength("n") != RADIOLIB_LORAWAN_NONCES_BUF_SIZE ||
      _prefs.getBytesLength("s") != RADIOLIB_LORAWAN_SESSION_BUF_SIZE) {
    _prefs.end();
    return false;
  }

  uint8_t nonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
  uint8_t sess  [RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
  _prefs.getBytes("n", nonces, sizeof(nonces));
  _prefs.getBytes("s", sess,   sizeof(sess));
  _prefs.end();

  if (n.setBufferNonces(nonces)  != RADIOLIB_ERR_NONE) return false;
  if (n.setBufferSession(sess)   != RADIOLIB_ERR_NONE) return false;

  int16_t st = n.activateOTAA();  // returns SESSION_RESTORED if accepted
  return (st == RADIOLIB_LORAWAN_SESSION_RESTORED);
}

// Save current session + nonces after a successful uplink to keep FCnt aligned.
static void lwSave(LoRaWANNode& n) {
  uint8_t nonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
  uint8_t sess  [RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
  memcpy(nonces, n.getBufferNonces(),  sizeof(nonces));
  memcpy(sess,   n.getBufferSession(), sizeof(sess));
  if (_prefs.begin("lw", false)) {
    _prefs.putBytes("n", nonces, sizeof(nonces));
    _prefs.putBytes("s", sess,   sizeof(sess));
    _prefs.end();
  }
}

void setup() {
  Serial.begin(115200);
  delay(30);

  // === ONE-TIME NVS RESET (clear saved LoRaWAN session) ===
  // Flash once with these 3 lines uncommented, power-cycle, then comment/remove.
  // Preferences p; p.begin("lw", false); p.clear(); p.end();
  // === END ONE-TIME RESET ===

  Heltec::begin();

  int16_t st = RADIOLIB_ERR_NONE;
  UplinkSettings::Values settings = UplinkSettings::load();

  DS18B20Reader::Discovery ds18b20;
  constexpr uint8_t kDs18b20ScanCount = 13;  // 0 through 120 seconds, every 10 seconds.
  for (uint8_t scan = 0; scan < kDs18b20ScanCount; ++scan) {
    ds18b20 = DS18B20Reader::discover();
    Serial.printf("DS18B20 scan %u/%u\n", scan + 1, kDs18b20ScanCount);
    Serial.printf("DS18B20 GPIO%d idle level: %s\n", DS18B20Reader::kDataPin,
                  ds18b20.lineHigh ? "HIGH" : "LOW");
    Serial.printf("DS18B20 bus present: %s\n", ds18b20.busPresent ? "yes" : "no");
    Serial.printf("DS18B20 sensors found: %u\n", ds18b20.count);
    for (uint8_t index = 0; index < ds18b20.count; ++index) {
      Serial.printf("DS%u address: %s\n", index + 1, ds18b20.addresses[index]);
      DS18B20Reader::showAddress(Heltec::display, index + 1, ds18b20.addresses[index]);
    }
    if (scan + 1 < kDs18b20ScanCount) delay(10000);
  }

  HTU21DReader::Readings readings{};
  const bool sensorReady = HTU21DReader::begin() && HTU21DReader::read(readings);
  if (!sensorReady) {
    Heltec::display.clear();
    Heltec::display.setFont(ArialMT_Plain_16);
    Heltec::display.setTextAlignment(TEXT_ALIGN_CENTER);
    Heltec::display.drawString(64, 24, "HTU21D error");
    Heltec::display.display();
    delay(2000);
    goto SLEEP;
  }

  HTU21DReader::show(Heltec::display, readings, "Sending...");
  delay(2000);

  // -------- Radio + LoRaWAN --------
  st = radio.begin();
  if (st != RADIOLIB_ERR_NONE) goto SLEEP;

  st = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  if (st != RADIOLIB_ERR_NONE) goto SLEEP;

  // Restore previous session if available; else join once
  if (!lwRestore(node)) {
    st = node.activateOTAA();
    if (st != RADIOLIB_LORAWAN_NEW_SESSION) goto SLEEP;
  }

  // -------- ONE uplink per wake --------
  {
    ++settings.uploadCount;
    char payload[64];
    const size_t payloadLength = HTU21DReader::formatPayload(
        readings, settings.uploadCount, settings.intervalSeconds,
        settings.lastDownlinkRssi, payload, sizeof(payload));

    uint8_t downlink[16]{};
    size_t downlinkLength = sizeof(downlink);
    st = node.sendReceive(reinterpret_cast<const uint8_t*>(payload), payloadLength, 1,
                          downlink, &downlinkLength);
    if (st >= RADIOLIB_ERR_NONE) {
      lwSave(node);                // CRITICAL: save after successful uplink
      if (st > RADIOLIB_ERR_NONE) {
        settings.lastDownlinkRssi = static_cast<int16_t>(lroundf(radio.getRSSI()));
        UplinkSettings::applyIntervalCommand(downlink, downlinkLength, settings);
      }
      UplinkSettings::save(settings);
    }
  }

SLEEP:
  if (sensorReady) {
    HTU21DReader::show(Heltec::display, readings, (st >= 0) ? "Sent" : "Send failed");
  }
  delay(2000);

  Heltec::deepSleep(settings.intervalSeconds);
}

void loop() {
  // never reached
}
