#include <Wire.h>
#include <LiquidCrystal.h>
#include <Adafruit_BMP280.h>
#include "DHT.h"
#include <math.h>

// ------------------- PIN SETUP -------------------
#define DHTPIN 7
#define DHTTYPE DHT11

const int rs = 12, en = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

Adafruit_BMP280 bmp;
bool bmpOk = false;

const int LDR_PIN = A0;
DHT dht(DHTPIN, DHTTYPE);

// ------------------- PRESSURE HISTORY -------------------
const int PRESS_HISTORY_SIZE = 20;
float pressureHistory[PRESS_HISTORY_SIZE];
unsigned long pressureTime[PRESS_HISTORY_SIZE];
int pressureIndex = 0;
bool pressureFilled = false;

// ------------------- TIMERS -------------------
unsigned long lastDisplayChange = 0;
unsigned long lastDHTRead = 0;
unsigned long lastBMPRead = 0;
unsigned long lastPressureRecord = 0;

byte screen = 0;

// ------------------- SENSOR VALUES -------------------
float hum = NAN;
float tD  = NAN;
float tB  = NAN;
float p   = NAN;
float alt = NAN;
int ldrRaw = 0;

// ------------------- CUSTOM LCD ICONS -------------------
// Only 8 custom chars are available: slots 0..7
byte iconSun[8] = {
  B00100,
  B10101,
  B01110,
  B11111,
  B01110,
  B10101,
  B00100,
  B00000
};

byte iconCloud[8] = {
  B00000,
  B00110,
  B01001,
  B10001,
  B11111,
  B11111,
  B00000,
  B00000
};

byte iconRain[8] = {
  B00110,
  B01001,
  B10001,
  B11111,
  B11111,
  B00100,
  B01010,
  B00100
};

byte iconStorm[8] = {
  B00110,
  B01001,
  B11111,
  B11111,
  B00100,
  B01110,
  B00100,
  B01000
};

byte iconThermo[8] = {
  B00100,
  B01010,
  B01010,
  B01010,
  B01010,
  B10001,
  B10001,
  B01110
};

byte iconDrop[8] = {
  B00100,
  B00100,
  B01010,
  B01010,
  B10001,
  B10001,
  B10001,
  B01110
};

byte iconUp[8] = {
  B00100,
  B01110,
  B10101,
  B00100,
  B00100,
  B00100,
  B00100,
  B00000
};

byte iconDown[8] = {
  B00100,
  B00100,
  B00100,
  B00100,
  B10101,
  B01110,
  B00100,
  B00000
};

// icon slot mapping
const byte ICON_SUN    = 0;
const byte ICON_CLOUD  = 1;
const byte ICON_RAIN   = 2;
const byte ICON_STORM  = 3;
const byte ICON_THERMO = 4;
const byte ICON_DROP   = 5;
const byte ICON_UP     = 6;
const byte ICON_DOWN   = 7;

// ------------------- HELPERS -------------------
void loadCustomChars() {
  lcd.createChar(ICON_SUN,    iconSun);
  lcd.createChar(ICON_CLOUD,  iconCloud);
  lcd.createChar(ICON_RAIN,   iconRain);
  lcd.createChar(ICON_STORM,  iconStorm);
  lcd.createChar(ICON_THERMO, iconThermo);
  lcd.createChar(ICON_DROP,   iconDrop);
  lcd.createChar(ICON_UP,     iconUp);
  lcd.createChar(ICON_DOWN,   iconDown);
}

void recordPressure(float p_hpa) {
  pressureHistory[pressureIndex] = p_hpa;
  pressureTime[pressureIndex] = millis();
  pressureIndex = (pressureIndex + 1) % PRESS_HISTORY_SIZE;
  if (pressureIndex == 0) pressureFilled = true;
}

float getPressureTendency(float currentP) {
  int count = pressureFilled ? PRESS_HISTORY_SIZE : pressureIndex;
  if (count < 2) return 0.0;

  unsigned long now = millis();
  unsigned long targetAgo = 30UL * 60UL * 1000UL; // 30 min
  int bestIdx = -1;

  for (int i = 0; i < count; i++) {
    int idx = (pressureIndex - 1 - i + PRESS_HISTORY_SIZE) % PRESS_HISTORY_SIZE;
    if (pressureTime[idx] == 0) continue;

    unsigned long dt = now - pressureTime[idx];
    if (dt >= targetAgo) {
      bestIdx = idx;
      break;
    }
  }

  if (bestIdx == -1) {
    unsigned long oldestTime = 0xFFFFFFFFUL;
    for (int i = 0; i < count; i++) {
      int idx = (pressureIndex - 1 - i + PRESS_HISTORY_SIZE) % PRESS_HISTORY_SIZE;
      if (pressureTime[idx] == 0) continue;

      if (pressureTime[idx] < oldestTime) {
        oldestTime = pressureTime[idx];
        bestIdx = idx;
      }
    }
  }

  if (bestIdx == -1) return 0.0;
  return currentP - pressureHistory[bestIdx];
}

String getForecast(float tempC, float humi, float pressure) {
  float dP = getPressureTendency(pressure);

  bool highP = pressure > 1016.0;
  bool lowP = pressure < 1005.0;
  bool veryLowP = pressure < 1000.0;

  if (veryLowP && dP < -1.0) return "Storm risk";
  if (lowP && dP < -0.6) return "Clouds/rain";
  if (lowP && dP >= -0.6) return "Unsettled";

  if (highP && dP > 0.6) {
    if (tempC > 30.0) return "Hot clear";
    return "Fair clear";
  }

  if (highP && fabs(dP) <= 0.6) return "Stable fair";

  if (!highP && !lowP) {
    if (dP > 0.6) return "Improving";
    if (dP < -0.6) return "Worsening";
    return "No change";
  }

  return "Similar";
}

byte getForecastIcon(String fc) {
  if (fc == "Hot clear" || fc == "Fair clear" || fc == "Stable fair") {
    return ICON_SUN;
  }
  else if (fc == "Clouds/rain") {
    return ICON_RAIN;
  }
  else if (fc == "Storm risk") {
    return ICON_STORM;
  }
  else {
    return ICON_CLOUD;
  }
}

int getLightPercent(int raw) {
  int pct = map(raw, 0, 1023, 0, 100);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

void print16(String text) {
  if (text.length() > 16) text = text.substring(0, 16);
  lcd.print(text);
}

// ------------------- DISPLAY SCREENS -------------------
void showScreen0_DHT() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.write(byte(ICON_THERMO));
  lcd.print(" DHT:");
  if (!isnan(tD)) lcd.print(tD, 1);
  else lcd.print("--.-");
  lcd.print((char)223);
  lcd.print("C");

  lcd.setCursor(0, 1);
  lcd.write(byte(ICON_DROP));
  lcd.print(" Hum:");
  if (!isnan(hum)) lcd.print(hum, 0);
  else lcd.print("--");
  lcd.print("%");
}

// UPDATED BMP SCREEN
void showScreen1_BMP() {
  lcd.clear();

  // Line 1: BMP temperature
  lcd.setCursor(0, 0);
  lcd.write(byte(ICON_THERMO));
  lcd.print("BMP T:");

  if (bmpOk && !isnan(tB)) {
    lcd.print(tB, 1);
  } else {
    lcd.print("--.-");
  }

  lcd.print((char)223);
  lcd.print("C");

  // Line 2: Pressure
  lcd.setCursor(0, 1);
  lcd.print("P:");

  if (bmpOk && !isnan(p)) {
    lcd.print(p, 1);
  } else {
    lcd.print("----.-");
  }

  lcd.print("hPa");
}

void showScreen2_Other() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Alt:");
  if (bmpOk && !isnan(alt)) lcd.print(alt, 0);
  else lcd.print("---");
  lcd.print("m");

  lcd.setCursor(9, 0);
  lcd.print("L:");
  lcd.print(getLightPercent(ldrRaw));
  lcd.print("%");

  lcd.setCursor(0, 1);
  lcd.print("LDR:");
  lcd.print(ldrRaw);
}

void showScreen3_Forecast() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("dP30:");
  if (bmpOk && !isnan(p)) {
    float dP = getPressureTendency(p);
    lcd.print(dP, 2);

    lcd.setCursor(15, 0);
    if (dP > 0.6) lcd.write(byte(ICON_UP));
    else if (dP < -0.6) lcd.write(byte(ICON_DOWN));
    else lcd.print("-");
  } else {
    lcd.print("N/A");
  }

  lcd.setCursor(0, 1);
  if (bmpOk && !isnan(p) && !isnan(tB)) {
    String fc = getForecast(tB, hum, p);
    lcd.write(byte(getForecastIcon(fc)));
    lcd.print(" ");

    String shortFc = fc;
    if (shortFc.length() > 14) shortFc = shortFc.substring(0, 14);
    lcd.print(shortFc);
  } else {
    print16("Fc:BMP not ready");
  }
}

void showCurrentScreen() {
  if (screen == 0) showScreen0_DHT();
  else if (screen == 1) showScreen1_BMP();
  else if (screen == 2) showScreen2_Other();
  else showScreen3_Forecast();
}

// ------------------- SETUP -------------------
void setup() {
  Serial.begin(9600);

  lcd.begin(16, 2);
  loadCustomChars();

  lcd.clear();
  lcd.print("Weather station");
  lcd.setCursor(0, 1);
  lcd.print("Init...");

  dht.begin();
  pinMode(LDR_PIN, INPUT);

  for (int i = 0; i < PRESS_HISTORY_SIZE; i++) {
    pressureHistory[i] = 0.0;
    pressureTime[i] = 0;
  }

  bmpOk = bmp.begin(0x76);
  if (!bmpOk) {
    Serial.println("BMP280 not found at 0x76");
    lcd.clear();
    lcd.print("BMP280 ERROR");
    lcd.setCursor(0, 1);
    lcd.print("Chk addr/wiring");
    delay(2500);
  }

  hum = dht.readHumidity();
  tD  = dht.readTemperature();

  if (isnan(hum)) hum = NAN;
  if (isnan(tD))  tD = NAN;

  if (bmpOk) {
    tB  = bmp.readTemperature();
    p   = bmp.readPressure() / 100.0F;
    alt = bmp.readAltitude(1013.25);

    if (isnan(tB))  tB = NAN;
    if (isnan(p))   p = NAN;
    if (isnan(alt)) alt = NAN;

    if (!isnan(p)) {
      recordPressure(p);
      lastPressureRecord = millis();
    }
  }

  ldrRaw = analogRead(LDR_PIN);

  delay(1200);

  screen = 0; // first screen after init: DHT
  showCurrentScreen();
  lastDisplayChange = millis();
  lastDHTRead = millis();
  lastBMPRead = millis();
}

// ------------------- LOOP -------------------
void loop() {
  unsigned long now = millis();

  // DHT11 every ~2 sec (sensor min sampling period is 1s) [web:13][web:100]
  if (now - lastDHTRead >= 2000UL) {
    lastDHTRead = now;

    float newHum = dht.readHumidity();
    float newTD  = dht.readTemperature();

    if (!isnan(newHum)) hum = newHum;
    if (!isnan(newTD))  tD = newTD;
  }

  // BMP280 every 1 sec
  if (bmpOk && (now - lastBMPRead >= 1000UL)) {
    lastBMPRead = now;

    float newTB  = bmp.readTemperature();
    float newP   = bmp.readPressure() / 100.0F;
    float newAlt = bmp.readAltitude(1013.25);

    if (!isnan(newTB))  tB = newTB;
    if (!isnan(newP))   p = newP;
    if (!isnan(newAlt)) alt = newAlt;
  }

  // LDR
  ldrRaw = analogRead(LDR_PIN);

  // Pressure history every 2 min
  if (bmpOk && !isnan(p) && (now - lastPressureRecord >= 120000UL)) {
    lastPressureRecord = now;
    recordPressure(p);
  }

  // Serial debug
  Serial.print("DHT T/H: ");
  if (!isnan(tD)) Serial.print(tD, 1); else Serial.print("N/A");
  Serial.print("C ");
  if (!isnan(hum)) Serial.print(hum, 0); else Serial.print("N/A");
  Serial.print("% | BMP T: ");
  if (bmpOk && !isnan(tB)) Serial.print(tB, 1); else Serial.print("N/A");
  Serial.print("C P: ");
  if (bmpOk && !isnan(p)) Serial.print(p, 1); else Serial.print("N/A");
  Serial.print("hPa Alt: ");
  if (bmpOk && !isnan(alt)) Serial.print(alt, 0); else Serial.print("N/A");
  Serial.print("m LDR: ");
  Serial.print(ldrRaw);
  Serial.print(" Light:");
  Serial.print(getLightPercent(ldrRaw));
  Serial.println("%");

  // Rotate every 4 sec
  if (now - lastDisplayChange >= 4000UL) {
    lastDisplayChange = now;
    screen = (screen + 1) % 4;
    showCurrentScreen();
  }
}
