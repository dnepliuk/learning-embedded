#include <Arduino.h>
#include <math.h>

#define LDR_PIN 8

const float ADC_MAX_VALUE = 4095.0;
const float SUPPLY_VOLTAGE_MV = 3300.0;

void setup()
{
  Serial.begin(115200);

  delay(2000);

  pinMode(LDR_PIN, INPUT);

  analogReadResolution(12);
  analogSetPinAttenuation(LDR_PIN, ADC_11db);

  Serial.println();
  Serial.println("ESP32 started");
  Serial.println("RAW\tCalculated, mV\tMeasured, mV\tError, mV\tError, %");
}

void loop()
{
  int adcValue = analogRead(LDR_PIN);

  float calculatedVoltageMv =
      (adcValue / ADC_MAX_VALUE) * SUPPLY_VOLTAGE_MV;

  uint32_t measuredVoltageMv = analogReadMilliVolts(LDR_PIN);

  float errorMv =
      fabs(calculatedVoltageMv - measuredVoltageMv);

  float errorPercent = 0.0;

  if (measuredVoltageMv > 0)
  {
    errorPercent =
        (errorMv / measuredVoltageMv) * 100.0;
  }

  Serial.printf(
      "%d\t%.2f\t\t%lu\t\t%.2f\t\t%.2f%%\n",
      adcValue,
      calculatedVoltageMv,
      (unsigned long)measuredVoltageMv,
      errorMv,
      errorPercent);
  delay(100);
}