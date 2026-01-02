#include <Wire.h>
#include <Adafruit_DPS310.h>
#include <SD.h>

#define BUTTON_PIN A5
#define LED_PIN    8

Adafruit_DPS310 dps;
File logfile;

bool readyToStart = false;   // Button armed state
bool readyDelay = false;     // 5-second ready indicator active
bool launched = false;

unsigned long readyStartMillis = 0;
unsigned long loggingStartMillis = 0;
unsigned long lastSampleMillis = 0;

// For a 10s buffer at 60Hz: 10*60 = 600
const uint16_t BUFFER_SIZE = 600;

struct DataPoint {
  unsigned long timestamp;  // millis() relative to logging start
  float altitude;           // meters
};

// Circular buffer for pre-launch data
DataPoint buffer[BUFFER_SIZE];
uint16_t bufIndex = 0;
bool bufFull = false;

float launchAltitude = 0;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  while (!SD.begin(4)) {
    Serial.println("SD initialization failed!");
    delay(1000);
  }

  if (!dps.begin_I2C()) {
    Serial.println("Couldn't find DPS310!");
    while (1);
  }

  Serial.println("Ready. Press button to arm; LED will show 5s green before buffering data.");
}

void openNewLogFile() {
  char filename[12];
  for (int i = 0; i < 100; i++) {
    sprintf(filename, "%02d.csv", i);
    if (!SD.exists(filename)) {
      logfile = SD.open(filename, FILE_WRITE);
      if (logfile) {
        logfile.println("Timestamp,Altitude_m");
        logfile.flush();
        Serial.print("Logging to file: ");
        Serial.println(filename);
        return;
      }
    }
  }
  Serial.println("Error: couldn't create log file!");
}

void writeBufferToFile() {
  Serial.println("Writing buffered data to file...");
  for (uint16_t i = 0; i < (bufFull ? BUFFER_SIZE : bufIndex); i++) {
    DataPoint &dp = buffer[i];
    unsigned long ms = dp.timestamp;
    unsigned int minutes = (ms / 60000) % 60;
    unsigned int seconds = (ms / 1000) % 60;
    unsigned int msecs = ms % 1000;
    char timeStr[16];
    sprintf(timeStr, "%02u:%02u:%03u", minutes, seconds, msecs);

    logfile.print(timeStr);
    logfile.print(",");
    logfile.println(dp.altitude, 2);
  }
  logfile.flush();
  Serial.println("Buffer write complete.");
}

void loop() {
  static unsigned long lastButtonPress = 0;

  unsigned long currentMillis = millis();

  // Button press: start the 5-second ready period if not already armed or launched
  if (!readyToStart && !readyDelay && digitalRead(BUTTON_PIN) == LOW) {
    if (currentMillis - lastButtonPress > 300) {  // debounce 300ms
      readyDelay = true;
      readyStartMillis = currentMillis;
      Serial.println("Button pressed: Starting 5-second ready indicator...");
      digitalWrite(LED_PIN, HIGH); // Turn LED on steady green during ready delay
    }
    lastButtonPress = currentMillis;
  }

  // Handle the 5-second ready delay period
  if (readyDelay) {
    if (currentMillis - readyStartMillis >= 5000) {  // 5 seconds elapsed
      readyDelay = false;
      readyToStart = true;
      launchAltitude = 0;
      bufIndex = 0;
      bufFull = false;
      Serial.println("Ready period ended. Starting buffering for launch...");
      // LED remains on steady green during buffering
      digitalWrite(LED_PIN, HIGH);
    }
  }

  // If armed, not launched yet: buffer data and check for launch
  if (readyToStart && !launched && (currentMillis - lastSampleMillis >= 17)) { // 17 ms = ~60Hz
    lastSampleMillis = currentMillis;

    sensors_event_t temp_event, pressure_event;
    dps.getEvents(&temp_event, &pressure_event);
    float altitude = 44330 * (1.0 - pow(pressure_event.pressure / 1013.25, 0.1903));

    if (launchAltitude == 0) {
      launchAltitude = altitude;
      Serial.print("Baseline altitude: ");
      Serial.println(launchAltitude, 2);
    }

    // Store in circular buffer
    buffer[bufIndex].timestamp = currentMillis;
    buffer[bufIndex].altitude = altitude;
    bufIndex = (bufIndex + 1) % BUFFER_SIZE;
    if (bufIndex == 0) bufFull = true;

    // Serial.print("Buffered altitude: ");
    // Serial.println(altitude, 2);

    // Detect launch: altitude rise >= 2m
    if (altitude - launchAltitude >= 30) {
      Serial.println("Launch detected! Starting logging...");
      launched = true;
      openNewLogFile();
      writeBufferToFile();
      loggingStartMillis = currentMillis;
      // Keep LED on steady during logging
      digitalWrite(LED_PIN, HIGH);
    }
  }

  // Once launched, log live data every 17 ms (~60 Hz)
  if (launched && (currentMillis - lastSampleMillis >= 17)) {
    lastSampleMillis = currentMillis;

    sensors_event_t temp_event, pressure_event;
    dps.getEvents(&temp_event, &pressure_event);

    float altitude = 44330 * (1.0 - pow(pressure_event.pressure / 1013.25, 0.1903));

    unsigned long elapsedMs = currentMillis - loggingStartMillis;
    unsigned int minutes = (elapsedMs / 60000) % 60;
    unsigned int seconds = (elapsedMs / 1000) % 60;
    unsigned int msecs = elapsedMs % 1000;

    char timeStr[16];
    sprintf(timeStr, "%02u:%02u:%03u", minutes, seconds, msecs);

    Serial.print(timeStr);
    Serial.print(", ");
    Serial.println(altitude, 2);

    logfile.print(timeStr);
    logfile.print(",");
    logfile.println(altitude, 2);
    logfile.flush();
  }
}

