#pragma once

#include <Arduino.h>
#include <Preferences.h>

namespace UplinkSettings {

constexpr uint32_t kDefaultIntervalSeconds = 60;
constexpr uint32_t kMinimumIntervalSeconds = 60;
constexpr uint32_t kMaximumIntervalSeconds = 86400;
constexpr int16_t kNoRssi = INT16_MIN;

struct Values {
  uint32_t intervalSeconds = kDefaultIntervalSeconds;
  uint32_t uploadCount = 0;
  int16_t lastDownlinkRssi = kNoRssi;
};

inline Values load() {
  Preferences preferences;
  Values values;
  if (!preferences.begin("app", true)) return values;

  values.intervalSeconds = preferences.getUInt("interval", kDefaultIntervalSeconds);
  values.uploadCount = preferences.getUInt("count", 0);
  values.lastDownlinkRssi = preferences.getShort("rssi", kNoRssi);
  preferences.end();

  if (values.intervalSeconds < kMinimumIntervalSeconds ||
      values.intervalSeconds > kMaximumIntervalSeconds) {
    values.intervalSeconds = kDefaultIntervalSeconds;
  }
  return values;
}

inline void save(const Values& values) {
  Preferences preferences;
  if (!preferences.begin("app", false)) return;
  preferences.putUInt("interval", values.intervalSeconds);
  preferences.putUInt("count", values.uploadCount);
  preferences.putShort("rssi", values.lastDownlinkRssi);
  preferences.end();
}

inline bool applyIntervalCommand(const uint8_t* data, size_t length, Values& values) {
  if (length < 3 || data[0] != 'I' || data[1] != '=') return false;

  uint32_t seconds = 0;
  for (size_t index = 2; index < length; ++index) {
    if (data[index] < '0' || data[index] > '9') return false;
    seconds = seconds * 10 + (data[index] - '0');
    if (seconds > kMaximumIntervalSeconds) return false;
  }
  if (seconds < kMinimumIntervalSeconds) return false;

  values.intervalSeconds = seconds;
  return true;
}

}  // namespace UplinkSettings
