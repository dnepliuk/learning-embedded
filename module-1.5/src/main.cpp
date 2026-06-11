#include <Arduino.h>

#define BUTTON_PIN 2
#define BURST_END_DELAY_US 100000UL

volatile uint32_t totalChanges = 0;
volatile uint32_t totalRisingEdges = 0;
volatile uint32_t totalFallingEdges = 0;
volatile uint32_t lastEdgeTimeUs = 0;

uint32_t pressCounter = 0;
uint32_t currentPressNumber = 0;

void IRAM_ATTR onButtonChange() {
  int level = digitalRead(BUTTON_PIN);

  totalChanges++;

  if (level == HIGH) {
    // LOW → HIGH
    totalRisingEdges++;
  } else {
    // HIGH → LOW
    totalFallingEdges++;
  }

  lastEdgeTimeUs = micros();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BUTTON_PIN, INPUT);

  attachInterrupt(
    digitalPinToInterrupt(BUTTON_PIN),
    onButtonChange,
    CHANGE
  );

  Serial.println();
  Serial.println("Raw button bounce measurement started");
  Serial.println("Press the button, hold it for 0.5-1 sec, then release it.");
  Serial.println();
}

void loop() {
  static uint32_t printedChanges = 0;
  static uint32_t printedRisingEdges = 0;
  static uint32_t printedFallingEdges = 0;

  uint32_t changes;
  uint32_t risingEdges;
  uint32_t fallingEdges;
  uint32_t lastEdge;

  noInterrupts();

  changes = totalChanges;
  risingEdges = totalRisingEdges;
  fallingEdges = totalFallingEdges;
  lastEdge = lastEdgeTimeUs;

  interrupts();

  bool newEdgesDetected = changes != printedChanges;

  bool signalIsStable =
    (micros() - lastEdge) > BURST_END_DELAY_US;

  if (newEdgesDetected && signalIsStable) {
    uint32_t newChanges = changes - printedChanges;
    uint32_t newRisingEdges = risingEdges - printedRisingEdges;
    uint32_t newFallingEdges = fallingEdges - printedFallingEdges;

    int stableLevel = digitalRead(BUTTON_PIN);

    Serial.println("================================");

    if (stableLevel == HIGH) {
      pressCounter++;
      currentPressNumber = pressCounter;

      Serial.print("Button action: ");
      Serial.println(currentPressNumber);

      Serial.println("Event: PRESS");
    } else {
      Serial.print("Button action: ");
      Serial.println(currentPressNumber);

      Serial.println("Event: RELEASE");
    }

    Serial.print("CHANGE edges: ");
    Serial.println(newChanges);

    Serial.print("RISING edges: ");
    Serial.println(newRisingEdges);

    Serial.print("FALLING edges: ");
    Serial.println(newFallingEdges);

    if (stableLevel == HIGH && newRisingEdges > 0) {
      Serial.print("Extra press triggers: ");
      Serial.println(newRisingEdges - 1);
    }

    Serial.println();

    printedChanges = changes;
    printedRisingEdges = risingEdges;
    printedFallingEdges = fallingEdges;
  }

  delay(1);
}