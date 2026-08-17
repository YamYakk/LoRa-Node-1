#pragma once

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <SSD1306Wire.h>

namespace DS18B20Reader {

constexpr uint8_t kDataPin = 47;
constexpr uint8_t kMaximumSensors = 3;

struct Discovery {
  bool lineHigh = false;
  bool busPresent = false;
  uint8_t count = 0;
  char addresses[kMaximumSensors][17]{};
};

inline OneWire& oneWire() {
  static OneWire instance(kDataPin);
  return instance;
}

inline DallasTemperature& sensors() {
  static DallasTemperature instance(&oneWire());
  return instance;
}

inline void formatAddress(const DeviceAddress address, char* output) {
  for (uint8_t index = 0; index < 8; ++index) {
    snprintf(output + index * 2, 3, "%02X", address[index]);
  }
}

inline Discovery discover() {
  Discovery discovery;
  pinMode(kDataPin, INPUT);
  delay(2);
  discovery.lineHigh = digitalRead(kDataPin) == HIGH;

  // Match the proven initialization pattern used by the earlier ESP32 project.
  sensors().begin();
  discovery.busPresent = oneWire().reset() == 1;

  const uint8_t deviceCount = sensors().getDeviceCount();
  for (uint8_t index = 0;
       index < deviceCount && discovery.count < kMaximumSensors;
       ++index) {
    DeviceAddress address;
    if (sensors().getAddress(address, index)) {
      formatAddress(address, discovery.addresses[discovery.count++]);
    }
  }
  return discovery;
}

inline void showAddress(SSD1306Wire& display, uint8_t sensorNumber, const char* address) {
  char label[20];
  snprintf(label, sizeof(label), "DS18B20 %u", sensorNumber);

  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 10, label);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 36, address);
  display.display();
}

}  // namespace DS18B20Reader
