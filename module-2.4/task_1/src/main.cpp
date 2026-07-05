#include <Arduino.h>
#define BUTTON 7

volatile int8_t counter = 0;
int8_t lastPrintedCounter = 0;

void IRAM_ATTR onButtonPressed()
{
  counter++;
}

void setup()
{
  Serial.begin(115200);

  pinMode(BUTTON, INPUT);

  attachInterrupt(
      digitalPinToInterrupt(BUTTON),
      onButtonPressed,
      FALLING);
}

void loop()
{
  if (counter != lastPrintedCounter)
  {
    Serial.print("Counter:");
    Serial.println(counter);
    lastPrintedCounter = counter;
  }
}