#include <Wire.h>
#include <LiquidCrystal.h>
#include <Adafruit_BMP280.h>
#include "DHT.h"

// ------------------- PIN SETUP -------------------
#define DHTPIN 7
#define DHTTYPE DHT11

// LCD pins
const int rs = 12, en = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

// BMP280 on I2C: SDA=A4, SCL=A5 on Arduino UNO [web:237][web:246]
Adafruit_BMP280 bmp;

// LDR on A0: 5V ─ LDR ─●─ 10k ─ GND, A0 at the ● [web:205][web:220]
const int LDR_PIN = A0;

DHT dht(DHTPIN, DHTTYPE);

// ------------------- PRESSURE HISTORY FOR FORECAST -------------------
const int PRESS_HISTORY_SIZE = 20;
float pressureHistory[PRESS_HISTORY_SIZE];
unsigned long pressureTime[PRESS_HISTORY_SIZE];
int pressureIndex = 0;
bool pressureFilled = false;

void recordPressure(float p_hpa) {
  pressureHistory[pressureIndex] = p_hpa;
  pressureTime[pressureIndex] = millis();
  pressureIndex = (pressureIndex + 1) % PRESS_HISTORY_SIZE;
  if (pressureIndex == 0) pressureFilled = true;
}

float getPressureTendency(float currentP) {
  if (!pressureFilled && pressureIndex < 2) return 0.0;

  unsigned long now = millis();
  unsigned long targetAgo = 30UL * 60UL * 1000UL; // ~30 minutes
  int bestIdx = -1;

  int count = pressureFilled ? PRESS_HISTORY_SIZE : pressureIndex;
  for (int i = 0; i < count; i++) {
    int idx = (pressureIndex - 1 - i + PRESS_HISTORY_SIZE) % PRESS_HISTORY_SIZE;
    unsigned long dt = now - pressureTime[idx];
    if (dt > targetAgo) {
      bestIdx = idx;
      break;
    }
  }

  if (bestIdx == -1) {
    bestIdx = pressureFilled ? pressureIndex : 0;
  }

  float oldP = pressureHistory[bestIdx];
  return currentP - oldP;
}

// simple forecast text using pressure trend + band + temp [web:182][web:190]
String getForecast(float tempC, float hum, float pressure) {
  float dP = getPressureTendency(pressure);

  String tendency;
  if (dP > 2.0)      tendency = "rapid rise";
  else if (dP > 0.6) tendency = "slow rise";
  else if (dP < -2.0) tendency = "rapid fall";
  else if (dP < -0.6) tendency = "slow fall";
  else                tendency = "steady";

  bool highP = pressure > 1016.0;
  bool lowP  = pressure < 1005.0;
  bool veryLowP = pressure < 1000.0;

  if (veryLowP && dP < -1.0) {
    return "Storm risk";
  }
  if (lowP && dP < -0.6) {
    return "Clouds/rain";
  }
  if (lowP && dP >= -0.6) {
    return "Unsettled";
  }
  if (highP && dP > 0.6) {
    if (tempC > 30.0) {
      return "Hot & clear";
    } else {
      return "Fair & clear";
    }
  }
  if (highP && fabs(dP) <= 0.6) {
    return "Stable fair";
  }
  if (!highP && !lowP) {
    if (dP > 0.6) {
      return "Improving";
    } else if (dP < -0.6) {
      return "Worsening";
    } else {
      return "No big change";
    }
  }

  return "Similar ahead";
}

// ------------------- SETUP -------------------
void setup() {
  Serial.begin(9600);

  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("Weather station");
  lcd.setCursor(0, 1);
  lcd.print("Init...");

  dht.begin();

  if (!bmp.begin(0x76)) {   // change to 0x77 if your module uses that addr [web:240][web:246]
    Serial.println("BMP280 not found!");
    lcd.clear();
    lcd.print("BMP280 ERROR");
    delay(2000);
  }

  pinMode(LDR_PIN, INPUT);

  delay(2000);
  lcd.clear();
}

// ------------------- MAIN LOOP -------------------
unsigned long lastDisplayChange = 0;
byte screen = 0;

void loop() {
  // read sensors
  float hum = dht.readHumidity();
  float tD  = dht.readTemperature();
  float tB  = bmp.readTemperature();
  float p   = bmp.readPressure() / 100.0F;        // hPa [web:240][web:249]
  float alt = bmp.readAltitude(1013.25);          // m, sea‑level ref [web:244][web:251]
  int   ldrRaw = analogRead(LDR_PIN);             // 0–1023 [web:220]

  if (isnan(hum) || isnan(tD)) {
    hum = -1;
    tD  = -1;
  }

  // update pressure history and forecast
  recordPressure(p);
  float dP = getPressureTendency(p);
  String fc = getForecast(tB, hum, p);

  // serial debug
  Serial.print("DHT T/H: "); Serial.print(tD); Serial.print("C ");
  Serial.print(hum); Serial.print("%  | BMP T: ");
  Serial.print(tB); Serial.print("C  P: ");
  Serial.print(p); Serial.print("hPa  Alt: ");
  Serial.print(alt); Serial.print("m  dP: ");
  Serial.print(dP); Serial.print("  LDR: ");
  Serial.println(ldrRaw);

  // LCD display: rotate every 2 seconds
  if (millis() - lastDisplayChange > 4000) {
    lastDisplayChange = millis();
    screen = (screen + 1) % 4;
    lcd.clear();

    if (screen == 0) {
      lcd.setCursor(0, 0);
      lcd.print("T:");
      lcd.print(tB, 1);
      lcd.print("C ");

      lcd.print("H:");
      lcd.print(hum, 0);
      lcd.print("%");

      lcd.setCursor(0, 1);
      lcd.print("LDR:");
      lcd.print(ldrRaw);
    }
    else if (screen == 1) {
      lcd.setCursor(0, 0);
      lcd.print("Press:");
      lcd.print(p, 1);
      lcd.print("hPa");

      lcd.setCursor(0, 1);
      lcd.print("Alt:");
      lcd.print(alt, 0);
      lcd.print("m");
    }
    else if (screen == 2) {
      lcd.setCursor(0, 0);
      lcd.print("dP30m:");
      lcd.print(dP, 2);
      lcd.print("hPa");

      lcd.setCursor(0, 1);
      lcd.print("Forecast:");
      // show first 8 chars of forecast
      for (byte i = 0; i < 8 && i < fc.length(); i++) {
        lcd.print(fc[i]);
      }
    }
    else if (screen == 3) {
      lcd.setCursor(0, 0);
      lcd.print("DHT:");
      lcd.print(tD, 1);
      lcd.print("C ");
      //added the below line
      lcd.print("H:");
      lcd.print(hum, 0);
      lcd.print("%");

      lcd.setCursor(0, 1);
      lcd.print("BMP:");
      lcd.print(tB, 1);
      lcd.print("C");
    }
  }

  delay(500);
}
