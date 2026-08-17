#pragma once

#include <Adafruit_HTU21DF.h>
#include <Arduino.h>
#include <SSD1306Wire.h>
#include <Wire.h>

namespace HTU21DReader {

constexpr int kSdaPin = 40;
constexpr int kSclPin = 41;

struct Readings {
  float temperatureC;
  float humidityPercent;
};

inline TwoWire& bus() {
  static TwoWire instance(1);
  return instance;
}

inline Adafruit_HTU21DF& sensor() {
  static Adafruit_HTU21DF instance;
  return instance;
}

inline bool begin() {
  bus().begin(kSdaPin, kSclPin);
  return sensor().begin(&bus());
}

inline bool read(Readings& readings) {
  readings.temperatureC = sensor().readTemperature();
  readings.humidityPercent = sensor().readHumidity();
  return isfinite(readings.temperatureC) && isfinite(readings.humidityPercent);
}

inline size_t formatPayload(const Readings& readings, uint32_t uploadCount,
                            uint32_t intervalSeconds, int16_t lastDownlinkRssi,
                            char* buffer, size_t bufferSize) {
  if (lastDownlinkRssi == INT16_MIN) {
    return snprintf(buffer, bufferSize, "T=%.1fC,H=%.1f%%,R=NA,N=%lu,I=%lu",
                    readings.temperatureC, readings.humidityPercent,
                    static_cast<unsigned long>(uploadCount),
                    static_cast<unsigned long>(intervalSeconds));
  }
  return snprintf(buffer, bufferSize, "T=%.1fC,H=%.1f%%,R=%ddBm,N=%lu,I=%lu",
                  readings.temperatureC, readings.humidityPercent, lastDownlinkRssi,
                  static_cast<unsigned long>(uploadCount),
                  static_cast<unsigned long>(intervalSeconds));
}

inline void show(SSD1306Wire& display, const Readings& readings, const char* status) {
  char temperature[20];
  char humidity[20];
  snprintf(temperature, sizeof(temperature), "T: %.1f C", readings.temperatureC);
  snprintf(humidity, sizeof(humidity), "H: %.1f %%", readings.humidityPercent);

  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 4, temperature);
  display.drawString(64, 24, humidity);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 48, status);
  display.display();
}

}  // namespace HTU21DReader
