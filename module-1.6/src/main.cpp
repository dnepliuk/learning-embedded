#include <Arduino.h>
#include <math.h>

#define LDR_PIN 8
#define LED_PIN 4

const float ADC_MAX_VALUE = 4095.0;
const float SUPPLY_VOLTAGE_MV = 3300.0;

void setup() {
  Serial.begin(115200);

  // Даємо Serial Monitor час підключитися після прошивки
  delay(2000);

  pinMode(LDR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  analogReadResolution(12);
  analogSetPinAttenuation(LDR_PIN, ADC_11db);

  Serial.println();
  Serial.println("ESP32 started");
  Serial.println("RAW\tCalculated, mV\tMeasured, mV\tError, mV\tError, %");
}

void loop() {
  int adcValue = analogRead(LDR_PIN);

  float calculatedVoltageMv =
      (adcValue / ADC_MAX_VALUE) * SUPPLY_VOLTAGE_MV;

  uint32_t measuredVoltageMv = analogReadMilliVolts(LDR_PIN);

  float errorMv =
      fabs(calculatedVoltageMv - measuredVoltageMv);

  float errorPercent = 0.0;

  if (measuredVoltageMv > 0) {
    errorPercent =
        (errorMv / measuredVoltageMv) * 100.0;
  }

  Serial.printf(
      "%d\t%.2f\t\t%lu\t\t%.2f\t\t%.2f%%\n",
      adcValue,
      calculatedVoltageMv,
      (unsigned long)measuredVoltageMv,
      errorMv,
      errorPercent
  );

  if (adcValue < 2000) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
  }

  delay(100);
}