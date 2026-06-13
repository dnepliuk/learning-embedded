#include <Arduino.h>

const uint8_t RELAY_PIN = 4;     // IN релейного модуля
const uint8_t NTC_PIN = 5;       // Середня точка дільника напруги з NTC
const uint8_t RED_LED_PIN = 1;   // Червоний світлодіод
const uint8_t BLUE_LED_PIN = 2;  // Синій світлодіод

// --------------------------------------------------
// Логіка реле
// --------------------------------------------------

const uint8_t RELAY_ON = HIGH;    // Реле активне, навантаження працює
const uint8_t RELAY_OFF = LOW;  // Реле вимкнене, навантаження відключене

// --------------------------------------------------
// Пороги ADC
// --------------------------------------------------

const int OVERHEAT_THRESHOLD = 1750;  // Вхід в аварійний режим
const int RECOVERY_THRESHOLD = 1800;  // Повернення до штатного режиму

// --------------------------------------------------
// Інтервали часу
// --------------------------------------------------

const unsigned long SENSOR_READ_INTERVAL = 100;  // Зчитування NTC кожні 100 мс
const unsigned long SIREN_INTERVAL = 200;        // Перемикання LED кожні 200 мс

// --------------------------------------------------
// Змінні стану
// --------------------------------------------------

bool isOverheated = false;
bool redLedIsActive = true;

unsigned long lastSensorReadTime = 0;
unsigned long lastSirenSwitchTime = 0;

// --------------------------------------------------
// Усереднене зчитування ADC
// --------------------------------------------------

int readAverageAdc() {
  const int SAMPLE_COUNT = 16;
  long sum = 0;

  for (int i = 0; i < SAMPLE_COUNT; i++) {
    sum += analogRead(NTC_PIN);
  }

  return sum / SAMPLE_COUNT;
}

// --------------------------------------------------
// Керування реле
// --------------------------------------------------

void setRelay(bool enabled) {
  digitalWrite(RELAY_PIN, enabled ? RELAY_ON : RELAY_OFF);
}

// --------------------------------------------------
// Вимкнення світлодіодів
// --------------------------------------------------

void turnOffLeds() {
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(BLUE_LED_PIN, LOW);
}

// --------------------------------------------------
// Перехід в аварійний режим
// --------------------------------------------------

void enterOverheatMode(unsigned long currentTime) {
  isOverheated = true;

  // Вимикаємо реле та відключаємо умовне навантаження
  setRelay(false);

  // Одразу запускаємо світлову сигналізацію
  redLedIsActive = true;
  digitalWrite(RED_LED_PIN, HIGH);
  digitalWrite(BLUE_LED_PIN, LOW);

  lastSirenSwitchTime = currentTime;

  Serial.println("ALARM: overheating detected.");
  Serial.println("Relay command: OFF.");
}

// --------------------------------------------------
// Повернення до штатного режиму
// --------------------------------------------------

void returnToNormalMode() {
  isOverheated = false;

  // Вмикаємо реле та відновлюємо живлення навантаження
  setRelay(true);

  // Вимикаємо світлову сигналізацію
  turnOffLeds();

  Serial.println("Temperature normalized.");
  Serial.println("Relay command: ON.");
}

// --------------------------------------------------
// Поперемінне мигання LED
// --------------------------------------------------

void updateSiren(unsigned long currentTime) {
  if (!isOverheated) {
    return;
  }

  if (currentTime - lastSirenSwitchTime >= SIREN_INTERVAL) {
    lastSirenSwitchTime = currentTime;

    redLedIsActive = !redLedIsActive;

    digitalWrite(RED_LED_PIN, redLedIsActive ? HIGH : LOW);
    digitalWrite(BLUE_LED_PIN, redLedIsActive ? LOW : HIGH);
  }
}

// --------------------------------------------------
// Ініціалізація
// --------------------------------------------------

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(NTC_PIN, INPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BLUE_LED_PIN, OUTPUT);

  // Початковий стан:
  // реле увімкнене, навантаження працює,
  // світлодіоди вимкнені
  setRelay(true);
  turnOffLeds();

  // 12-бітний ADC
  analogReadResolution(12);

  // Розширений діапазон вимірювання ADC
  analogSetPinAttenuation(NTC_PIN, ADC_11db);

  Serial.println("System started.");
  Serial.println("Relay command: ON.");
}

// --------------------------------------------------
// Основний цикл
// --------------------------------------------------

void loop() {
  const unsigned long currentTime = millis();

  // Періодично зчитуємо значення з терморезистора
  if (currentTime - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
    lastSensorReadTime = currentTime;

    const int adcValue = readAverageAdc();
    const uint32_t voltageMv = analogReadMilliVolts(NTC_PIN);

    Serial.printf(
      "ADC: %d, Voltage: %lu mV, Mode: %s\n",
      adcValue,
      voltageMv,
      isOverheated ? "OVERHEAT" : "NORMAL"
    );

    // Перегрів: ADC опустився нижче критичного порога
    if (!isOverheated && adcValue <= OVERHEAT_THRESHOLD) {
      enterOverheatMode(currentTime);
    }

    // Охолодження: ADC знову піднявся вище порога
    if (isOverheated && adcValue >= RECOVERY_THRESHOLD) {
      returnToNormalMode();
    }
  }

  // Сирена працює незалежно від зчитування датчика
  updateSiren(currentTime);
}