#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HardwareSerial.h>
#include <TinyGPS++.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>
#include <WiFi.h> 
#include <WiFiManager.h>
#include <Adafruit_NeoPixel.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
//
// Firebase configuration
#define API_KEY "AIzaSyBZyMfIM95UUxAgNRfYqycw58hU20wIXo0"
#define DATABASE_URL "https://agrosense-npk-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define USER_EMAIL "aaktru089@gmail.com" 
#define USER_PASSWORD "adibtantowi16"

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Konfigurasi OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// Pin Konfigurasi
#define SDA_PIN 8
#define SCL_PIN 9
#define RS485_RX_PIN 39 
#define RS485_TX_PIN 40

// Pin pH Sensor
#define PH_RX_PIN 38  
#define PH_TX_PIN 21  

// Pin GPS
#define GPS_RX 5      
#define GPS_TX 4      

// Pin Button Navigation
#define BTN_UP 1        
#define BTN_DOWN 6      
#define BTN_BACK 7      
#define BTN_NEXT 47     

// Pin Sensor
#define TEMP_PIN 10     
#define SOIL_SENSOR_PIN 2   

// Konfigurasi LED RGB WiFi
#define LED_PIN 48
#define LED_COUNT 1
#define LED_BRIGHTNESS 50

// KONSTANTA KALIBRASI pH
#define PH_CALIBRATION_SLOPE 0.89137931    
#define PH_CALIBRATION_OFFSET 1.157586207    

#define JUMLAH_DATA 600

// Inisialisasi objek
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
HardwareSerial sharedSerial(1);   
HardwareSerial gpsSerial(2);      
TinyGPSPlus gps;
OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);
Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
WiFiManager wifiManager;

// Command frames NPK
uint8_t Com[8] = { 0x01, 0x03, 0x00, 0x1E, 0x00, 0x01, 0xE4, 0x0C }; 
uint8_t Com1[8] = { 0x01, 0x03, 0x00, 0x1F, 0x00, 0x01, 0xB5, 0xCC }; 
uint8_t Com2[8] = { 0x01, 0x03, 0x00, 0x20, 0x00, 0x01, 0x85, 0xC0 }; 

// Command frame pH
uint8_t ComPH[8] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A };

// Variabel global NPK
int N, P, K;
int N_BASELINE = 0;
int P_BASELINE = 0;
int K_BASELINE = 0;

// Variabel global pH
int pHRaw = 0;              
float pHSensor = 0.0;       
float pHCalibrated = 0.0;   
bool phConnectionEstablished = false;
int phBaudRate = 9600; 

// Variabel GPS
float currentLatitude = 0.0;
float currentLongitude = 0.0;
int currentSatellites = 0;
bool gpsDataValid = false;

// Variabel sensor kelembapan tanah (Capacitive Soil Moisture Sensor v2.0)
int soilSensorRawValue = 0;
float moistureLevel = 0.0;
float moistureEMA = -1.0;
const float EMA_ALPHA = 0.2;
#define ADC_DRY  3583   // GANTI dengan raw ADC saat sensor di udara/kering
#define ADC_WET  1830   // GANTI dengan raw ADC saat sensor di air/basah

// Variabel WiFi
bool wifiConnected = false;
bool wifiManagerStarted = false;
unsigned long lastWiFiCheck = 0;
const unsigned long wifiCheckInterval = 1000;

// Menu system
enum MenuState {
  MAIN_MENU,
  SET_K_VALUE,      // Menu untuk mengatur nilai K
  AMBIL_DATA,
  CEK_SENSOR,
  DATA_REALTIME,
  UPLOAD_DATA 
};

MenuState currentMenu = MAIN_MENU;
int menuIndex = 0;
const int maxMenuItems = 3;

String menuItems[3] = {
  "1. Ambil Data",
  "2. Cek Sensor", 
  "3. Data Real Time"
};

// Button variables
bool btnUpPressed = false;
bool btnDownPressed = false;
bool btnBackPressed = false;
bool btnNextPressed = false;

// Mapping ID ke nama tanaman
const char* classNames[5] = {  // Nama tanaman sesuai urutan classID
  "Bawang Merah",  // ID: 0
  "Jagung",        // ID: 1
  "Kangkung",      // ID: 2
  "Kol",           // ID: 3
  "Padi"           // ID: 4
};

// Database tanaman
struct TrainingData {
  uint8_t classID;      // 1 byte untuk ID kelas (0-6)
  int16_t n;            // 2 bytes untuk Nitrogen
  int16_t p;            // 2 bytes untuk Phosphorus
  int16_t k;            // 2 bytes untuk Potassium
  uint8_t ph;           // 1 byte untuk pH (dikali 10)
  uint8_t moisture;     // 1 byte untuk kelembaban (0-100)
};


// Data training KNN (600 sampel) dipisah ke file terpisah agar .ino lebih ringkas
#include "training_data.h"


// Konversi pH terkompresi (uint8_t) ke nilai pH asli
float getpH(uint8_t phCompressed) {
  return phCompressed / 10.0;  // contoh: 72 menjadi 7.2
}

String getClassName(uint8_t classID) {
  if(classID < 5) {
    return String(classNames[classID]);
  }
  return "Unknown";
}

// Variables untuk ambil data
float readings_N[5];
float readings_P[5];
float readings_K[5];
float readings_pH[5];  
float readings_moisture[5];  
float readings_temp[5];  
float avg_temp = 0.0;    

// Variabel global GPS untuk tracking lokasi
float final_latitude = 0.0;   
float final_longitude = 0.0;  
int final_satellites = 0;     
bool gps_data_collected = false; 

int reading_count = 0;
float avg_N, avg_P, avg_K;
float avg_pH;          
float avg_moisture;

int kValue = 15;  
bool settingK = false;  

String best_plant = "";

// Firebase variables
bool firebaseReady = false;
bool uploadInProgress = false;
bool uploadSuccess = false;
String uploadStatus = "";
unsigned long uploadStartTime = 0;

float best_similarity = 0;

// Struct untuk menyimpan hasil jarak
struct DistanceResult {
  String className;
  float distance;
}; 

// Deklarasi forward untuk fungsi-fungsi KNN
void sortDistances(DistanceResult distances[], int size);
float calculateEuclideanDistance(float n1, float p1, float k1, float ph1, float m1, TrainingData &data);
void calculateNormalizationStats();
float normalizeValue(float value, float min, float max);
void calculatePlantRecommendation();

// Struct untuk menyimpan stats normalisasi
struct NormalizationStats {
  float nMin, nMax;
  float pMin, pMax;
  float kMin, kMax;
  float phMin, phMax;
  float mMin, mMax;
};

NormalizationStats normStats;

// Sensor status variables
bool npkSensorReady = false;
bool tempSensorReady = false;
bool gpsSensorReady = false;
bool moistureSensorReady = false;
bool phSensorReady = false;

bool sensorChecked = false;

// FreeRTOS Task Handles (untuk kontrol task)
TaskHandle_t taskMoistureHandle = NULL;
TaskHandle_t taskTempHandle = NULL;
TaskHandle_t taskGPSHandle = NULL;
TaskHandle_t taskNPKHandle = NULL;
TaskHandle_t taskPHHandle = NULL;
TaskHandle_t taskDisplayHandle = NULL;

// Mutex untuk proteksi data (agar tidak bentrok antar task)
SemaphoreHandle_t xMutexSerial = NULL;

// FreeRTOS Status Flag
bool freertosRunning = false;

// Nilai cache untuk ditampilkan di layar realtime
float cached_realN = 0, cached_realP = 0, cached_realK = 0;
float cached_pH = 0;
float cached_temp = 0;
float cached_moisture = 0;
bool cached_gpsValid = false;
int cached_satellites = 0;
float cached_lat = 0, cached_lng = 0;

// State untuk ambil data
bool ambil_data_finished = false;
bool currentSerialMode = 0; 
bool serialInUse = false;


// FUNGSI WIFI MANAGEMENT
void initWiFiManager() {
  // Setup LED untuk WiFi indicator
  led.begin();
  led.setBrightness(LED_BRIGHTNESS);
  setLED(255, 0, 0); // Merah - belum connect
  
  // Set default state
  wifiConnected = false;
  firebaseReady = false;  // reset status firebase
  
  // Konfigurasi WiFiManager untuk non-blocking mode
  wifiManager.setConfigPortalBlocking(false);
  wifiManager.setTimeout(0);  // Unlimited (non-blocking)
  
  // Set WiFi mode
  WiFi.mode(WIFI_STA);
  delay(100);
  
  // Cek apakah ada credentials WiFi tersimpan
  String ssid = WiFi.SSID();

  if (ssid.length() > 0) {
    WiFi.begin();
    
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
      delay(500);
      yield();

      // Tampilkan status connecting di OLED
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(20, 15);
      display.print("Connecting WiFi");
      display.setCursor(25, 28);
      display.print(ssid);
      // Progress dots animasi
      int dots = ((millis() - startAttempt) / 500) % 4;
      display.setCursor(20, 42);
      for(int i = 0; i < dots; i++) display.print(".");
      display.display();
    }
    if (WiFi.status() == WL_CONNECTED) {
      // Berhasil connect
      wifiConnected = true;
      setLED(0, 255, 0); // Hijau
      
      delay(500); // Stabilisasi koneksi
      initFirebase();
    } else {
      // Gagal connect meski ada credentials - tidak masalah, lanjut ke menu
      wifiConnected = false;
      setLED(255, 0, 0); // Merah
      firebaseReady = false;
    }
  } else {
    // Tidak ada WiFi tersimpan, langsung skip ke menu
    wifiConnected = false;
    setLED(255, 0, 0); // Merah
    firebaseReady = false;
  }
  
  // Start config portal (non-blocking) untuk setup manual nanti
  wifiManager.autoConnect("AGROSENSE-Setup", "12345678");
  wifiManagerStarted = true;
}

void updateWiFiStatus() {
  // Rate limiting - cek setiap 1 detik
  if (millis() - lastWiFiCheck < wifiCheckInterval) {
    return;
  }
  lastWiFiCheck = millis();
  
  // Process WiFi Manager (non-blocking)
  if (wifiManagerStarted) {
    wifiManager.process();
  }
  
  // Baca status WiFi actual
  bool currentWiFiStatus = (WiFi.status() == WL_CONNECTED);
  
  // Update HANYA jika ada perubahan status
  if (currentWiFiStatus != wifiConnected) {
    wifiConnected = currentWiFiStatus;
    
    if (wifiConnected) {
      // WiFi baru connect
      setLED(0, 255, 0); // Hijau
      
      // Init Firebase hanya jika belum ready dan tidak sedang upload
      if (!firebaseReady && !uploadInProgress) {
        delay(500); // Stabilisasi koneksi
        initFirebase();
      }
    } else {
      // WiFi disconnect
      setLED(255, 0, 0); // Merah
      firebaseReady = false;  // reset status firebase
    }
  }
}

void setLED(uint8_t red, uint8_t green, uint8_t blue) {
  led.setPixelColor(0, led.Color(red, green, blue));
  led.show();
}

void handleWiFiReset() {
  static unsigned long buttonPressed = 0;
  static bool buttonState = false;
  
  if (digitalRead(0) == LOW) {
    if (!buttonState) {
      buttonPressed = millis();
      buttonState = true;
    }
    
    if (millis() - buttonPressed > 3000) {
      Serial.println("Resetting WiFi Settings...");
      setLED(255, 165, 0); // Orange

      // Tampilkan info di OLED
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(15, 20);
      display.print("Mereset WiFi...");
      display.setCursor(10, 35);
      display.print("Harap tunggu...");
      display.display();

      wifiManager.resetSettings();
      WiFi.disconnect(true);  // putus koneksi aktif
      WiFi.mode(WIFI_OFF);    // matikan WiFi
      firebaseReady = false;
      wifiConnected = false;
      
      delay(1000);
      Serial.println("WiFi reset. Restarting...");
      ESP.restart();          // restart bersih
    }
  } else {
    buttonState = false;
  }
}

void initFirebase() {
  // Validasi WiFi benar-benar connected
  if (WiFi.status() != WL_CONNECTED) {
    firebaseReady = false;
    return;  // Exit langsung jika tidak connected
  }
  
  // Validasi free heap untuk hindari out of memory
  if (ESP.getFreeHeap() < 30000) {
    firebaseReady = false;
    return;  // Exit jika memori kurang dari 30KB
  }
  
  // Assign the api key
  config.api_key = API_KEY;
  
  // Assign the user sign in credentials
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  
  // Assign the RTDB URL
  config.database_url = DATABASE_URL;
  
  // Assign the callback function for the long running token generation task
  config.token_status_callback = tokenStatusCallback;
  
  // Begin Firebase
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  // Wait for authentication dengan timeout 10 detik
  unsigned long authStart = millis();
  while (!Firebase.ready() && millis() - authStart < 10000) {
    delay(100);
    yield();  // Feed watchdog
    
    // Cek jika WiFi disconnect saat proses auth
    if (WiFi.status() != WL_CONNECTED) {
      firebaseReady = false;
      return;
    }
  }
  
  if (Firebase.ready()) {
    firebaseReady = true;
  } else {
    firebaseReady = false;
  }
}

void uploadDataToFirebase() {
  if (!firebaseReady || !wifiConnected) {
    uploadStatus = "WiFi/Firebase not ready";
    uploadSuccess = false;
    return;
  }
  
  uploadInProgress = true;
  uploadStartTime = millis();
  uploadStatus = "Uploading...";
  
  // Create timestamp
  String timestamp = String(millis());
  String path = "/sensor_data/" + timestamp;
  
  // Create JSON object
  FirebaseJson json;
  json.set("nitrogen", avg_N);
  json.set("phosphorus", avg_P);
  json.set("potassium", avg_K);
  json.set("ph", avg_pH);
  json.set("moisture", avg_moisture);
  json.set("temperature", avg_temp);
  json.set("recommended_plant", best_plant);
  json.set("similarity_score", best_similarity);

  // Sertakan data GPS jika tersedia
  json.set("satellites", final_satellites);
  if (gps_data_collected && final_latitude != 0.0) {
      json.set("latitude", final_latitude);
      json.set("longitude", final_longitude);
  } else {
      json.set("latitude", "0.0");
      json.set("longitude", "0.0");
  }
  
  // Add timestamp
  json.set("timestamp", timestamp);
  json.set("device_id", "ESP32-AgroSense");
  
  // Upload to Firebase
  if (Firebase.RTDB.setJSON(&fbdo, path, &json)) {
    uploadSuccess = true;
    uploadStatus = "Upload Success!";
    Serial.println("Data uploaded successfully");
    Serial.println("Path: " + path);
  } else {
    uploadSuccess = false;
    uploadStatus = "Upload Failed";
    Serial.println("Upload failed: " + fbdo.errorReason());
  }
  
  uploadInProgress = false;
}

// FUNGSI SERIAL MANAGEMENT
void setupSerialForNPK() {
  // Skip jika sudah di mode NPK
  if(serialInUse && currentSerialMode == 0) {
    return;
  }

  // Matikan serial yang sedang aktif sebelum ganti mode
  if(serialInUse) {
    sharedSerial.end();
    delay(100);
  }

  // Isolasi pin pH ke Hi-Z supaya tidak mengganggu bus RS485
  pinMode(PH_TX_PIN, INPUT);
  pinMode(PH_RX_PIN, INPUT);
  delay(100);  // waktu discharge

  // Setup serial untuk NPK
  sharedSerial.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  delay(150);  // stabilisasi tambahan

  // Bersihkan buffer serial
  for(int i = 0; i < 5; i++) {
    while(sharedSerial.available()) {
      sharedSerial.read();
    }
    delay(20);
  }

  // Dummy write untuk memastikan line siap
  sharedSerial.write(0x00);
  sharedSerial.flush();
  delay(50);

  // Clear lagi setelah dummy write
  while(sharedSerial.available()) {
    sharedSerial.read();
  }

  currentSerialMode = 0;
  serialInUse = true;
}

void disableAllSerial() {
  if(serialInUse) {
    sharedSerial.end();
    delay(100);  // discharge penuh

    // Set pin TX ke Hi-Z, jangan OUTPUT LOW (bisa ganggu sensor lain di bus)
    pinMode(RS485_TX_PIN, INPUT);
    pinMode(PH_TX_PIN, INPUT);

    // Pin RX juga ke input
    pinMode(RS485_RX_PIN, INPUT);
    pinMode(PH_RX_PIN, INPUT);

    delay(100);  // waktu discharge kapasitor

    serialInUse = false;
    currentSerialMode = -1;
  }
}

void setupSerialForPH() {
  // Skip jika sudah di mode pH
  if(serialInUse && currentSerialMode == 1) {
    return;
  }

  // Matikan serial yang sedang aktif sebelum ganti mode
  if(serialInUse) {
    sharedSerial.end();
    delay(100);
  }

  // Isolasi pin NPK ke Hi-Z supaya tidak mengganggu pembacaan pH
  pinMode(RS485_TX_PIN, INPUT);  
  pinMode(RS485_RX_PIN, INPUT);  
  delay(100);

  // Setup serial untuk pH
  sharedSerial.begin(phBaudRate, SERIAL_8N1, PH_RX_PIN, PH_TX_PIN);
  delay(150);

  // Bersihkan buffer serial
  for(int i = 0; i < 5; i++) {
    while(sharedSerial.available()) {
      sharedSerial.read();
    }
    delay(20);
  }

  // Dummy write untuk memastikan line siap
  sharedSerial.write(0x00);
  sharedSerial.flush();
  delay(50);
  
  while(sharedSerial.available()) {
    sharedSerial.read();
  }
  
  currentSerialMode = 1;
}

// TASK 1: Baca Kelembapan Tanah (PRIORITAS TERTINGGI - PALING CEPAT!)
void taskReadMoisture(void *parameter) {
  Serial.println("[RTOS] Moisture task started");
  static float emaValue = -1.0;

  for(;;) {
    const int NUM_SAMPLES = 15;
    long sum = 0;
    for(int i = 0; i < NUM_SAMPLES; i++) {
      sum += analogRead(SOIL_SENSOR_PIN);
      vTaskDelay(2 / portTICK_PERIOD_MS);
    }
    int rawValue = sum / NUM_SAMPLES;

    float moisture = (float)(ADC_DRY - rawValue) * 100.0 / (float)(ADC_DRY - ADC_WET);
    if (moisture < 0.0) moisture = 0.0;
    if (moisture > 100.0) moisture = 100.0;

    emaValue = (emaValue < 0) ? moisture : (EMA_ALPHA * moisture) + ((1 - EMA_ALPHA) * emaValue);
    cached_moisture = emaValue;

    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

// TASK 2: Baca Suhu (PRIORITAS TINGGI)
void taskReadTemp(void *parameter) {
  Serial.println("[RTOS] Temperature task started");
  
  for(;;) {
    tempSensor.requestTemperatures();
    float temp = tempSensor.getTempCByIndex(0);
    
    if(temp != DEVICE_DISCONNECTED_C) {
      cached_temp = temp;
    }
    
    // Update setiap 500ms
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

// TASK 3: Baca GPS (PRIORITAS MENENGAH)
void taskReadGPS(void *parameter) {
  Serial.println("[RTOS] GPS task started");
  
  for(;;) {
    // Non-blocking GPS read
    if(gpsSerial.available() > 0) {
      char c = gpsSerial.read();
      if (gps.encode(c)) {
        if (gps.location.isValid()) {
          cached_lat = gps.location.lat();
          cached_lng = gps.location.lng();
          cached_gpsValid = true;
        }
        if (gps.satellites.isValid()) {
          cached_satellites = gps.satellites.value();
        }
      }
    }
    
    // Update setiap 200ms
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

// TASK 4: Baca NPK (PRIORITAS RENDAH - butuh serial mutex)
void taskReadNPK(void *parameter) {
  Serial.println("[RTOS] NPK task started");
  
  for(;;) {
    if(npkSensorReady) {
      // Tunggu serial mutex tersedia (max 100ms)
      if(xSemaphoreTake(xMutexSerial, pdMS_TO_TICKS(100)) == pdTRUE) {
        
        // Baca NPK
        int rawN = readN();
        int rawP = readP();
        int rawK = readK();
        
        // Release mutex
        xSemaphoreGive(xMutexSerial);
        
        // Update cached values
        cached_realN = calibrateN(rawN);
        cached_realP = calibrateP(rawP);
        cached_realK = calibrateK(rawK);
      }
    }
    
    // Update setiap 2 detik (NPK lambat, tidak masalah)
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

// TASK 5: Baca pH (PRIORITAS RENDAH - butuh serial mutex)
void taskReadPH(void *parameter) {
  Serial.println("[RTOS] pH task started");
  
  for(;;) {
    if(phSensorReady) {
      // Tunggu serial mutex tersedia (max 100ms)
      if(xSemaphoreTake(xMutexSerial, pdMS_TO_TICKS(100)) == pdTRUE) {
        
        // Baca pH
        readCalibratedPH();
        cached_pH = pHCalibrated;
        
        // Release mutex
        xSemaphoreGive(xMutexSerial);
      }
    }
    
    // Update setiap 2 detik
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

// TASK 6: Update Display (PRIORITAS TINGGI)
void taskUpdateDisplay(void *parameter) {
  Serial.println("[RTOS] Display task started");
  
  for(;;) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // Header dengan WiFi status
    display.setCursor(15, 0);
    display.print("DATA REAL TIME");
    display.setCursor(105, 0);
    if(wifiConnected) {
      display.print("W+");
    } else {
      display.print("W-");
    }
    display.drawLine(0, 8, 128, 8, SSD1306_WHITE);
    
    // NPK Data (from cache)
    display.setCursor(0, 12);
    if (!npkSensorReady) {
      display.print("NPK: Init diperlukan");
    } else {
      display.print("N:");
      display.print(cached_realN, 0);
      display.print(" P:");
      display.print(cached_realP, 0);
      display.print(" K:");
      display.print(cached_realK, 0);
    }
    
    // pH Data (from cache)
    display.setCursor(0, 20);
    display.print("pH: ");
    if(cached_pH > 0 && phSensorReady) {
      display.print(cached_pH, 2);
      if (cached_pH <= 5.5) {
        display.print(" (Asam)");
      } else if (cached_pH > 8.0) {
        display.print(" (Basa)");
      } else {
        display.print(" (Netral)");
      }
    } else {
      display.print("Init diperlukan");
    }
    
    // Temperature & Moisture (from cache)
    display.setCursor(0, 28);
    display.print("Suhu: ");
    display.print(cached_temp, 1);
    display.println(" C");
    
    display.setCursor(0, 36);
    display.print("Kelembapan: ");
    display.print(cached_moisture, 1); 
    
    // GPS Data (from cache)
    display.setCursor(0, 44);
    display.print("GPS: ");
    if(cached_gpsValid) {
      display.print("OK (");
      display.print(cached_satellites);
      display.print(" sat)");
    } else {
      display.print("No Fix");
    }
    
    // Location atau WiFi info
    if(cached_gpsValid) {
      display.setCursor(0, 52);
      display.print("Lat:");
      display.print(cached_lat, 4);
      display.setCursor(0, 60);
      display.print("Lng:");
      display.print(cached_lng, 4);
    } else {
      display.setCursor(0, 52);
      if(wifiConnected) {
        display.print("WiFi: ");
        display.println(WiFi.localIP());
      } else {
        display.println("WiFi: Disconnected");
      }
      display.setCursor(0, 60);
      display.println("BACK: Kembali");
    }
    
    display.display();
    
    // Update display setiap 200ms (5x per detik = smooth!)
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

// FUNGSI: Start FreeRTOS Tasks (dipanggil saat masuk menu Data Real Time)
void startRealtimeTasks() {
  if(freertosRunning) {
    Serial.println("[RTOS] Tasks already running");
    return;
  }
  
  Serial.println("[RTOS] Starting tasks...");
  
  // Buat mutex untuk serial protection
  xMutexSerial = xSemaphoreCreateMutex();
  
  // Buat semua tasks dengan prioritas berbeda
  // Prioritas: 1=lowest, 5=highest
  
  xTaskCreatePinnedToCore(
    taskReadMoisture,      // Fungsi task
    "TaskMoisture",        // Nama task (untuk debug)
    8192,                  // Stack size (bytes)
    NULL,                  // Parameter
    4,                     // Prioritas (4 = TINGGI)
    &taskMoistureHandle,   // Handle
    0                      // Core 0 (ESP32 punya 2 core)
  );
  
  xTaskCreatePinnedToCore(
    taskReadTemp,
    "TaskTemp",
    2048,
    NULL,
    3,                     // Prioritas sedang
    &taskTempHandle,
    0
  );
  
  xTaskCreatePinnedToCore(
    taskReadGPS,
    "TaskGPS",
    2048,
    NULL,
    3,
    &taskGPSHandle,
    0
  );
  
  xTaskCreatePinnedToCore(
    taskReadNPK,
    "TaskNPK",
    8192,                  
    NULL,
    2,                     
    &taskNPKHandle,
    1                      
  );
  
  xTaskCreatePinnedToCore(
    taskReadPH,
    "TaskPH",
    8192,
    NULL,
    2,
    &taskPHHandle,
    1
  );
  
  xTaskCreatePinnedToCore(
    taskUpdateDisplay,
    "TaskDisplay",
    8192,
    NULL,
    3,
    &taskDisplayHandle,
    0
  );
  
  freertosRunning = true;
  Serial.println("[RTOS] All tasks started successfully!");
}

// FUNGSI: Stop FreeRTOS Tasks (dipanggil saat keluar menu Data Real Time)
void stopRealtimeTasks() {
  if(!freertosRunning) {
    Serial.println("[RTOS] No tasks running");
    return;
  }
  
  Serial.println("[RTOS] Stopping tasks...");
  
  // Delete semua tasks
  if(taskMoistureHandle != NULL) {
    vTaskDelete(taskMoistureHandle);
    taskMoistureHandle = NULL;
  }
  
  if(taskTempHandle != NULL) {
    vTaskDelete(taskTempHandle);
    taskTempHandle = NULL;
  }
  
  if(taskGPSHandle != NULL) {
    vTaskDelete(taskGPSHandle);
    taskGPSHandle = NULL;
  }
  
  if(taskNPKHandle != NULL) {
    vTaskDelete(taskNPKHandle);
    taskNPKHandle = NULL;
  }
  
  if(taskPHHandle != NULL) {
    vTaskDelete(taskPHHandle);
    taskPHHandle = NULL;
  }
  
  if(taskDisplayHandle != NULL) {
    vTaskDelete(taskDisplayHandle);
    taskDisplayHandle = NULL;
  }
  
  // Delete mutex
  if(xMutexSerial != NULL) {
    vSemaphoreDelete(xMutexSerial);
    xMutexSerial = NULL;
  }
  
  freertosRunning = false;
  Serial.println("[RTOS] All tasks stopped");
}

void setup() {
  Serial.begin(9600);
  
  // Setup button pins
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(0, INPUT_PULLUP); 
  
  // Setup soil moisture sensor pin
  pinMode(SOIL_SENSOR_PIN, INPUT);
  
  // Konfigurasi ADC untuk ESP32-S3
  analogReadResolution(12);        
  analogSetAttenuation(ADC_11db);  
  
  // Inisialisasi I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  
  // Inisialisasi OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
    // Welcome screen
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  // Frame luar ganda
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.drawRect(2, 2, 124, 60, SSD1306_WHITE);
  
  // Logo icon daun sederhana di atas
  display.fillCircle(64, 10, 3, SSD1306_WHITE);
  display.drawLine(64, 13, 64, 18, SSD1306_WHITE);
  display.drawLine(64, 15, 60, 17, SSD1306_WHITE);
  display.drawLine(64, 15, 68, 17, SSD1306_WHITE);
  
  // Judul utama AGROSENSE (text size 2, lebih besar)
  display.setTextSize(2);
  display.setCursor(12, 20);
  display.println("AGROSENSE");
  
  // Plus symbol
  display.setTextSize(1);
  display.setCursor(108, 22);
  display.println("+");
  
  // Subtitle
  display.setTextSize(1);
  display.setCursor(18, 38);
  display.println("Smart Crop Match");
  
  // Tagline bawah
  display.setCursor(20, 50);
  display.println("Empowering Agri");
  
  display.display();
  delay(3000);
  
  // Animasi loading
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(30, 26);
  display.println("Initializing");
  
  // Progress bar animasi
  for(int i = 0; i < 96; i += 8) {
    display.fillRect(16 + i, 38, 6, 4, SSD1306_WHITE);
    display.display();
    delay(100);
  }
  delay(500);
  // Inisialisasi WiFi Manager (non-blocking)
  initWiFiManager();
  
  // Inisialisasi sensors
  Serial.println("Initializing Shared Serial for NPK & pH sensor...");
  setupSerialForNPK(); 
  
  Serial.println("Initializing pH sensor with calibration...");
  testPhBaudRates(); 
  
  disableAllSerial();  
  delay(200);
  
  // Reset state flags
  currentSerialMode = -1;  
  serialInUse = false; 

  Serial.println("Initializing GPS on Serial2...");
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  
  tempSensor.begin();
  
  Serial.println("NPK Smart Farming System Started with WiFi");
  Serial.println("WiFi Portal: ESP32-NPK-Setup / 12345678");
  
  // Reset menu
  currentMenu = MAIN_MENU;
  menuIndex = 0;

  delay(500);  // Beri waktu sistem stabil
  yield();     // Feed watchdog
}

void loop() {
  // Update WiFi status di background
  updateWiFiStatus();
  
  // Handle WiFi reset button
  handleWiFiReset();
  
  // Handle navigasi buttons
  handleButtons();
  
  // Run menu yang aktif
  switch(currentMenu) {
  case MAIN_MENU:
    displayMainMenu();
    break;
  case SET_K_VALUE:         
    runSetKValue();         
    break;                 
  case AMBIL_DATA:
    runAmbilData();
    break;
  case CEK_SENSOR:
    runCekSensor();
    break;
  case DATA_REALTIME:
    runDataRealtime();
    break;
  case UPLOAD_DATA:
    runUploadData();
    break;
}
  
  delay(100);
}

void handleButtons() {
  // Read button states (active LOW karena pull-up)
  bool currentUp = !digitalRead(BTN_UP);
  bool currentDown = !digitalRead(BTN_DOWN);
  bool currentBack = !digitalRead(BTN_BACK);
  bool currentNext = !digitalRead(BTN_NEXT);
  
  // Detect button press (edge detection)
  if(currentUp && !btnUpPressed) {
    btnUpPressed = true;
    handleUpButton();
  } else if(!currentUp) {
    btnUpPressed = false;
  }
  
  if(currentDown && !btnDownPressed) {
    btnDownPressed = true;
    handleDownButton();
  } else if(!currentDown) {
    btnDownPressed = false;
  }
  
  if(currentBack && !btnBackPressed) {
    btnBackPressed = true;
    handleBackButton();
  } else if(!currentBack) {
    btnBackPressed = false;
  }
  
  if(currentNext && !btnNextPressed) {
    btnNextPressed = true;
    handleNextButton();
  } else if(!currentNext) {
    btnNextPressed = false;
  }
}

void handleUpButton() {
  if(currentMenu == MAIN_MENU) {
    menuIndex--;
    if(menuIndex < 0) menuIndex = maxMenuItems - 1;
  }
  else if(currentMenu == SET_K_VALUE) {
    kValue++;
    if(kValue > 20) kValue = 20;  // Max K=20
  }
  else if(currentMenu == CEK_SENSOR) {
    menuIndex--;
    if(menuIndex < 0) menuIndex = 0;
  }
  else if(currentMenu == AMBIL_DATA && ambil_data_finished) {
    menuIndex--;
    if(menuIndex < 0) menuIndex = 0;
  }
}

void handleDownButton() {
  if(currentMenu == MAIN_MENU) {
    menuIndex++;
    if(menuIndex >= maxMenuItems) menuIndex = 0;
  }
  else if(currentMenu == SET_K_VALUE) {
    kValue--;
    if(kValue < 1) kValue = 1;  // Min K=1
  }
  else if(currentMenu == CEK_SENSOR) {
    menuIndex++;
    if(menuIndex > 2) menuIndex = 2;
  }
  else if(currentMenu == AMBIL_DATA && ambil_data_finished) {
    menuIndex++;
    if(menuIndex > 5) menuIndex = 5;
  }
}

void handleBackButton() {
  if(currentMenu == SET_K_VALUE) {
    currentMenu = MAIN_MENU;
    settingK = false;
    menuIndex = 0;
  }
  else if(currentMenu != MAIN_MENU) {
    
    // Stop FreeRTOS tasks jika keluar dari menu Data Real Time
    if(currentMenu == DATA_REALTIME && freertosRunning) {
      stopRealtimeTasks();
    }
    
    // Kode asli Anda (TETAP ada)
    currentMenu = MAIN_MENU;
    reading_count = 0;
    ambil_data_finished = false;
    gps_data_collected = false;
    uploadInProgress = false;
    uploadSuccess = false;
    uploadStatus = "";
    sensorChecked = false;
    menuIndex = 0;
    disableAllSerial();
  }
}


void handleNextButton() {
  if(currentMenu == MAIN_MENU) {
    switch(menuIndex) {
      case 0:
        currentMenu = SET_K_VALUE;  // UBAH KE SET_K_VALUE
        settingK = true;
        break;
      case 1:
        currentMenu = CEK_SENSOR;
        break;
      case 2:
        currentMenu = DATA_REALTIME;
        break;
    }
  }
  else if(currentMenu == SET_K_VALUE) {
    // Konfirmasi K, mulai ambil data
    currentMenu = AMBIL_DATA;
    reading_count = 0;
    ambil_data_finished = false;
    gps_data_collected = false;
    settingK = false;
  }
  else if (currentMenu == AMBIL_DATA && ambil_data_finished) {
    currentMenu = UPLOAD_DATA;
    uploadDataToFirebase();
  }
}

void displayMainMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Header dengan WiFi status
  display.drawRoundRect(0, 0, 128, 16, 3, SSD1306_WHITE);
  display.setCursor(12, 5);
  display.print("MENU UTAMA");
  
  // WiFi status indicator di header
  display.setCursor(95, 5);
  if(wifiConnected) {
    display.print("WiFi");
  } else {
    display.print("NoWF");
  }
  
  // Garis pemisah
  display.drawLine(0, 17, 128, 17, SSD1306_WHITE);
  
  // Menu items
  for(int i = 0; i < maxMenuItems; i++) {
    int y_pos = 20 + (i * 12);
    
    if(i == menuIndex) {
      display.fillRoundRect(2, y_pos-1, 124, 10, 2, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(5, y_pos); 
    } else {
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(5, y_pos); 
    }
    
    display.print(i + 1);
    display.print(". ");
    
    String menuText = menuItems[i];
    int dotPosition = menuText.indexOf('.');
    if (dotPosition != -1) {
      menuText = menuText.substring(dotPosition + 2); 
    }
    display.print(menuText);
    
    display.setTextColor(SSD1306_WHITE);
  }
  
  // Footer dengan instruksi navigasi
  display.drawLine(0, 54, 128, 54, SSD1306_WHITE);
  display.setCursor(5, 56);
  display.print("^v: Nav  OK: Pilih");
  
  display.display();
}

void runSetKValue() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Header
  display.drawRoundRect(0, 0, 128, 16, 3, SSD1306_WHITE);
  display.setCursor(20, 5);
  display.print("SET NILAI K");
  
  // WiFi status
  display.setCursor(105, 5);
  if(wifiConnected) {
    display.print("W+");
  } else {
    display.print("W-");
  }
  
  display.drawLine(0, 17, 128, 17, SSD1306_WHITE);
  
  // Display K value (centered, large)
  display.setTextSize(3);
  display.setCursor(48, 28);
  display.print("K=");
  display.print(kValue);
  
  // Footer
  display.setTextSize(1);
  display.drawLine(0, 54, 128, 54, SSD1306_WHITE);
  display.setCursor(10, 56);
  display.print("^v:Ubah NEXT:OK");
  
  display.display();
}

void runAmbilData() {
  if(!ambil_data_finished) {
    if(reading_count < 5) {
      // Tampilkan status pengambilan data
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("Mengambil Data...");
      display.setCursor(0, 15);
      display.print("Sample: ");
      display.print(reading_count + 1);
      display.print("/5");
      
      // WiFi status di pojok
      display.setCursor(90, 0);
      if(wifiConnected) {
        display.print("WiFi:ON");
      } else {
        display.print("WiFi:OFF");
      }
      
      display.display();
      
      // Baca nilai NPK
      int rawN = readN();
      int rawP = readP();
      int rawK = readK();
      
      // Kalibrasi NPK
      float calibratedN = calibrateN(rawN);
      float calibratedP = calibrateP(rawP);
      float calibratedK = calibrateK(rawK);
      
      // Baca nilai pH
      readCalibratedPH();
      float calibratedpH = pHCalibrated;
      
      // Baca nilai kelembapan
      readSoilMoisture();
      float currentMoisture = moistureLevel;
      
      // Baca nilai suhu
      tempSensor.requestTemperatures();
      float currentTemp = tempSensor.getTempCByIndex(0);
      if(currentTemp == DEVICE_DISCONNECTED_C) {
        currentTemp = 0.0; 
      }
      
      // Ambil GPS data hanya pada sample pertama
      if(reading_count == 0 && !gps_data_collected) {
        unsigned long gpsStartTime = millis();
        
        while(millis() - gpsStartTime < 10000 && !gps_data_collected) {
          while (gpsSerial.available() > 0) {
            char c = gpsSerial.read();
            if (gps.encode(c)) {
              if (gps.location.isValid()) {
                final_latitude = gps.location.lat();
                final_longitude = gps.location.lng();
                gps_data_collected = true;
              }
              if (gps.satellites.isValid()) {
                final_satellites = gps.satellites.value();
              }
            }
          }
        }
        
        if(!gps_data_collected) {
          final_latitude = 0.0;
          final_longitude = 0.0;
          final_satellites = 0;
          gps_data_collected = true; 
        }
      }
      
      // Simpan pembacaan
      readings_N[reading_count] = calibratedN;
      readings_P[reading_count] = calibratedP;
      readings_K[reading_count] = calibratedK;
      readings_pH[reading_count] = calibratedpH;
      readings_moisture[reading_count] = currentMoisture;
      readings_temp[reading_count] = currentTemp; 
      
      // Tampilkan data sementara di OLED
      display.setCursor(0, 25);
      display.print("N:");
      display.print(calibratedN, 0);
      display.print(" P:");
      display.print(calibratedP, 0);
      display.setCursor(0, 33);
      display.print("K:");
      display.print(calibratedK, 0);
      display.print(" pH:");
      display.print(calibratedpH, 1);
      display.setCursor(0, 41);
      display.print("M:");
      display.print(currentMoisture, 1);
      display.print("%");
      display.print(" T:");
      display.print(currentTemp, 1);
      
      display.display();
      
      reading_count++;
      delay(2000); 
      
    } else {
      // Hitung rata-rata
      avg_N = 0; avg_P = 0; avg_K = 0; avg_pH = 0; avg_moisture = 0; avg_temp = 0; 
      for(int i = 0; i < 5; i++) {
        avg_N += readings_N[i];
        avg_P += readings_P[i];
        avg_K += readings_K[i];
        avg_pH += readings_pH[i];
        avg_moisture += readings_moisture[i];
        avg_temp += readings_temp[i]; 
      }
      avg_N /= 5.0;
      avg_P /= 5.0;
      avg_K /= 5.0;
      avg_pH /= 5.0;
      avg_moisture /= 5.0;
      avg_temp /= 5.0; 
      
      // Hitung rekomendasi
      calculatePlantRecommendation();
      
      // Set flag bahwa pengambilan data sudah selesai
      ambil_data_finished = true;
    }
  } else {
    // Tampilkan hasil final
    displayResults();
  }
}

void displayResults() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Header
  display.drawRoundRect(0, 0, 128, 12, 3, SSD1306_WHITE);
  display.setCursor(8, 3);
  display.print("HASIL ANALISIS");
  
  // WiFi status
  display.setCursor(105, 3);
  if(wifiConnected) {
    display.print("W+");
  } else {
    display.print("W-");
  }
  
  display.drawLine(0, 13, 128, 13, SSD1306_WHITE);
  
  // Konten (scrollable)
  String contentItems[8];
  
  // Item 0: NPK
  contentItems[0] = "N:" + String(avg_N, 0) + " P:" + String(avg_P, 0) + " K:" + String(avg_K, 0);
  
  // Item 1: pH, Moisture
  contentItems[1] = "pH:" + String(avg_pH, 1) + " M:" + String(avg_moisture, 1) + "%";
  
  // Item 2: Temperature
  contentItems[2] = "Temp: " + String(avg_temp, 1) + "C";
  
  // Item 3: Label Rekomendasi
  contentItems[3] = "Rekomendasi:";
  
  // Item 4: Nama tanaman + score
  contentItems[4] = best_plant + " (" + String(best_similarity, 0) + "%)";
  
  // Item 5: Satelit
  contentItems[5] = "Satelit: " + String(final_satellites);
  
  // Item 6: Latitude
  if(gps_data_collected && final_latitude != 0.0) {
    contentItems[6] = "Lat: " + String(final_latitude, 4);
  } else {
    contentItems[6] = "Lat: No data";
  }
  
  // Item 7: Longitude
  if(gps_data_collected && final_longitude != 0.0) {
    contentItems[7] = "Lng: " + String(final_longitude, 4);
  } else {
    contentItems[7] = "Lng: No data";
  }
  
  // Tampilkan 3 item sesuai menuIndex
  for(int i = 0; i < 3; i++) {
    int itemIndex = menuIndex + i;
    if(itemIndex < 8) {
      int y_pos = 17 + (i * 12);
      display.setCursor(3, y_pos);
      display.print(contentItems[itemIndex]);
    }
  }
  
  // Indikator scroll
  if(menuIndex > 0) {
    display.setCursor(120, 17);
    display.print("^");
  }
  
  if(menuIndex < 5) {
    display.setCursor(120, 38);
    display.print("v");
  }
  
  // Footer
  display.drawLine(0, 53, 128, 53, SSD1306_WHITE);
  display.setCursor(2, 56);
  display.print("OK:Upload BACK:Menu");
  
  display.display();
}

void runCekSensor() {
  // CEK SENSOR HANYA SEKALI saat pertama masuk menu
  if(!sensorChecked) {
    // Tampilkan loading screen
    disableAllSerial();
    delay(200); 
    currentSerialMode = -1;
    serialInUse = false;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(15, 25);
    display.print("Checking sensors...");
    display.display();
    
    // Jalankan pengecekan
    checkAllSensors();
    //JIKA EROR HILANGKAN INI 
    disableAllSerial();
    delay(200);

    sensorChecked = true;
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Header (Fixed) - lebih tinggi untuk tidak nabrak
  display.drawRoundRect(0, 0, 128, 12, 3, SSD1306_WHITE);
  display.setCursor(8, 3);
  display.print("STATUS SENSOR");
  display.setCursor(105, 3);
  if(wifiConnected) {
    display.print("W+");
  } else {
    display.print("W-");
  }
  display.drawLine(0, 13, 128, 13, SSD1306_WHITE);
  
  // Array sensor
  String sensorNames[6] = {"NPK", "pH", "TEMP", "MOIST", "GPS", "WiFi"};
  String sensorStatus[6];
  
  // Status NPK
  if(npkSensorReady) {
    sensorStatus[0] = "READY";
  } else {
    sensorStatus[0] = "NOT READY";
  }
  
  // Status pH
  if(phSensorReady) {
    sensorStatus[1] = "READY (" + String(phBaudRate) + ")";
  } else {
    sensorStatus[1] = "NOT READY";
  }
  
  // Status TEMP
  if(tempSensorReady) {
    sensorStatus[2] = "READY";
  } else {
    sensorStatus[2] = "NOT READY";
  }
  
  // Status MOISTURE
  if(moistureSensorReady) {
    sensorStatus[3] = "READY";
  } else {
    sensorStatus[3] = "NOT READY";
  }
  
  // Status GPS
  if(gpsSensorReady) {
    sensorStatus[4] = "OK (" + String(currentSatellites) + ")";
  } else {
    sensorStatus[4] = "NOT READY";
  }
  
  // Status WiFi
  if(wifiConnected) {
    sensorStatus[5] = "CONNECTED";
  } else {
    sensorStatus[5] = "NO CONNECT";
  }
  
  // Tampilkan hanya 3 item dengan scroll
  for(int i = 0; i < 3; i++) {
    int sensorIndex = menuIndex + i;
    if(sensorIndex < 6) {
      int y_pos = 17 + (i * 12);
      display.setCursor(3, y_pos);
      display.print(sensorNames[sensorIndex]);
      display.print(": ");
      display.print(sensorStatus[sensorIndex]);
    }
  }
  
  // Indikator scroll
  if(menuIndex > 0) {
    display.setCursor(120, 17);
    display.print("^");
  }
  if(menuIndex < 3) {
    display.setCursor(120, 38);
    display.print("v");
  }
  
  // Footer (Fixed) - aman tidak nabrak
  display.drawLine(0, 53, 128, 53, SSD1306_WHITE);
  display.setCursor(5, 56);
  display.print("^v:Nav BACK:Menu");
  
  display.display();
}

void runDataRealtime() {
  // Start FreeRTOS tasks saat pertama masuk menu
  if(!freertosRunning) {
    startRealtimeTasks();
  }
  delay(50);
}

void runUploadData() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Header
  display.drawRoundRect(0, 0, 128, 12, 3, SSD1306_WHITE);
  display.setCursor(20, 3);
  display.print("UPLOAD DATA");
  display.setCursor(105, 3);
  if(wifiConnected) {
    display.print("W+");
  } else {
    display.print("W-");
  }
  display.drawLine(0, 13, 128, 13, SSD1306_WHITE);
  
  // Konten (scrollable)
  if (uploadInProgress) {
    // Saat sedang upload - LOCK scroll
    display.setCursor(3, 17);
    display.println("Firebase: ");
    display.setCursor(3, 25);
    if(firebaseReady) {
      display.println("Ready");
    } else {
      display.println("Not Ready");
    }
    
    display.setCursor(3, 33);
    display.println("Status:");
    display.setCursor(3, 41);
    display.println("Uploading...");
    
    // Timeout check
    if (millis() - uploadStartTime > 15000) {
      uploadInProgress = false;
      uploadSuccess = false;
      uploadStatus = "Timeout";
    }
  } else {
    // Setelah upload selesai - ENABLE scroll
    String contentItems[6];
    
    // Item 0: Firebase status
    contentItems[0] = "Firebase: ";
    if(firebaseReady) {
      contentItems[0] += "Ready";
    } else {
      contentItems[0] += "Not Ready";
    }
    
    // Item 1: Status
    contentItems[1] = "Status: " + uploadStatus;
    
    // Item 2: Blank line
    contentItems[2] = "";
    
    // Item 3-5: Result message
    if (uploadSuccess) {
      contentItems[3] = "Data berhasil";
      contentItems[4] = "diupload ke";
      contentItems[5] = "Firebase!";
    } else {
      contentItems[3] = "Upload gagal!";
      contentItems[4] = "Periksa koneksi";
      contentItems[5] = "WiFi/Firebase";
    }
    
    // Tampilkan 3 item sesuai menuIndex
    for(int i = 0; i < 3; i++) {
      int itemIndex = menuIndex + i;
      if(itemIndex < 6) {
        int y_pos = 17 + (i * 12);
        display.setCursor(3, y_pos);
        display.print(contentItems[itemIndex]);
      }
    }
    
    // Scroll indicators
    if(menuIndex > 0) {
      display.setCursor(120, 17);
      display.print("^");
    }
    if(menuIndex < 3) { // 6 items - 3 visible = 3
      display.setCursor(120, 38);
      display.print("v");
    }
  }
  
  // Footer
  display.drawLine(0, 53, 128, 53, SSD1306_WHITE);
  display.setCursor(2, 56);
  if(uploadInProgress) {
    display.print("Please wait...");
  } else {
    display.print("BACK: Kembali");
  }
  
  display.display();
}
void checkAllSensors() {
  npkSensorReady = testNPKSensor();
  delay(300);  // delay antar test

  phSensorReady = testPhSensor();
  delay(300);

  tempSensor.requestTemperatures();
  float temp = tempSensor.getTempCByIndex(0);
  tempSensorReady = (temp != DEVICE_DISCONNECTED_C && 
                     temp > -50 && temp < 100);
  delay(100);
  moistureSensorReady = testSoilMoistureSensor();
  delay(100);
  gpsSensorReady = testGPSSensor();
}

void readGPS() {
  unsigned long startTime = millis();
  gpsDataValid = false;
  
  while(millis() - startTime < 1000) {
    while (gpsSerial.available() > 0) {
      char c = gpsSerial.read();
      if (gps.encode(c)) {
        if (gps.location.isValid()) {
          currentLatitude = gps.location.lat();
          currentLongitude = gps.location.lng();
          gpsDataValid = true;
        }
        if (gps.satellites.isValid()) {
          currentSatellites = gps.satellites.value();
        }
      }
    }
    
    if(gpsDataValid) {
      break;
    }
  }
}

bool testGPSSensor() {
  unsigned long startTime = millis();
  bool dataReceived = false;
  int testSatelliteCount = 0;
  
  while(millis() - startTime < 3000) {
    while (gpsSerial.available() > 0) {
      char c = gpsSerial.read();
      if (gps.encode(c)) {
        dataReceived = true;
        if(gps.satellites.isValid()) {
          testSatelliteCount = gps.satellites.value();
          currentSatellites = testSatelliteCount;
        }
        if (gps.location.isValid()) {
          currentLatitude = gps.location.lat();
          currentLongitude = gps.location.lng();
          gpsDataValid = true;
        }
      }
    }
    
    if(dataReceived) {
      return true;
    }
  }
  
  return false;
}

void testPhBaudRates() {
  int baudRates[] = {9600, 4800, 2400};
  int numRates = 3;
  
  for(int i = 0; i < numRates; i++) {
    sharedSerial.end();
    delay(10);
    sharedSerial.begin(baudRates[i], SERIAL_8N1, PH_RX_PIN, PH_TX_PIN);
    delay(100);
    
    uint8_t testData[10] = {0};
    
    while(sharedSerial.available()) {
      sharedSerial.read();
    }
    
    sharedSerial.write(ComPH, 8);
    delay(200);
    
    int received = readPhSerial(testData, 7);
    if(received > 0) {
      phConnectionEstablished = true;
      phBaudRate = baudRates[i];
      return;
    }
  }
  phConnectionEstablished = false;
}

bool testPhSensor() {
  if(!phConnectionEstablished) {
    testPhBaudRates();
  }
  
  if(phConnectionEstablished) {
    setupSerialForPH();
    
    uint8_t testData[10] = {0};
    
    while(sharedSerial.available()) {
      sharedSerial.read();
    }
    
    sharedSerial.write(ComPH, 8);
    delay(200);
    
    int received = readPhSerial(testData, 7);
    return (received > 0);
  }
  
  return false;
}

void readCalibratedPH() {
  if(!phConnectionEstablished) return;
  
  setupSerialForPH();
  
  int rawValue = readPH();
  
  if (rawValue > 0) {
    pHSensor = rawValue / 10.0;  
    // pHCalibrated = (PH_CALIBRATION_SLOPE * pHSensor) + PH_CALIBRATION_OFFSET;
    pHCalibrated = (-0.1038 * pHSensor * pHSensor) + (2.2668 * pHSensor) - 2.7616;
    // pHCalibrated = (-0.0888 * pHSensor * pHSensor) + (2.061 * pHSensor) - 2.1149;
    
    if (pHCalibrated < 0.0) {
      pHCalibrated = 0.0;
    } else if (pHCalibrated > 14.0) {
      pHCalibrated = 14.0;
    }
  } else {
    pHCalibrated = -1;
  }
}

int readPH(void) {
  if(!phConnectionEstablished) return 0;
  
  uint8_t Data[10] = { 0 };
  uint8_t ch = 0;
  bool flag = 1;
  int attempts = 0;
  
  unsigned long startTime = millis();
  const unsigned long globalTimeout = 5000; // 5 detik

  while (flag && attempts < 3 && (millis() - startTime < globalTimeout)) {
    attempts++;
    
    while(sharedSerial.available()) {
      sharedSerial.read();
    }
    
    delay(50);
    sharedSerial.write(ComPH, 8);
    delay(100);
    
    if (readPhSerial(&ch, 1) == 1) {      
      if (ch == 0x01) {
        Data[0] = ch;
        if (readPhSerial(&ch, 1) == 1) {
          if (ch == 0x03) {
            Data[1] = ch;
            if (readPhSerial(&ch, 1) == 1) {
              if (ch == 0x02) {
                Data[2] = ch;
                if (readPhSerial(&Data[3], 4) == 4) {
                  uint16_t receivedCRC1 = Data[5] * 256 + Data[6];
                  uint16_t receivedCRC2 = Data[6] * 256 + Data[5];
                  uint16_t calculatedCRC = CRC16_2(Data, 5);
                  
                  if (calculatedCRC == receivedCRC1 || calculatedCRC == receivedCRC2) {
                    pHRaw = Data[3] * 256 + Data[4];
                    flag = 0;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  
  if (attempts >= 3 || (millis() - startTime >= globalTimeout)) {  // cek timeout
    pHRaw = 0;
  }
  
  sharedSerial.flush();
  return pHRaw;
}

uint8_t readPhSerial(uint8_t *buf, size_t len) {
  size_t offset = 0, left = len;
  int16_t timeout = 500;
  uint8_t *buffer = buf;
  long curr = millis();
  
  while (left && (millis() - curr < timeout)) {
    if (sharedSerial.available()) {
      buffer[offset] = sharedSerial.read();
      offset++;
      left--;
    }
    delay(1);
  }
  
  return offset;
}

bool testSoilMoistureSensor() {
  int testReadings[5];
  int validReadings = 0;
  
  for(int i = 0; i < 5; i++) {
    testReadings[i] = analogRead(SOIL_SENSOR_PIN);
    delay(50);
    
    if(testReadings[i] >= 0 && testReadings[i] <= 4095) {
      validReadings++;
    }
  }
  
  if(validReadings >= 3) {
    int sum = 0;
    for(int i = 0; i < 5; i++) {
      sum += testReadings[i];
    }
    int average = sum / 5;
    
    if(average == 0 || average == 4095) {
      return false;
    }
    
    return true;
  }
  
  return false;
}

void readSoilMoisture() {
  const int NUM_SAMPLES = 15;
  long sum = 0;
  for(int i = 0; i < NUM_SAMPLES; i++) {
    sum += analogRead(SOIL_SENSOR_PIN);
    delay(5);
  }
  soilSensorRawValue = sum / NUM_SAMPLES;

  float rawMoisture = (float)(ADC_DRY - soilSensorRawValue) * 100.0 / (float)(ADC_DRY - ADC_WET);
  if (rawMoisture > 100.0) rawMoisture = 100.0;
  if (rawMoisture < 0.0) rawMoisture = 0.0;

  moistureEMA = (moistureEMA < 0) ? rawMoisture : (EMA_ALPHA * rawMoisture) + ((1 - EMA_ALPHA) * moistureEMA);
  moistureLevel = moistureEMA;
}

bool testNPKSensor() {
  setupSerialForNPK();
  
  uint8_t Data[10] = { 0 };
  uint8_t ch = 0;
  bool sensor_ok = false;
  unsigned long startTime = millis();
  
  while ((millis() - startTime < 2000) && !sensor_ok) {
    sharedSerial.write(Com, 8);
    delay(50);
    
    if (readSerial(&ch, 1) == 1) {
      if (ch == 0x01) {
        Data[0] = ch;
        if (readSerial(&ch, 1) == 1) {
          if (ch == 0x03) {
            Data[1] = ch;
            if (readSerial(&ch, 1) == 1) {
              if (ch == 0x02) {
                Data[2] = ch;
                if (readSerial(&Data[3], 4) == 4) {
                  if (CRC16_2(Data, 5) == (Data[5] * 256 + Data[6])) {
                    sensor_ok = true;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  
  sharedSerial.flush();
  return sensor_ok;
}

// Fungsi untuk menghitung jarak Euclidean dengan normalisasi
float calculateEuclideanDistance(float n1, float p1, float k1, float ph1, float m1,
                                  TrainingData &data) {
  // Konversi data terkompresi ke float
  float n2 = data.n;
  float p2 = data.p;
  float k2 = data.k;
  float ph2 = getpH(data.ph);
  float m2 = data.moisture;

  // Normalisasi min-max tiap parameter sebelum hitung jarak
  float n1_norm = normalizeValue(n1, normStats.nMin, normStats.nMax);
  float n2_norm = normalizeValue(n2, normStats.nMin, normStats.nMax);
  
  float p1_norm = normalizeValue(p1, normStats.pMin, normStats.pMax);
  float p2_norm = normalizeValue(p2, normStats.pMin, normStats.pMax);
  
  float k1_norm = normalizeValue(k1, normStats.kMin, normStats.kMax);
  float k2_norm = normalizeValue(k2, normStats.kMin, normStats.kMax);
  
  float ph1_norm = normalizeValue(ph1, normStats.phMin, normStats.phMax);
  float ph2_norm = normalizeValue(ph2, normStats.phMin, normStats.phMax);
  
  float m1_norm = normalizeValue(m1, normStats.mMin, normStats.mMax);
  float m2_norm = normalizeValue(m2, normStats.mMin, normStats.mMax);
  
  float dn = n1_norm - n2_norm;
  float dp = p1_norm - p2_norm;
  float dk = k1_norm - k2_norm;
  float dph = ph1_norm - ph2_norm;
  float dm = m1_norm - m2_norm;
  
  return sqrt(dn*dn + dp*dp + dk*dk + dph*dph + dm*dm);
}

// Fungsi untuk sorting (bubble sort sederhana)
void sortDistances(DistanceResult distances[], int size) {
  for(int i = 0; i < size - 1; i++) {
    for(int j = 0; j < size - i - 1; j++) {
      if(distances[j].distance > distances[j+1].distance) {
        DistanceResult temp = distances[j];
        distances[j] = distances[j+1];
        distances[j+1] = temp;
      }
    }
  }
}

// Fungsi untuk menghitung min dan max dari training data
void calculateNormalizationStats() {
  // Baca data pertama dari flash sebagai nilai awal min/max
  TrainingData tempData;
  memcpy_P(&tempData, &trainingSet[0], sizeof(TrainingData));
  
  normStats.nMin = normStats.nMax = tempData.n;
  normStats.pMin = normStats.pMax = tempData.p;
  normStats.kMin = normStats.kMax = tempData.k;
  normStats.phMin = normStats.phMax = getpH(tempData.ph);
  normStats.mMin = normStats.mMax = tempData.moisture;
  
  for(int i = 1; i < JUMLAH_DATA; i++) {
    // Baca satu per satu dari flash agar hemat RAM
    memcpy_P(&tempData, &trainingSet[i], sizeof(TrainingData));
    
    // N
    if(tempData.n < normStats.nMin) normStats.nMin = tempData.n;
    if(tempData.n > normStats.nMax) normStats.nMax = tempData.n;
    
    // P
    if(tempData.p < normStats.pMin) normStats.pMin = tempData.p;
    if(tempData.p > normStats.pMax) normStats.pMax = tempData.p;
    
    // K
    if(tempData.k < normStats.kMin) normStats.kMin = tempData.k;
    if(tempData.k > normStats.kMax) normStats.kMax = tempData.k;
    
    // pH
    float currentPh = getpH(tempData.ph);
    if(currentPh < normStats.phMin) normStats.phMin = currentPh;
    if(currentPh > normStats.phMax) normStats.phMax = currentPh;
    
    // Moisture
    if(tempData.moisture < normStats.mMin) normStats.mMin = tempData.moisture;
    if(tempData.moisture > normStats.mMax) normStats.mMax = tempData.moisture;
  }
}

// Fungsi untuk normalisasi nilai (Min-Max Normalization)
float normalizeValue(float value, float min, float max) {
  if(max - min == 0) return 0.0; // Hindari division by zero
  return (value - min) / (max - min);
}
void calculatePlantRecommendation() {
  // Validasi data: tolak jika semua sensor terbaca nol/tidak wajar
  bool npkAllZero = (avg_N == 0 && avg_P == 0 && avg_K == 0);
  bool moistureZero = (avg_moisture == 0);
  bool phIs92 = (avg_pH > 9.2);
  if (npkAllZero && moistureZero && phIs92) {
    best_plant = "Tidak Ada";
    best_similarity = 0.0;
    return;
  }

  calculateNormalizationStats();

  // Simpan hanya K tetangga terdekat, tidak perlu simpan semua data training
  const int MAX_K = 20;  // sesuai kValue maksimal
  struct KNearestResult {
    uint8_t classID;
    float distance;
  };
  
  KNearestResult kNearest[MAX_K];
  
  // Inisialisasi dengan jarak infinite
  for(int i = 0; i < MAX_K; i++) {
    kNearest[i].classID = 0;
    kNearest[i].distance = 999999.0;
  }
  
  // Proses data training satu per satu (streaming), dibaca langsung dari flash
  TrainingData tempData;
  for(int i = 0; i < JUMLAH_DATA; i++) {
    memcpy_P(&tempData, &trainingSet[i], sizeof(TrainingData));

    float dist = calculateEuclideanDistance(
      avg_N, avg_P, avg_K, avg_pH, avg_moisture,
      tempData
    );

    // Cek apakah data ini masuk ke daftar K tetangga terdekat
    for(int k = 0; k < kValue && k < MAX_K; k++) {
      if(dist < kNearest[k].distance) {
        // Geser entri yang lebih jauh ke bawah
        for(int m = MAX_K - 1; m > k; m--) {
          kNearest[m] = kNearest[m-1];
        }
        // Sisipkan di posisi k
        kNearest[k].classID = tempData.classID;
        kNearest[k].distance = dist;
        break;
      }
    }

    // Yield setiap 50 data untuk feed watchdog ESP32
    if(i % 50 == 0) {
      yield();
      delay(1);
    }
  }

  // Reject option: tolak jika tetangga terdekat masih terlalu jauh
  if(kNearest[0].distance > 0.30f) {
    best_plant = "Tidak Dikenali";
    best_similarity = 0.0f;
    return;
  }

  // Weighted voting dengan bobot inverse-square distance
  float votes[5] = {0};
  float totalWeight = 0;

  int validCount = 0;
  for(int i = 0; i < kValue && i < MAX_K; i++) {
      if(kNearest[i].distance > 0.30f) continue;
      float weight;
      if(kNearest[i].distance == 0) {
        weight = 1000;
      } else {
        weight = 1.0 / (kNearest[i].distance * kNearest[i].distance);
      }
      votes[kNearest[i].classID] += weight;
      totalWeight += weight;
      validCount++;
  }
  
  // Cari kelas dengan weight terbesar
  int bestIndex = 0;
  float maxVote = votes[0];
  for(int i = 1; i < 5; i++) {
    if(votes[i] > maxVote) {
      maxVote = votes[i];
      bestIndex = i;
    }
  }
  
  // Set hasil prediksi
  best_plant = classNames[bestIndex];
  // best_similarity = (maxVote / totalWeight) * 100.0;
  float penalty = (float)validCount / (float)kValue;
  best_similarity = ((maxVote / totalWeight) * 100.0) * penalty;
  
  // Tidak pakai malloc/free, aman terhadap fragmentasi memori
}

float calibrateN(int rawValue) {
  int adjustedValue = rawValue - N_BASELINE;
  if (adjustedValue <= 0) return 0.0;
  float result = 0.2141* adjustedValue + 40.688;
  return (result < 0) ? 0.0 : result;
}

float calibrateP(int rawValue) {
  int adjustedValue = rawValue - P_BASELINE;
  if (adjustedValue <= 0) return 0.0;
  float result =  0.0414 * adjustedValue + 15.517;
  return (result < 0) ? 0.0 : result;
}

float calibrateK(int rawValue) {
  int adjustedValue = rawValue - K_BASELINE;
  if (adjustedValue <= 0) return 0.0;
  float result = 0.0784* adjustedValue + 29.932;
  return (result < 0) ? 0.0 : result;
}

int readN(void) {
  setupSerialForNPK();
  
  uint8_t Data[10] = { 0 };
  uint8_t ch = 0;
  bool flag = 1;
  
  unsigned long startTime = millis();
  const unsigned long timeout = 3000; // 3 detik

  while (flag && (millis() - startTime < timeout)) {
    delay(100);
    sharedSerial.write(Com, 8);
    delay(10);
    
    if (readSerial(&ch, 1) == 1) {
      if (ch == 0x01) {
        Data[0] = ch;
        if (readSerial(&ch, 1) == 1) {
          if (ch == 0x03) {
            Data[1] = ch;
            if (readSerial(&ch, 1) == 1) {
              if (ch == 0x02) {
                Data[2] = ch;
                if (readSerial(&Data[3], 4) == 4) {
                  if (CRC16_2(Data, 5) == (Data[5] * 256 + Data[6])) {
                    N = Data[3] * 256 + Data[4];
                    flag = 0;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  if (flag) {
    N = 0; // Jika timeout, set 0
  }
  sharedSerial.flush();
  return N;
}

int readP(void) {
  setupSerialForNPK();
  
  uint8_t Data1[10] = { 0 };
  uint8_t ch1 = 0;
  bool flag1 = 1;
  
  unsigned long startTime = millis();
  const unsigned long timeout = 3000;

  while (flag1 && (millis() - startTime < timeout)) {
    delay(100);
    sharedSerial.write(Com1, 8);
    delay(10);
    
    if (readSerial(&ch1, 1) == 1) {
      if (ch1 == 0x01) {
        Data1[0] = ch1;
        if (readSerial(&ch1, 1) == 1) {
          if (ch1 == 0x03) {
            Data1[1] = ch1;
            if (readSerial(&ch1, 1) == 1) {
              if (ch1 == 0x02) {
                Data1[2] = ch1;
                if (readSerial(&Data1[3], 4) == 4) {
                  if (CRC16_2(Data1, 5) == (Data1[5] * 256 + Data1[6])) {
                    P = Data1[3] * 256 + Data1[4];
                    flag1 = 0;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  if (flag1) {
    P = 0;
  }
  sharedSerial.flush();
  return P;
}

int readK(void) {
  setupSerialForNPK();
  
  uint8_t Data2[10] = { 0 };
  uint8_t ch2 = 0;
  bool flag2 = 1;
  
  unsigned long startTime = millis();
  const unsigned long timeout = 3000;

  while (flag2 && (millis() - startTime < timeout)) {
    delay(100);
    sharedSerial.write(Com2, 8);
    delay(10);
    
    if (readSerial(&ch2, 1) == 1) {
      if (ch2 == 0x01) {
        Data2[0] = ch2;
        if (readSerial(&ch2, 1) == 1) {
          if (ch2 == 0x03) {
            Data2[1] = ch2;
            if (readSerial(&ch2, 1) == 1) {
              if (ch2 == 0x02) {
                Data2[2] = ch2;
                if (readSerial(&Data2[3], 4) == 4) {
                  if (CRC16_2(Data2, 5) == (Data2[5] * 256 + Data2[6])) {
                    K = Data2[3] * 256 + Data2[4];
                    flag2 = 0;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  if (flag2) {
    K = 0;
  }
  sharedSerial.flush();
  return K;
}

uint8_t readSerial(uint8_t *buf, size_t len) {
  size_t offset = 0, left = len;
  int16_t timeout = 500;
  uint8_t *buffer = buf;
  long curr = millis();
  
  while (left) {
    if (sharedSerial.available()) {
      buffer[offset] = sharedSerial.read();
      offset++;
      left--;
    }
    if (millis() - curr > timeout) {
      break;
    }
  }
  return offset;
}

unsigned int CRC16_2(unsigned char *buf, int len) {
  unsigned int crc = 0xFFFF;
  for (int pos = 0; pos < len; pos++) {
    crc ^= (unsigned int)buf[pos];
    for (int i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  crc = ((crc & 0x00ff) << 8) | ((crc & 0xff00) >> 8);
  return crc;
}