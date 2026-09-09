/*
  Light Meter — Instrument Design
  Arduino Nano + BH1750 ambient light sensor + ST7789 TFT display

  Reads ambient light in lux, converts to an exposure value (EV) at a
  user-set ISO, and displays the corresponding aperture/shutter-speed
  pairing. ISO is cycled manually via a single push button.

  NOTE: This file is a documented reconstruction of the original sketch,
  rebuilt from the project's wiring diagram and recorded display output
  (ISO 200, LUX 1575, EV 10.8, f/5.6, 1/125) — the original source file
  was lost. 

  Libraries required (Arduino Library Manager):
    - BH1750 by Christopher Laws
    - Adafruit GFX Library
    - Adafruit ST7789

  Wiring (per pin mapping):
    BH1750   -> A4 (SDA), A5 (SCL), 5V, GND
    ST7789   -> D11 (SDA/MOSI), D13 (SCL/SCK), D10 (CS), D9 (RES), D8 (DC), 5V (BLK), GND
    Button   -> D2 to GND (uses internal pull-up)
*/

#include <Wire.h>              // Handles I2C communication (used by the BH1750 sensor)
#include <BH1750.h>            // Library for the BH1750 digital light sensor
#include <Adafruit_GFX.h>      // Core graphics library — provides drawing/text functions used by the display library
#include <Adafruit_ST7789.h>   // Driver library specific to the ST7789 TFT display
#include <SPI.h>               // Handles SPI communication (used by the ST7789 display)

//names to the pin numbers so the rest
#define TFT_CS   10   // Chip Select — tells the display when the Arduino is talking to it
#define TFT_RST  9    // Reset pin — lets the Arduino restart the display if needed
#define TFT_DC   8    // Data/Command pin — tells the display whether incoming data is a command or pixel data
#define BUTTON_PIN 2  // Digital pin the ISO-cycle button is wired to

// Creates the display object using the pins above, so later code can just call tft.something()
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// Creates the light sensor object, so later code can just call lightMeter.something()
BH1750 lightMeter;

// ---- ISO cycle ----
// The set of ISO values the button cycles through, in array form.
const int isoValues[] = {100, 200, 400, 800, 1600};
// Automatically counts how many values are in the array above (5), so we don't have to hardcode it.
const int numIsoValues = sizeof(isoValues) / sizeof(isoValues[0]);
// Tracks which ISO value is currently selected. Starts at index 1, which is ISO 200 —
// matching the ISO shown in the recorded display output.
int isoIndex = 1;

// ---- Aperture / shutter speed tables for EV lookup ----
// Standard full-stop aperture 
const float apertures[] = {1.4, 2.0, 2.8, 4.0, 5.6, 8.0, 11.0, 16.0};
//  shutter speed 
const int shutterDenominators[] = {4000, 2000, 1000, 500, 250, 125, 60, 30};

// ---- Debounce ----
// Debouncing prevents one physical button press from being read as multiple rapid presses
// due to tiny electrical noise when the button contacts touch.
unsigned long lastButtonPress = 0;      // Stores the timestamp (in milliseconds) of the last accepted press
const unsigned long debounceDelay = 250; // Minimum time (ms) that must pass before another press counts

void setup() {
  Serial.begin(9600);      // Starts serial communication so readings can be viewed on a computer via USB

  Wire.begin();             // Initializes the I2C bus so the Arduino can talk to the BH1750 sensor
  lightMeter.begin();       // Initializes the BH1750 sensor itself, using default settings

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // Sets the button pin to read HIGH by default and LOW when pressed,
  // using the Arduino's built-in pull-up resistor instead of needing an external one.

  tft.init(135, 240);       // Initializes the display and tells it its resolution (135 x 240 pixels)
  tft.setRotation(1);       // Rotates the display 90 degrees, matching how it's mounted in the housing
  tft.fillScreen(ST77XX_BLACK); // Clears the screen to a black background on startup
  tft.setTextColor(ST77XX_WHITE); // Sets all text to display in white
  tft.setTextSize(2);       // Sets the text to a readable, moderately large size
}

void loop() {
  // ---- Check for a button press to cycle ISO ----
  // digitalRead(BUTTON_PIN) == LOW means the button is currently pressed (because of INPUT_PULLUP).
  // The second condition makes sure enough time has passed since the last accepted press (debounce).
  if (digitalRead(BUTTON_PIN) == LOW && (millis() - lastButtonPress) > debounceDelay) {
    isoIndex = (isoIndex + 1) % numIsoValues;
    // Moves to the next ISO value in the array. The "% numIsoValues" wraps back to
    // index 0 once it goes past the last value, so the cycle repeats.
    lastButtonPress = millis(); // Records this moment as the last time the button was pressed
  }

  // ---- Read the sensor ----
  float lux = lightMeter.readLightLevel(); // Gets the current ambient light reading, in lux
  int iso = isoValues[isoIndex];           // Looks up the currently selected ISO value

  // ---- Calculate exposure value (EV) ----
  // EV100 is the exposure value assuming ISO 100, calculated from lux using a standard
  // photographic formula: EV100 = log2(lux / 2.5).
  float ev100 = log(lux / 2.5) / log(2);
  // log(x) is natural log in C++, so dividing by log(2) converts it to log base 2 (log2).

  // Adjusts EV100 to the actual selected ISO. Doubling ISO effectively adds 1 to EV,
  // so this scales the correction by how far the chosen ISO is from 100.
  float ev = ev100 + log((float)iso / 100.0) / log(2);

  // ---- Look up aperture/shutter pairing from EV ----
  // Rounds EV to the nearest whole number, then shifts it to line up with index 0
  // of the apertures/shutter arrays (chosen so typical daylight EVs fall in range).
  // constrain() clamps the result so it can never go outside the array's valid indices (0–7).
  int evIndex = constrain((int)round(ev) - 6, 0, 7);
  float aperture = apertures[evIndex];         // Looks up the suggested f-stop for this EV
  int shutterDenom = shutterDenominators[evIndex]; // Looks up the matching shutter speed

  // ---- Serial output for debugging ----
  // Prints all current values to the Serial Monitor, useful for checking behavior
  // without needing the physical display.
  Serial.print("Lux: "); Serial.print(lux);
  Serial.print("  ISO: "); Serial.print(iso);
  Serial.print("  EV: "); Serial.print(ev, 1);       // ", 1" means show 1 decimal place
  Serial.print("  f/"); Serial.print(aperture, 1);
  Serial.print("  1/"); Serial.println(shutterDenom);

  // ---- Display update ----
  tft.fillScreen(ST77XX_BLACK); // Clears the previous frame before drawing the new one

  tft.setCursor(5, 10);         // Moves the text cursor to x=5, y=10 pixels
  tft.print("ISO: "); tft.println(iso);

  tft.setCursor(5, 40);
  tft.print("LUX: "); tft.println(lux, 0); // ", 0" means show no decimal places for lux

  tft.setCursor(5, 70);
  tft.print("EV: "); tft.println(ev, 1);

  tft.setCursor(5, 100);
  tft.print("f/ "); tft.println(aperture, 1);

  tft.setCursor(5, 130);
  tft.print("1/ "); tft.println(shutterDenom);

  delay(500); // Waits half a second before the next reading, so the display isn't flickering constantly
}
