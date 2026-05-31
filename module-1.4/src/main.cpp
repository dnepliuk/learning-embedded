#include <Arduino.h>

#define EXTERNAL_BUTTON_PIN 4
#define BOOT_BUTTON_PIN 0

#define LED_1_PIN 5
#define LED_2_PIN 6

#define FAST_BLINK_DELAY 150
#define SLOW_BLINK_DELAY 700

#define DEBOUNCE_DELAY 50

int currentBlinkDelay = SLOW_BLINK_DELAY;

bool ledState = LOW;

int lastExternalButtonState = HIGH;
int lastBootButtonState = HIGH;

bool isButtonPressed(int buttonPin, int &lastButtonState) {
  int currentButtonState = digitalRead(buttonPin);

  bool pressed =
      lastButtonState == HIGH &&
      currentButtonState == LOW;

  if (pressed) {
    delay(DEBOUNCE_DELAY);

    pressed = digitalRead(buttonPin) == LOW;
  }

  lastButtonState = currentButtonState;

  return pressed;
}

void blinkLeds() {
  ledState = !ledState;

  digitalWrite(LED_1_PIN, ledState);
  digitalWrite(LED_2_PIN, !ledState);

  delay(currentBlinkDelay);
}


void setup() {
  Serial.begin(115200);

  pinMode(EXTERNAL_BUTTON_PIN, INPUT);

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  pinMode(LED_1_PIN, OUTPUT);
  pinMode(LED_2_PIN, OUTPUT);

  digitalWrite(LED_1_PIN, LOW);
  digitalWrite(LED_2_PIN, LOW);

  Serial.println("Систему запущено.");
  Serial.println("Зовнішня кнопка: швидке миготіння.");
  Serial.println("Кнопка BOOT: повільне миготіння.");
}

void loop() {
  if (isButtonPressed(EXTERNAL_BUTTON_PIN, lastExternalButtonState)) {
    currentBlinkDelay = FAST_BLINK_DELAY;
    Serial.println("Увімкнено швидкий режим миготіння.");
  }

  if (isButtonPressed(BOOT_BUTTON_PIN, lastBootButtonState)) {
    currentBlinkDelay = SLOW_BLINK_DELAY;
    Serial.println("Увімкнено повільний режим миготіння.");
  }

  blinkLeds();
}