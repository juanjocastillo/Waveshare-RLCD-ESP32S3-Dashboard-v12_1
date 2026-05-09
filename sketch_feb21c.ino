#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans24pt7b.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Update.h>
#include <TJpg_Decoder.h>
#include <AudioFileSourceICYStream.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorAAC.h>
#include <AudioOutputI2S.h>
#include <PubSubClient.h>
#include <esp_heap_caps.h>
#include <ESP_I2S.h>
#include <time.h>
#include <math.h>
#include <Wire.h>
#include "display_bsp.h"
#include "font.h"
#include "secfont.h"
#include "PCF85063A-SOLDERED.h"
#include "iss_world_map.h"



// ===== CONFIGURATION =====
#include "secrets.h"   // WiFi SSID/password, API key, location â€” keep out of version control

const char* ntpServer          = "2.ar.pool.ntp.org";

// POSIX TZ string â€” handles DST transitions automatically.
// AEST-10AEDT,M10.1.0,M4.1.0/3 means:
//   Standard: AEST UTC+10, DST: AEDT UTC+11
//   DST starts 1st Sunday October 02:00, ends 1st Sunday April 03:00
// Change if your timezone differs â€” other options:
//   "AWST-8"                           Perth       (no DST)
//   "ACST-9:30"                        Darwin      (no DST)
//   "ACST-9:30ACDT,M10.1.0,M4.1.0/3"  Adelaide    (DST)
//   "AEST-10"                          Brisbane    (no DST)
//   "AEST-10AEDT,M10.1.0,M4.1.0/3"    Sydney/Melb (DST)
static const char* kDefaultPosixTZ = "<-03>3";
static const char* kDefaultTZLabel = "GMT-3";
char configuredPosixTZ[64] = "<-03>3";
char configuredTZLabel[16] = "GMT-3";
Preferences preferences;

// gmtOffset_sec is now computed at runtime from the system clock after NTP sync.
// It reflects the current UTC offset including DST. Do NOT hardcode this.
long gmtOffset_sec = -10800;   // default Argentina GMT-3 â€” overwritten after NTP sync

const int BTN_LEFT    = 0;
const int BTN_MIDDLE  = 18;
const int BAT_ADC_PIN = 4;
static const uint8_t SHTC3_ADDR = 0x70;
const int SPK_EN_PIN = 46;
const int I2S_DOUT_PIN = 8;
const int I2S_BCLK_PIN = 9;
const int I2S_LRCLK_PIN = 45;
const int I2S_MCLK_PIN = 16;

#define FONT_SMALL   FreeSans9pt7b
#define FONT_MEDIUM  FreeSans12pt7b
#define FONT_LARGE   FreeSans18pt7b
#define FONT_XLARGE  FreeSans24pt7b



static const int W = 400;
static const int H = 300;
DisplayPort RlcdPort(12, 11, 5, 40, 41, W, H);
GFXcanvas1  canvas(W, H);
PCF85063A   rtc;
I2SClass i2sAudio;
AudioGeneratorMP3* radioMp3 = nullptr;
AudioGeneratorAAC* radioAac = nullptr;
AudioFileSourceICYStream* radioFile = nullptr;
AudioFileSourceBuffer* radioBuff = nullptr;
AudioOutputI2S* radioOut = nullptr;
bool audioReady = false;
bool codecReady = false;
bool radioEngineReady = false;
bool radioPlaying = false;
bool radioFallbackTried = false;
String radioNowPlaying = "";
String radioStatus = "Detenida";
String otaStatus = "Sin actualizacion";
bool otaInProgress = false;
int otaProgressPct = 0;
size_t otaUploadBytes = 0;
size_t otaExpectedBytes = 0;
String otaUploadingFilename = "";
String currentFirmwareName = "RLCD42-v11_3.bin";
int radioDecoderErrorBurst = 0;
unsigned long radioDecoderErrorWindowStart = 0;
unsigned long radioConnectStartMs = 0;
unsigned long radioLastEventMs = 0;
WebServer configServer(80);
WiFiClient mqttNetClient;
PubSubClient mqttClient(mqttNetClient);

static const char* kConfigApSsid = "RLCD-Setup";
static bool wifiUsedPortal = false;
static String wifiPortalSSID = "";
static String wifiPortalIP = "";

float temperature    = 0.0f;
float humidity       = 0.0f;
float batteryVoltage = 0.0f;
bool batteryCharging = false;
int   wifiRSSI       = 0;
int   hour24         = 0;
int   minuteVal      = 0;
int   secondVal      = 0;
bool  wifiConnected  = false;
int   sensorUpdateCounter = 0;
unsigned long ntpLastSync = 0;
int   sensorReadCount     = 0;
int   sensorFailCount     = 0;
String detectedCity       = "";
float detectedLat         = 0.0f;
float detectedLon         = 0.0f;
bool  detectedLocationValid = false;
unsigned long cityLastAttempt = 0;
unsigned long kumaLastFetch = 0;
unsigned long issLastFetch = 0;
unsigned long issOrbitLastFetch = 0;
String weatherLastError   = "Sin intento";
int weatherLastHttpCode   = 0;
bool weatherRefreshRequested = false;
bool kumaRefreshRequested = false;
bool issRefreshRequested = false;
bool photoRefreshRequested = false;

String photoTheme = "anime,girl";
bool photoUseUploaded = false;
int audioVolumePct = 80;
bool batteryCriticalBeepEnabled = true;
bool issFootprintBeepEnabled = false;
bool displayInvertMode = false;
bool batterySaveMode = false;
String radioCountry = "AR";
String radioFilterCodec = "mp3";
int radioFilterBitrateMax = 96;
static const int RADIO_PRESET_COUNT = 10;
String radioPresetName[RADIO_PRESET_COUNT];
String radioPresetUrl[RADIO_PRESET_COUNT];
String radioPresetCodec[RADIO_PRESET_COUNT];
String radioPresetCountry[RADIO_PRESET_COUNT];
String radioStationName = "";
String radioStationUrl = "";
String radioStationCodec = "mp3";
int radioVolumePct = 60;
bool radioMuted = false;
// MQTT deshabilitado para edicion/publicacion YT-GitHub.
bool mqttEnabled = false;
String mqttHost = "MQTT_HOST_DISABLED";
int mqttPort = 1883;
String mqttUser = "MQTT_USER_DISABLED";
String mqttPass = "MQTT_PASS_DISABLED";
String mqttTopicRoot = "MQTT_TOPIC_DISABLED";
bool mqttConnected = false;
String mqttStatusText = "No disponible";
unsigned long mqttLastConnectAttemptMs = 0;
unsigned long mqttLastStatePublishMs = 0;
unsigned long radioLastScreenDrawMs = 0;
unsigned long radioLastAutoStartAttemptMs = 0;
bool radioStartRequested = false;
bool radioUserStopped = true;
static const char* kFixedStationName = "Aspen 102.3";
static const char* kFixedStationUrl  = "http://26653.live.streamtheworld.com/ASPEN.mp3";
static const char* kFallbackStationName = "SomaFM Groove Salad";
static const char* kFallbackStationUrl  = "http://ice1.somafm.com/groovesalad-128-mp3";
unsigned long lastBatteryCriticalBeepMs = 0;
unsigned long lastIssPassBeepMs = 0;
bool issWasInsideAlertRange = false;
unsigned long lastWebVolumeBeepMs = 0;
String connectedWifiPassword = "";
String webAdminPassword = "";
String webAuthRealm = "RLCD-Config";
const char* kRuntimeConfigPath = "/config.json";

// KUMA status source (default):
// - Deploy Uptime Kuma and create a public Status Page.
// - Copy the base URL and status page slug.
// - Example endpoint used by this firmware:
//   <base>/api/status-page/<slug>
//
// Alternative without self-hosting:
// - UptimeRobot can be used as data source too, but you must adapt
//   fetchKumaDashboardData() to consume UptimeRobot API JSON and map
//   fields to kumaData.groups[] / kumaData.monitors[].
// - UptimeRobot docs: https://uptimerobot.com/api/
static const char* kKumaBaseUrl = "https://YOUR_KUMA_DOMAIN";
static const char* kKumaSlug = "YOUR_STATUS_PAGE_SLUG";
static const int KUMA_MAX_GROUPS = 8;
static const int KUMA_MAX_MONITORS = 24;
static const int KUMA_MAX_GROUP_MONS = 8;
static const int KUMA_BEANS = 20;
static const int WIFI_SCAN_MAX = 12;
static const int WIFI_HISTORY_MAX = 3;

int  currentPage  = 0;
const int totalPages = 14;
int  previousPage = -1;

// Flash state for Earth dot on seasons orbit page
bool     earthFlashOn    = true;
unsigned long earthFlashLast = 0;
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 200;

const int HISTORY_SIZE = 24;

struct WeatherData {
  float  currentTemp;
  float  feelsLike;
  String condition;
  int    humidity;
  float  windSpeed;
  String windDir;
  float  uvIndex;
  float  precipMM;
  int    airQualityIndex;
  String airQualityText;
  float  pm25;
  struct Forecast {
    String day;
    float  maxTemp;
    float  minTemp;
    String condition;
    float  precipMM;
    int    rainChance;
  } forecast[3];
  unsigned long lastUpdate;
  bool valid;
} weatherData;

struct AstronomyData {
  String sunrise, sunset, moonrise, moonset, moonPhase;
  int    moonIllumination;
  bool   valid;
} astroData;

struct HourlyData {
  float  temp[6];
  int    rainChance[6];
  float  rainMM[6];
  float  uvIndex[6];
  float  windSpeed[6];
  String time[6];
  bool   valid;
} hourlyData;

struct HistoricalData {
  float tempHistory[HISTORY_SIZE];
  float humidityHistory[HISTORY_SIZE];
  int   currentIndex;
  unsigned long lastLogTime;
  bool  initialized;
  int   sampleCount;
} history;

struct GraphBounds { float mn, mx, rng; };

struct SeasonEvents {
  int marchEq;
  int juneSol;
  int septEq;
  int decSol;
};

struct SeasonInfo {
  const char* name;
  int daysSince;
  int daysUntil;
  const char* nextEvent;
  int nextEventDoy;
};

struct KumaMonitor {
  int id;
  String name;
  int latestStatus;   // 1 up, 0 down, -1 unknown
  int latestPingMs;
  String latestTime;
  uint8_t beans[KUMA_BEANS]; // 1 up, 0 down/unknown
  bool hasHeartbeat;
};

struct KumaGroup {
  String name;
  int monitorIdx[KUMA_MAX_GROUP_MONS];
  int monitorCount;
};

struct KumaData {
  KumaGroup groups[KUMA_MAX_GROUPS];
  int groupCount;
  KumaMonitor monitors[KUMA_MAX_MONITORS];
  int monitorCount;
  bool valid;
  unsigned long lastUpdate;
} kumaData;

struct WifiScanEntry {
  String ssid;
  int rssi;
  uint8_t enc;
};

struct IssData {
  float latitude;
  float longitude;
  float altitudeKm;
  float velocityKmh;
  unsigned long timestamp;
  bool valid;
  String lastError;
} issData;

void applyRadioVolume();
void stopRadioPlayback();
bool startRadioPlayback();
void serviceRadioPlayback();
bool preflightRadioUrl(const String& url, String& why);
String normalizeRadioStreamUrlForDevice(const String& url);
void radioMetadataCb(void* cbData, const char* type, bool isUnicode, const char* str);
void radioStatusCb(void* cbData, int code, const char* str);
bool loadRuntimeConfigFile();
bool saveRuntimeConfigFile();
bool loadUploadedPhotoFromFs(String& err);
bool validateUploadedPhoto(const uint8_t* data, size_t len, String& err);
void handleConfigImageUploadDone();
void handleConfigImageUploadStream();
void handleConfigOtaStatus();
void handleConfigOtaUploadDone();
void handleConfigOtaUploadStream();
void drawOtaProgressScreen();
void drawOtaProgressOverlay();
static int jpegSofMarker(const uint8_t* b, size_t n);

static const int ISS_TRAIL_MAX = 24;
float issTrailLat[ISS_TRAIL_MAX];
float issTrailLon[ISS_TRAIL_MAX];
int issTrailCount = 0;
static const int ISS_ORBIT_WINDOW_SEC = 50 * 60;     // ~1 orbit around now (about 100 min total)
static const int ISS_ORBIT_STEP_SEC = 180;           // 3 minutes
static const int ISS_POSITIONS_MAX_PER_REQ = 10;     // API documented limit
static const int ISS_ORBIT_MAX = ((ISS_ORBIT_WINDOW_SEC * 2) / ISS_ORBIT_STEP_SEC) + 1;
float issOrbitLat[ISS_ORBIT_MAX];
float issOrbitLon[ISS_ORBIT_MAX];
unsigned long issOrbitTs[ISS_ORBIT_MAX];
int issOrbitCount = 0;
bool issOrbitValid = false;
bool issOrbitFetchInProgress = false;
int issOrbitFetchIndex = 0;
int issOrbitFetchWritten = 0;
unsigned long issOrbitFetchStartTs = 0;
unsigned long issOrbitNextReqAt = 0;
String issOrbitLastError = "";

WifiScanEntry wifiScanList[WIFI_SCAN_MAX];
int wifiScanCount = 0;
unsigned long wifiScanLast = 0;
bool wifiScanInProgress = false;

static const char* kPhotoUrlFmt0 = "https://loremflickr.com/g/400/272/anime,girl?lock=%lu";
static const char* kPhotoUrlFmt1 = "https://loremflickr.com/g/400/272/anime,girl?lock=%lu";
static const char* kPhotoUrlFmt2 = "https://loremflickr.com/g/400/272/anime,girl,manga?lock=%lu";
static const size_t kPhotoMaxBytes = 300000;
static const char* kUploadedPhotoPath = "/photo_user.jpg";
static const char* kUploadedPhotoTmpPath = "/photo_user.tmp";
uint8_t* photoJpegData = nullptr;
size_t photoJpegLen = 0;
bool photoValid = false;
unsigned long photoLastFetch = 0;
int photoRefreshMinutes = 5;
String photoLastError = "Sin imagen";
String photoUploadStatus = "Sin imagen subida";
File photoUploadFile;
bool photoUploadInProgress = false;
size_t photoUploadBytes = 0;
bool photoUploadOk = false;
String photoUploadErr = "";

String getDisplayLocation() {
  if (detectedCity.length() > 0) return detectedCity;
  String fallback = String(weatherLocation);
  fallback.trim();
  if (fallback.length() == 0 || fallback.equalsIgnoreCase("location city")) return "Ciudad";
  return fallback;
}

void applyConfiguredTimeZone() {
  setenv("TZ", configuredPosixTZ, 1);
  tzset();
}

void loadTimeZoneConfig() {
  String tz = kDefaultPosixTZ;
  String lbl = kDefaultTZLabel;

  if (preferences.begin("catdisplay", true)) {
    tz = preferences.getString("tz", kDefaultPosixTZ);
    lbl = preferences.getString("tzlbl", kDefaultTZLabel);
    preferences.end();
  } else {
    // First boot or erased NVS: create defaults.
    if (preferences.begin("catdisplay", false)) {
      preferences.putString("tz", kDefaultPosixTZ);
      preferences.putString("tzlbl", kDefaultTZLabel);
      preferences.end();
    }
  }

  tz.trim();
  lbl.trim();
  if (tz.length() == 0) tz = kDefaultPosixTZ;
  if (lbl.length() == 0) lbl = kDefaultTZLabel;

  snprintf(configuredPosixTZ, sizeof(configuredPosixTZ), "%s", tz.c_str());
  snprintf(configuredTZLabel, sizeof(configuredTZLabel), "%s", lbl.c_str());
  applyConfiguredTimeZone();
}

void saveTimeZoneConfig(const char* tz, const char* label) {
  if (preferences.begin("catdisplay", false)) {
    preferences.putString("tz", tz);
    preferences.putString("tzlbl", label);
    preferences.end();
  }
}

String getStoredWifiPasswordForSSID(const String& ssid) {
  if (ssid.length() == 0) return "";
  String out = "";
  if (!preferences.begin("catdisplay", true)) return "";
  for (int i = 0; i < WIFI_HISTORY_MAX; i++) {
    char kS[6];
    char kP[6];
    snprintf(kS, sizeof(kS), "w%ds", i);
    snprintf(kP, sizeof(kP), "w%dp", i);
    String s = preferences.getString(kS, "");
    String p = preferences.getString(kP, "");
    s.trim();
    if (s == ssid) {
      out = p;
      break;
    }
  }
  preferences.end();
  return out;
}

void updateWebAdminPasswordFromWiFi() {
  String p = connectedWifiPassword;
  if (p.length() < 8) p = WiFi.psk();
  if (p.length() < 8) p = getStoredWifiPasswordForSSID(WiFi.SSID());
  if (p.length() < 8) p = String(password);
  p.trim();
  if (p.length() >= 8) webAdminPassword = p;
}

bool loadRuntimeConfigFile() {
  if (!SPIFFS.begin(true)) return false;
  if (!SPIFFS.exists(kRuntimeConfigPath)) return false;
  File f = SPIFFS.open(kRuntimeConfigPath, FILE_READ);
  if (!f) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  photoTheme = doc["photoTheme"] | photoTheme;
  photoUseUploaded = doc["photoUseUploaded"] | photoUseUploaded;
  photoRefreshMinutes = doc["photoRefreshMinutes"] | photoRefreshMinutes;
  audioVolumePct = doc["audioVolumePct"] | audioVolumePct;
  batteryCriticalBeepEnabled = doc["batteryCriticalBeepEnabled"] | batteryCriticalBeepEnabled;
  issFootprintBeepEnabled = doc["issFootprintBeepEnabled"] | issFootprintBeepEnabled;
  displayInvertMode = doc["displayInvertMode"] | displayInvertMode;
  batterySaveMode = doc["batterySaveMode"] | batterySaveMode;
  radioCountry = doc["radioCountry"] | radioCountry;
  radioFilterCodec = doc["radioFilterCodec"] | radioFilterCodec;
  radioFilterBitrateMax = doc["radioFilterBitrateMax"] | radioFilterBitrateMax;
  for (int i = 0; i < RADIO_PRESET_COUNT; i++) {
    String kn = String("radioPresetName") + i;
    String ku = String("radioPresetUrl") + i;
    String kc = String("radioPresetCodec") + i;
    String kcc = String("radioPresetCountry") + i;
    radioPresetName[i] = doc[kn] | radioPresetName[i];
    radioPresetUrl[i] = doc[ku] | radioPresetUrl[i];
    radioPresetCodec[i] = doc[kc] | radioPresetCodec[i];
    radioPresetCountry[i] = doc[kcc] | radioPresetCountry[i];
  }
  radioStationName = doc["radioStationName"] | radioStationName;
  radioStationUrl = doc["radioStationUrl"] | radioStationUrl;
  radioStationCodec = doc["radioStationCodec"] | radioStationCodec;
  radioVolumePct = doc["radioVolumePct"] | radioVolumePct;
  currentPage = doc["currentPage"] | currentPage;

  String tz = doc["configuredPosixTZ"] | "";
  String tzl = doc["configuredTZLabel"] | "";
  tz.trim();
  tzl.trim();
  if (tz.length() > 0) snprintf(configuredPosixTZ, sizeof(configuredPosixTZ), "%s", tz.c_str());
  if (tzl.length() > 0) snprintf(configuredTZLabel, sizeof(configuredTZLabel), "%s", tzl.c_str());

  return true;
}

bool saveRuntimeConfigFile() {
  if (!SPIFFS.begin(true)) return false;
  JsonDocument doc;
  doc["photoTheme"] = photoTheme;
  doc["photoUseUploaded"] = photoUseUploaded;
  doc["photoRefreshMinutes"] = photoRefreshMinutes;
  doc["audioVolumePct"] = audioVolumePct;
  doc["batteryCriticalBeepEnabled"] = batteryCriticalBeepEnabled;
  doc["issFootprintBeepEnabled"] = issFootprintBeepEnabled;
  doc["displayInvertMode"] = displayInvertMode;
  doc["batterySaveMode"] = batterySaveMode;
  doc["radioCountry"] = radioCountry;
  doc["radioFilterCodec"] = radioFilterCodec;
  doc["radioFilterBitrateMax"] = radioFilterBitrateMax;
  for (int i = 0; i < RADIO_PRESET_COUNT; i++) {
    doc[String("radioPresetName") + i] = radioPresetName[i];
    doc[String("radioPresetUrl") + i] = radioPresetUrl[i];
    doc[String("radioPresetCodec") + i] = radioPresetCodec[i];
    doc[String("radioPresetCountry") + i] = radioPresetCountry[i];
  }
  doc["radioStationName"] = radioStationName;
  doc["radioStationUrl"] = radioStationUrl;
  doc["radioStationCodec"] = radioStationCodec;
  doc["radioVolumePct"] = radioVolumePct;
  doc["currentPage"] = currentPage;
  doc["configuredPosixTZ"] = configuredPosixTZ;
  doc["configuredTZLabel"] = configuredTZLabel;
  File f = SPIFFS.open(kRuntimeConfigPath, FILE_WRITE);
  if (!f) return false;
  bool ok = (serializeJson(doc, f) > 0);
  f.close();
  return ok;
}

void loadRuntimeConfig() {
  if (preferences.begin("catdisplay", true)) {
    photoTheme = preferences.getString("img_theme", "anime,girl");
    photoUseUploaded = preferences.getBool("img_up", false);
    photoRefreshMinutes = preferences.getInt("img_min", 5);
    audioVolumePct = preferences.getInt("aud_vol", 80);
    batteryCriticalBeepEnabled = preferences.getBool("beep_bat", true);
    issFootprintBeepEnabled = preferences.getBool("beep_iss", false);
    displayInvertMode = preferences.getBool("disp_inv", false);
    batterySaveMode = preferences.getBool("bat_save", false);
    radioCountry = preferences.getString("rad_cc", "AR");
    radioFilterCodec = preferences.getString("rad_fcodec", "mp3");
    radioFilterBitrateMax = preferences.getInt("rad_fbr", 96);
    for (int i = 0; i < RADIO_PRESET_COUNT; i++) {
      char kn[10];
      char ku[10];
      char kc[10];
      char kcc[10];
      snprintf(kn, sizeof(kn), "rpn%d", i);
      snprintf(ku, sizeof(ku), "rpu%d", i);
      snprintf(kc, sizeof(kc), "rpc%d", i);
      snprintf(kcc, sizeof(kcc), "rcc%d", i);
      radioPresetName[i] = preferences.getString(kn, "");
      radioPresetUrl[i] = preferences.getString(ku, "");
      radioPresetCodec[i] = preferences.getString(kc, "");
      radioPresetCountry[i] = preferences.getString(kcc, "AR");
    }
    radioStationName = preferences.getString("rad_name", "");
    radioStationUrl = preferences.getString("rad_url", "");
    radioStationCodec = preferences.getString("rad_codec", "mp3");
    radioVolumePct = preferences.getInt("rad_vol", 60);
    radioMuted = preferences.getBool("rad_muted", false);
    currentFirmwareName = preferences.getString("fw_name", "RLCD42-v11_3.bin");
    mqttEnabled = preferences.getBool("mqtt_en", true);
    mqttHost = preferences.getString("mqtt_h", "MQTT_HOST_DISABLED");
    mqttPort = preferences.getInt("mqtt_p", 1883);
    mqttUser = preferences.getString("mqtt_u", "MQTT_USER_DISABLED");
    mqttPass = preferences.getString("mqtt_pw", "MQTT_PASS_DISABLED");
    mqttTopicRoot = preferences.getString("mqtt_rt", "MQTT_TOPIC_DISABLED");
    preferences.end();
  }

  photoTheme.trim();
  if (photoTheme.length() == 0) photoTheme = "anime,girl";
  if (photoRefreshMinutes < 1) photoRefreshMinutes = 1;
  if (photoRefreshMinutes > 10) photoRefreshMinutes = 10;
  if (photoUseUploaded) {
    String err;
    if (!loadUploadedPhotoFromFs(err)) {
      photoUseUploaded = false;
      photoUploadStatus = "Error imagen fija: " + err;
    } else {
      photoUploadStatus = "Imagen fija activa (400x272)";
    }
  }
  if (audioVolumePct < 0) audioVolumePct = 0;
  if (audioVolumePct > 100) audioVolumePct = 100;
  radioCountry.trim();
  radioCountry.toUpperCase();
  if (radioCountry != "--" && radioCountry.length() != 2) radioCountry = "AR";
  radioFilterCodec.trim();
  radioFilterCodec.toLowerCase();
  if (radioFilterCodec != "mp3" && radioFilterCodec != "aac" && radioFilterCodec != "aacp" && radioFilterCodec != "any") {
    radioFilterCodec = "mp3";
  }
  if (radioFilterBitrateMax != 32 && radioFilterBitrateMax != 48 && radioFilterBitrateMax != 64 &&
      radioFilterBitrateMax != 96 && radioFilterBitrateMax != 128) {
    radioFilterBitrateMax = 96;
  }
  for (int i = 0; i < RADIO_PRESET_COUNT; i++) {
    radioPresetName[i].trim();
    radioPresetUrl[i].trim();
    radioPresetCodec[i].trim();
    radioPresetCodec[i].toLowerCase();
    radioPresetCountry[i].trim();
    radioPresetCountry[i].toUpperCase();
    if (radioPresetCodec[i].length() == 0) radioPresetCodec[i] = "mp3";
    if (radioPresetCountry[i] != "--" && radioPresetCountry[i].length() != 2) radioPresetCountry[i] = "AR";
    if (radioPresetUrl[i].length() == 0) {
      radioPresetName[i] = "";
      radioPresetCountry[i] = "AR";
    }
  }
  radioStationName.trim();
  radioStationUrl.trim();
  radioStationCodec.trim();
  radioStationCodec.toLowerCase();
  if (radioStationName.length() == 0) radioStationName = kFixedStationName;
  if (radioStationUrl.length() == 0) radioStationUrl = kFixedStationUrl;
  if (radioStationCodec.length() == 0) radioStationCodec = "mp3";
  radioMuted = false;
  if (radioVolumePct < 0) radioVolumePct = 0;
  if (radioVolumePct > 100) radioVolumePct = 100;
  if (currentPage < 0 || currentPage >= totalPages) currentPage = 0;
  mqttHost.trim();
  if (mqttHost.length() == 0) mqttHost = "MQTT_HOST_DISABLED";
  if (mqttPort <= 0 || mqttPort > 65535) mqttPort = 1883;
  mqttUser.trim();
  if (mqttUser.length() == 0) mqttUser = "MQTT_USER_DISABLED";
  mqttTopicRoot.trim();
  mqttTopicRoot.toLowerCase();
  if (mqttTopicRoot.length() == 0) mqttTopicRoot = "MQTT_TOPIC_DISABLED";
  loadRuntimeConfigFile();
}

void saveRuntimeConfig() {
  if (!preferences.begin("catdisplay", false)) return;
  preferences.putString("img_theme", photoTheme);
  preferences.putBool("img_up", photoUseUploaded);
  preferences.putInt("img_min", photoRefreshMinutes);
  preferences.putInt("aud_vol", audioVolumePct);
  preferences.putBool("beep_bat", batteryCriticalBeepEnabled);
  preferences.putBool("beep_iss", issFootprintBeepEnabled);
  preferences.putBool("disp_inv", displayInvertMode);
  preferences.putBool("bat_save", batterySaveMode);
  preferences.putString("rad_cc", radioCountry);
  preferences.putString("rad_fcodec", radioFilterCodec);
  preferences.putInt("rad_fbr", radioFilterBitrateMax);
  for (int i = 0; i < RADIO_PRESET_COUNT; i++) {
    char kn[10];
    char ku[10];
    char kc[10];
    char kcc[10];
    snprintf(kn, sizeof(kn), "rpn%d", i);
    snprintf(ku, sizeof(ku), "rpu%d", i);
    snprintf(kc, sizeof(kc), "rpc%d", i);
    snprintf(kcc, sizeof(kcc), "rcc%d", i);
    preferences.putString(kn, radioPresetName[i]);
    preferences.putString(ku, radioPresetUrl[i]);
    preferences.putString(kc, radioPresetCodec[i]);
    preferences.putString(kcc, radioPresetCountry[i]);
  }
  preferences.putString("rad_name", radioStationName);
  preferences.putString("rad_url", radioStationUrl);
  preferences.putString("rad_codec", radioStationCodec);
  preferences.putInt("rad_vol", radioVolumePct);
  preferences.putBool("rad_muted", radioMuted);
  preferences.putBool("mqtt_en", mqttEnabled);
  preferences.putString("mqtt_h", mqttHost);
  preferences.putInt("mqtt_p", mqttPort);
  preferences.putString("mqtt_u", mqttUser);
  preferences.putString("mqtt_pw", mqttPass);
  preferences.putString("mqtt_rt", mqttTopicRoot);
  preferences.end();
  saveRuntimeConfigFile();
}

float estimateBatteryMinutesLeft(float vbat) {
  const float vMin = 3.40f;
  const float vMax = 4.20f;
  const float fullMinutes = 180.0f; // approximation
  float pct = (vbat - vMin) / (vMax - vMin);
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 1.0f) pct = 1.0f;
  return pct * fullMinutes;
}

bool isBatteryCriticalSoon() {
  return estimateBatteryMinutesLeft(batteryVoltage) <= 5.0f;
}

int getEffectiveAudioPercent() {
  int ui = audioVolumePct;
  if (ui < 0) ui = 0;
  if (ui > 100) ui = 100;
  // Remap: UI 0..100 => old effective 50..100
  return 50 + (ui / 2);
}

bool tryConnectWiFiCredentials(const String& ssid, const String& pass, unsigned long timeoutMs) {
  if (ssid.length() == 0) return false;
  WiFi.disconnect(false, false);
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    delay(200);
  }
  return WiFi.status() == WL_CONNECTED;
}

void saveWifiHistoryEntry(const String& ssidIn, const String& passIn) {
  String ssid = ssidIn;
  String pass = passIn;
  ssid.trim();
  pass.trim();
  if (ssid.length() == 0 || pass.length() == 0) return;

  String s[WIFI_HISTORY_MAX];
  String p[WIFI_HISTORY_MAX];

  if (!preferences.begin("catdisplay", false)) return;
  for (int i = 0; i < WIFI_HISTORY_MAX; i++) {
    char kS[6];
    char kP[6];
    snprintf(kS, sizeof(kS), "w%ds", i);
    snprintf(kP, sizeof(kP), "w%dp", i);
    s[i] = preferences.getString(kS, "");
    p[i] = preferences.getString(kP, "");
  }

  String ns[WIFI_HISTORY_MAX];
  String np[WIFI_HISTORY_MAX];
  int n = 0;
  ns[n] = ssid;
  np[n] = pass;
  n++;

  for (int i = 0; i < WIFI_HISTORY_MAX && n < WIFI_HISTORY_MAX; i++) {
    if (s[i].length() == 0 || p[i].length() == 0) continue;
    if (s[i] == ssid) continue;
    ns[n] = s[i];
    np[n] = p[i];
    n++;
  }

  for (int i = 0; i < WIFI_HISTORY_MAX; i++) {
    char kS[6];
    char kP[6];
    snprintf(kS, sizeof(kS), "w%ds", i);
    snprintf(kP, sizeof(kP), "w%dp", i);
    preferences.putString(kS, i < n ? ns[i] : "");
    preferences.putString(kP, i < n ? np[i] : "");
  }
  preferences.end();
}

bool tryConnectFromWifiHistory() {
  String s[WIFI_HISTORY_MAX];
  String p[WIFI_HISTORY_MAX];
  if (!preferences.begin("catdisplay", true)) return false;
  for (int i = 0; i < WIFI_HISTORY_MAX; i++) {
    char kS[6];
    char kP[6];
    snprintf(kS, sizeof(kS), "w%ds", i);
    snprintf(kP, sizeof(kP), "w%dp", i);
    s[i] = preferences.getString(kS, "");
    p[i] = preferences.getString(kP, "");
  }
  preferences.end();

  for (int i = 0; i < WIFI_HISTORY_MAX; i++) {
    s[i].trim();
    p[i].trim();
    if (s[i].length() == 0 || p[i].length() == 0) continue;
    Serial.printf("WiFi historial #%d: %s\n", i + 1, s[i].c_str());
    if (tryConnectWiFiCredentials(s[i], p[i], 8000)) {
      connectedWifiPassword = p[i];
      saveWifiHistoryEntry(s[i], p[i]);  // Keep MRU order fresh.
      return true;
    }
  }
  return false;
}

void applyTimeZonePreset(String preset, String& tzOut, String& lblOut) {
  preset.trim();
  preset.toUpperCase();
  if (preset == "AR") { tzOut = "<-03>3"; lblOut = "GMT-3"; return; }
  if (preset == "CL") { tzOut = "<-04>4"; lblOut = "GMT-4"; return; }
  if (preset == "CO") { tzOut = "<-05>5"; lblOut = "GMT-5"; return; }
  if (preset == "UTC") { tzOut = "UTC0"; lblOut = "UTC"; return; }
  if (preset == "CET") { tzOut = "CET-1CEST,M3.5.0/2,M10.5.0/3"; lblOut = "CET"; return; }
  if (preset == "JST") { tzOut = "JST-9"; lblOut = "JST"; return; }
}

// ===== TZ LABEL HELPER =====
// Returns the current local timezone abbreviation (e.g. "AEDT" or "AEST")
// by reading tm_zone from the system clock â€” set correctly by the POSIX TZ string.
// Falls back to a UTC-offset string if tm_zone is unavailable.
const char* getTZLabel() { return configuredTZLabel; }

String normalizeMoonPhase(const String& rawPhase) {
  String p = rawPhase;
  p.toLowerCase();
  p.trim();

  if (p == "new moon" || p == "luna nueva") return "New Moon";
  if (p == "full moon" || p == "luna llena") return "Full Moon";
  if (p == "waxing crescent" || p == "luna creciente") return "Waxing Crescent";
  if (p == "first quarter" || p == "cuarto creciente") return "First Quarter";
  if (p == "waxing gibbous" || p == "gibosa creciente") return "Waxing Gibbous";
  if (p == "waning gibbous" || p == "gibosa menguante") return "Waning Gibbous";
  if (p == "last quarter" || p == "cuarto menguante") return "Last Quarter";
  if (p == "waning crescent" || p == "luna menguante") return "Waning Crescent";
  return rawPhase;
}

String moonPhaseEs(const String& rawPhase) {
  String phase = normalizeMoonPhase(rawPhase);
  if (phase == "New Moon") return "Luna nueva";
  if (phase == "Waxing Crescent") return "Luna creciente";
  if (phase == "First Quarter") return "Cuarto creciente";
  if (phase == "Waxing Gibbous") return "Gibosa creciente";
  if (phase == "Full Moon") return "Luna llena";
  if (phase == "Waning Gibbous") return "Gibosa menguante";
  if (phase == "Last Quarter") return "Cuarto menguante";
  if (phase == "Waning Crescent") return "Luna menguante";
  return rawPhase;
}

// ===== CENTERED TEXT HELPER =====
void printCentered(const GFXfont* font, int y, const char* text) {
  canvas.setFont(font);
  int16_t x1, y1; uint16_t tw, th;
  canvas.getTextBounds(text, 0, y, &x1, &y1, &tw, &th);
  canvas.setCursor((W - tw) / 2 - x1, y);
  canvas.print(text);
}

// ===== CRC8 / SHTC3 =====
uint8_t crc8(const uint8_t *data, int len) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}

bool shtc3_cmd(uint16_t cmd) {
  Wire.beginTransmission(SHTC3_ADDR);
  Wire.write((uint8_t)(cmd >> 8));
  Wire.write((uint8_t)(cmd & 0xFF));
  return Wire.endTransmission() == 0;
}

bool shtc3_read(float &tempC, float &rh) {
  if (!shtc3_cmd(0x3517)) return false;
  delay(50);
  if (!shtc3_cmd(0x7866)) return false;
  delay(20);
  Wire.requestFrom((int)SHTC3_ADDR, 6);
  if (Wire.available() != 6) return false;
  uint8_t d[6];
  for (int i = 0; i < 6; i++) d[i] = Wire.read();
  if (crc8(&d[0], 2) != d[2] || crc8(&d[3], 2) != d[5]) return false;
  uint16_t tRaw  = (uint16_t)d[0] << 8 | d[1];
  uint16_t rhRaw = (uint16_t)d[3] << 8 | d[4];
  tempC = -45.0f + 175.0f * (float)tRaw  / 65536.0f - 4.0f;
  rh    = 100.0f * (float)rhRaw / 65536.0f;
  shtc3_cmd(0xB098);
  return true;
}

// ===== BATTERY =====
float readBatteryVoltage() {
  int raw = analogRead(BAT_ADC_PIN);
  return (raw / 4095.0f) * 3.3f * 3.0f * 1.079f;
}

int batteryToSegments(float vbat) {
  if (vbat >= 4.0f)  return 5;
  if (vbat >= 3.90f) return 4;
  if (vbat >= 3.80f) return 3;
  if (vbat >= 3.65f) return 2;
  if (vbat >= 3.50f) return 1;
  return 0;
}

void updateBatteryChargingState(float vbat, unsigned long nowMs) {
  // Heuristic only: this board wiring does not expose a dedicated CHG/STAT GPIO.
  // We infer charging from short-window voltage trend with hysteresis.
  static bool init = false;
  static float refV = 0.0f;
  static float baseV = 0.0f;
  static unsigned long refMs = 0;
  static uint8_t riseConfirm = 0;
  static uint8_t fallConfirm = 0;
  if (!init) {
    init = true;
    refV = vbat;
    baseV = vbat;
    refMs = nowMs;
    batteryCharging = false;
    return;
  }
  if (nowMs - refMs < 5000UL) return;  // evaluate every 5s

  float dv = vbat - refV;

  // Track a slow baseline while not charging to detect cable-connect lift.
  if (!batteryCharging) {
    baseV = (baseV * 0.85f) + (vbat * 0.15f);
  }

  if (dv >= 0.004f) {
    if (riseConfirm < 6) riseConfirm++;
    fallConfirm = 0;
  } else if (dv <= -0.003f) {
    if (fallConfirm < 6) fallConfirm++;
    riseConfirm = 0;
  } else {
    if (riseConfirm > 0) riseConfirm--;
    if (fallConfirm > 0) fallConfirm--;
  }

  // Enter charging state with either short positive trend or baseline lift.
  if (!batteryCharging) {
    if (riseConfirm >= 2) batteryCharging = true;
    if ((vbat - baseV) >= 0.012f && dv >= 0.001f) batteryCharging = true;
  }

  // Exit quickly on sustained/down trend after unplug.
  if (batteryCharging) {
    if (fallConfirm >= 1) batteryCharging = false;
    if ((nowMs - refMs) >= 30000UL && dv <= 0.0005f) {
      // No meaningful rise for a while: likely not charging.
      batteryCharging = false;
    }
  }

  if (vbat >= 4.22f) batteryCharging = false;      // top-of-charge stabilization
  refV = vbat;
  refMs = nowMs;
}

void drawChargeBoltIcon(int x, int y, bool active) {
  if (!active) return;
  canvas.fillTriangle(x + 2, y, x + 7, y, x + 3, y + 6, 1);
  canvas.fillTriangle(x + 3, y + 6, x + 8, y + 6, x + 2, y + 13, 1);
}

String urlEncode(const String& s) {
  const char* hex = "0123456789ABCDEF";
  String out;
  out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    bool safe = (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) out += (char)c;
    else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}



// ===== WEATHER API =====
bool fetchWeatherData() {
  weatherLastHttpCode = 0;
  if (!wifiConnected) {
    weatherLastError = "Sin WiFi";
    return false;
  }
  if (String(weatherApiKey).length() < 12) {
    weatherLastError = "API key invalida";
    return false;
  }
  HTTPClient http;
  String queryLocation = detectedCity;
  queryLocation.trim();
  if (queryLocation.length() == 0) {
    queryLocation = String(weatherLocation);
    queryLocation.trim();
  }
  if (queryLocation.length() == 0 || queryLocation.equalsIgnoreCase("location city")) queryLocation = "auto:ip";
  String queryParam = (queryLocation == "auto:ip") ? queryLocation : urlEncode(queryLocation);
  String url = "https://api.weatherapi.com/v1/forecast.json?key=" + String(weatherApiKey) +
               "&q=" + queryParam + "&days=3&aqi=yes&alerts=no&lang=es";
  Serial.print("Consulta clima q=");
  Serial.println(queryLocation);
  http.begin(url); http.setTimeout(15000);
  int code = http.GET();
  weatherLastHttpCode = code;
  if (code != 200) {
    String errPayload = http.getString();
    http.end();
    if (code < 0) weatherLastError = "Error red/clima (" + String(code) + ")";
    else weatherLastError = "HTTP clima " + String(code);
    if (errPayload.length() > 0) {
      int msgPos = errPayload.indexOf("\"message\"");
      if (msgPos > 0) {
        int c = errPayload.indexOf(":", msgPos);
        int q1 = errPayload.indexOf("\"", c + 1);
        int q2 = errPayload.indexOf("\"", q1 + 1);
        if (q1 > 0 && q2 > q1) weatherLastError = errPayload.substring(q1 + 1, q2);
      }
    }
    Serial.print("Clima fallo: ");
    Serial.println(weatherLastError);
    return false;
  }
  String payload = http.getString(); http.end();
  if (payload.length() < 300) {
    weatherLastError = "Respuesta clima vacia";
    Serial.println("Clima fallo: respuesta muy corta");
    return false;
  }

  auto pfloat = [&](const char* key, int from, int to) -> float {
    int p = payload.indexOf(key, from);
    if (p < 0 || (to > 0 && p > to)) return 0.0f;
    p += strlen(key);
    int e1 = payload.indexOf(",", p), e2 = payload.indexOf("}", p);
    int e = (e1 > 0 && (e2 < 0 || e1 < e2)) ? e1 : e2;
    String s = payload.substring(p, e); s.trim(); return s.toFloat();
  };
  auto pint = [&](const char* key, int from, int to) -> int {
    int p = payload.indexOf(key, from);
    if (p < 0 || (to > 0 && p > to)) return 0;
    p += strlen(key);
    int e1 = payload.indexOf(",", p), e2 = payload.indexOf("}", p);
    int e = (e1 > 0 && (e2 < 0 || e1 < e2)) ? e1 : e2;
    String s = payload.substring(p, e); s.trim(); return s.toInt();
  };
  auto pstr = [&](const char* key, int from) -> String {
    int p = payload.indexOf(key, from);
    if (p < 0) return "";
    p += strlen(key);
    return payload.substring(p, payload.indexOf("\"", p));
  };

  int curPos   = payload.indexOf("\"current\":");
  int fcastPos = payload.indexOf("\"forecast\":");
  int locPos   = payload.indexOf("\"location\":");
  if (curPos < 0 || fcastPos < 0) {
    weatherLastError = "JSON clima incompleto";
    Serial.println("Clima fallo: JSON incompleto");
    return false;
  }

  if (locPos > 0) {
    String city = pstr("\"name\":\"", locPos);
    if (city.length() > 0) detectedCity = city;

    int latPos = payload.indexOf("\"lat\":", locPos);
    int lonPos = payload.indexOf("\"lon\":", locPos);
    if (latPos > 0 && lonPos > 0 && latPos < curPos && lonPos < curPos) {
      detectedLat = pfloat("\"lat\":", locPos, curPos);
      detectedLon = pfloat("\"lon\":", locPos, curPos);
      detectedLocationValid = true;
    }
  }

  weatherData.currentTemp = pfloat("\"temp_c\":",      curPos, fcastPos);
  weatherData.feelsLike   = pfloat("\"feelslike_c\":", curPos, fcastPos);
  weatherData.humidity    = pint  ("\"humidity\":",    curPos, fcastPos);
  weatherData.windSpeed   = pfloat("\"wind_kph\":",    curPos, fcastPos);
  weatherData.windDir     = pstr  ("\"wind_dir\":\"",  curPos);
  weatherData.condition   = pstr  ("\"text\":\"",      curPos);
  weatherData.precipMM    = pfloat("\"precip_mm\":",   curPos, fcastPos);

  {
    int uvPos = payload.indexOf("\"uv\":", curPos);
    if (uvPos > 0 && uvPos < fcastPos) {
      uvPos += 5;
      int e1 = payload.indexOf(",", uvPos), e2 = payload.indexOf("}", uvPos);
      int e = (e1 > 0 && (e2 < 0 || e1 < e2)) ? e1 : e2;
      String s = payload.substring(uvPos, e); s.trim();
      weatherData.uvIndex = s.toFloat();
    } else weatherData.uvIndex = 0.0f;
  }

  weatherData.airQualityIndex = pint("\"us-epa-index\":", curPos, fcastPos);
  weatherData.pm25            = pfloat("\"pm2_5\":",       curPos, fcastPos);
  switch (weatherData.airQualityIndex) {
    case 1: weatherData.airQualityText = "Buena";            break;
    case 2: weatherData.airQualityText = "Moderada";         break;
    case 3: weatherData.airQualityText = "Insalubre+";       break;
    case 4: weatherData.airQualityText = "Insalubre";        break;
    case 5: weatherData.airQualityText = "Muy insalubre";    break;
    case 6: weatherData.airQualityText = "Peligrosa";        break;
    default:weatherData.airQualityText = "Desconocida";      break;
  }

  int arrPos = payload.indexOf("\"forecastday\":[");
  if (arrPos > 0) {
    int sPos = arrPos;
    for (int i = 0; i < 3; i++) {
      int p = payload.indexOf("\"date\":\"", sPos); if (p < 0) break;
      p += 8; int e = payload.indexOf("\"", p);
      weatherData.forecast[i].day = payload.substring(p, e); sPos = e;
      int dObj = payload.indexOf("\"day\":{", sPos); if (dObj < 0) break;
      weatherData.forecast[i].maxTemp    = pfloat("\"maxtemp_c\":",        dObj, dObj+2000);
      weatherData.forecast[i].minTemp    = pfloat("\"mintemp_c\":",        dObj, dObj+2000);
      weatherData.forecast[i].precipMM   = pfloat("\"totalprecip_mm\":",   dObj, dObj+2000);
      weatherData.forecast[i].rainChance = pint  ("\"daily_chance_of_rain\":", dObj, dObj+2000);
      int cPos = payload.indexOf("\"condition\":", dObj);
      if (cPos > 0) { weatherData.forecast[i].condition = pstr("\"text\":\"", cPos); sPos = cPos+100; }
      else sPos = dObj+100;
    }
  }

  int astroPos = payload.indexOf("\"astronomy\":");
  if (astroPos < 0) astroPos = payload.indexOf("\"astro\":");
  if (astroPos > 0) {
    astroData.sunrise          = pstr("\"sunrise\":\"",    astroPos);
    astroData.sunset           = pstr("\"sunset\":\"",     astroPos);
    astroData.moonrise         = pstr("\"moonrise\":\"",   astroPos);
    astroData.moonset          = pstr("\"moonset\":\"",    astroPos);
    astroData.moonPhase        = pstr("\"moon_phase\":\"", astroPos);
    astroData.moonIllumination = pint("\"moon_illumination\":", astroPos, astroPos+500);
    astroData.valid = true;
  }

  int hourPos = payload.indexOf("\"hour\":[");
  if (hourPos > 0) {
    int sPos = hourPos, found = 0;
    for (int h = 0; h < 24 && found < 6; h++) {
      int tp = payload.indexOf("\"time\":\"", sPos); if (tp < 0) break;
      tp += 8; int te = payload.indexOf("\"", tp);
      String ts = payload.substring(tp, te);
      int hv = ts.substring(11, 13).toInt(); sPos = te;
      if (hv < hour24) continue;
      hourlyData.time[found]       = ts.substring(11, 16);
      hourlyData.temp[found]       = pfloat("\"temp_c\":",         sPos, sPos+2000);
      hourlyData.rainChance[found] = pint  ("\"chance_of_rain\":", sPos, sPos+2000);
      hourlyData.rainMM[found]     = pfloat("\"precip_mm\":",      sPos, sPos+2000);
      hourlyData.uvIndex[found]    = pfloat("\"uv\":",             sPos, sPos+2000);
      hourlyData.windSpeed[found]  = pfloat("\"wind_kph\":",       sPos, sPos+2000);
      found++;
    }
    hourlyData.valid = (found > 0);
  }

  weatherData.lastUpdate = millis();
  weatherData.valid = true;
  weatherLastError = "OK";
  return true;
}

// ===== DISPLAY HELPERS =====
void pushCanvasToRLCD(bool invert = false) {
  uint8_t *buf = canvas.getBuffer();
  const int bpr = (W + 7) / 8;
  RlcdPort.RLCD_ColorClear(ColorWhite);
  bool inv = (invert ^ displayInvertMode);
  for (int y = 0; y < H; y++) {
    uint8_t *row = buf + y * bpr;
    for (int bx = 0; bx < bpr; bx++) {
      uint8_t v = inv ? row[bx] ^ 0xFF : row[bx];
      int x0 = bx * 8;
      for (int bit = 0; bit < 8; bit++) {
        int x = x0 + bit; if (x >= W) break;
        if (v & (0x80 >> bit)) RlcdPort.RLCD_SetPixel((uint16_t)x, (uint16_t)y, ColorBlack);
      }
    }
  }
  RlcdPort.RLCD_Display();
}

void applyPowerProfile() {
  if (batterySaveMode) {
    setCpuFrequencyMhz(80);
    WiFi.setSleep(true);
    currentPage = 0;
    if (radioPlaying || radioStartRequested) {
      radioUserStopped = true;
      radioStartRequested = false;
      stopRadioPlayback();
      restoreBeepAudioEngine();
    }
  } else {
    setCpuFrequencyMhz(240);
    WiFi.setSleep(false);
  }
}

void clearPhotoCache() {
  if (photoJpegData != nullptr) {
    free(photoJpegData);
    photoJpegData = nullptr;
  }
  photoJpegLen = 0;
  photoValid = false;
}

bool validateUploadedPhoto(const uint8_t* data, size_t len, String& err) {
  err = "";
  if (!data || len < 4) { err = "archivo vacio"; return false; }
  if (data[0] != 0xFF || data[1] != 0xD8) { err = "no es JPEG"; return false; }
  if (data[len - 2] != 0xFF || data[len - 1] != 0xD9) { err = "JPEG incompleto"; return false; }
  int sof = jpegSofMarker(data, len);
  if (sof == 0xC2) { err = "JPEG progresivo no soportado"; return false; }

  uint16_t wj = 0, hj = 0;
  JRESULT jr = TJpgDec.getJpgSize(&wj, &hj, data, (uint32_t)len);
  if (jr != JDR_OK) { err = "JPEG invalido"; return false; }
  if (wj != 400 || hj != 272) {
    err = "tamano invalido (esperado 400x272)";
    return false;
  }
  return true;
}

bool loadUploadedPhotoFromFs(String& err) {
  err = "";
  if (!SPIFFS.begin(true)) { err = "SPIFFS"; return false; }
  if (!SPIFFS.exists(kUploadedPhotoPath)) { err = "sin archivo"; return false; }
  File f = SPIFFS.open(kUploadedPhotoPath, FILE_READ);
  if (!f) { err = "no se puede abrir"; return false; }
  size_t n = (size_t)f.size();
  if (n == 0 || n > kPhotoMaxBytes) { f.close(); err = "tamano invalido"; return false; }
  uint8_t* buf = photoAlloc(n);
  if (!buf) { f.close(); err = "sin memoria"; return false; }
  size_t r = f.read(buf, n);
  f.close();
  if (r != n) { free(buf); err = "lectura incompleta"; return false; }
  if (!validateUploadedPhoto(buf, n, err)) { free(buf); return false; }
  clearPhotoCache();
  photoJpegData = buf;
  photoJpegLen = n;
  photoValid = true;
  photoLastFetch = millis();
  photoLastError = "OK";
  return true;
}

bool photoRenderCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  for (uint16_t yy = 0; yy < h; yy++) {
    int py = y + (int)yy;
    if (py < 0 || py >= H) continue;
    for (uint16_t xx = 0; xx < w; xx++) {
      int px = x + (int)xx;
      if (px < 0 || px >= W) continue;
      uint16_t c = bitmap[yy * w + xx];
      int r = ((c >> 11) & 0x1F) * 255 / 31;
      int g = ((c >> 5) & 0x3F) * 255 / 63;
      int b = (c & 0x1F) * 255 / 31;
      int lum = (r * 30 + g * 59 + b * 11) / 100;
      if (lum < 128) canvas.drawPixel(px, py, 1);
    }
  }
  return true;
}

void drawThermometerIcon(int x, int y) {
  canvas.drawCircle(x+3, y+18, 4, 1); canvas.fillCircle(x+3, y+18, 2, 1);
  canvas.fillRect(x+1, y, 4, 16, 1);  canvas.fillRect(x+2, y, 2, 16, 0);
}

void drawDropletIcon(int x, int y) {
  canvas.fillCircle(x+4, y+10, 4, 1);
  canvas.fillTriangle(x+4, y, x, y+8, x+8, y+8, 1);
  canvas.fillCircle(x+4, y+10, 2, 0);
}

void drawWiFiIcon(int x, int y, int rssi) {
  int bars = 0;
  if      (rssi > -50) bars = 4;
  else if (rssi > -60) bars = 3;
  else if (rssi > -70) bars = 2;
  else if (rssi > -80) bars = 1;
  for (int i = 0; i < 4; i++) {
    int h = (i+1) * 4;
    if (i < bars && wifiConnected) canvas.fillRect(x+i*6, y+16-h, 4, h, 1);
    else                           canvas.drawRect(x+i*6, y+16-h, 4, h, 1);
  }
}

void drawWiFiBarsCompact(int x, int y, int rssi) {
  int bars = 0;
  if      (rssi > -50) bars = 4;
  else if (rssi > -60) bars = 3;
  else if (rssi > -70) bars = 2;
  else if (rssi > -80) bars = 1;
  for (int i = 0; i < 4; i++) {
    int h = (i + 1) * 3;
    int bx = x + i * 5;
    int by = y + 12 - h;
    if (i < bars) canvas.fillRect(bx, by, 3, h, 1);
    else canvas.drawRect(bx, by, 3, h, 1);
  }
}

String wifiEncShort(uint8_t enc) {
  switch (enc) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA12";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "ENT";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA23";
    case WIFI_AUTH_WAPI_PSK: return "WAPI";
    default: return "UNK";
  }
}

void updateWifiScan(bool force = false) {
  unsigned long now = millis();
  if (!force && wifiScanLast > 0 && (now - wifiScanLast) < 30000UL && wifiScanCount > 0) return;
  if (wifiScanInProgress) return;

  int running = WiFi.scanComplete();
  if (running == WIFI_SCAN_RUNNING) return;
  if (running >= 0) WiFi.scanDelete();

  // Start async scan; results will be collected by drawWifiNetworksPage()
  WiFi.scanNetworks(true);
  wifiScanInProgress = true;
}

bool pollWifiScanResults() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING || n == WIFI_SCAN_FAILED) return false;

  wifiScanLast = millis();
  wifiScanCount = 0;
  if (n <= 0) {
    WiFi.scanDelete();
    wifiScanInProgress = false;
    return false;
  }

  for (int i = 0; i < n && wifiScanCount < WIFI_SCAN_MAX; i++) {
    String s = WiFi.SSID(i);
    s.trim();
    if (s.length() == 0) s = "(oculta)";
    wifiScanList[wifiScanCount].ssid = s;
    wifiScanList[wifiScanCount].rssi = WiFi.RSSI(i);
    wifiScanList[wifiScanCount].enc = (uint8_t)WiFi.encryptionType(i);
    wifiScanCount++;
  }
  WiFi.scanDelete();
  wifiScanInProgress = false;

  for (int i = 0; i < wifiScanCount - 1; i++) {
    for (int j = i + 1; j < wifiScanCount; j++) {
      if (wifiScanList[j].rssi > wifiScanList[i].rssi) {
        WifiScanEntry t = wifiScanList[i];
        wifiScanList[i] = wifiScanList[j];
        wifiScanList[j] = t;
      }
    }
  }
  return true;
}

static uint8_t* photoAlloc(size_t n) {
  if (psramFound()) {
    uint8_t* p = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
  }
  return (uint8_t*)malloc(n);
}

static int jpegSofMarker(const uint8_t* b, size_t n) {
  if (!b || n < 4) return -1;
  size_t j = 2;
  while (j + 3 < n) {
    if (b[j] != 0xFF) { j++; continue; }
    size_t k = j + 1;
    while (k < n && b[k] == 0xFF) k++;
    if (k >= n) break;
    uint8_t m = b[k];
    if ((m >= 0xC0 && m <= 0xC3) || (m >= 0xC5 && m <= 0xC7) || (m >= 0xC9 && m <= 0xCB) || (m >= 0xCD && m <= 0xCF)) {
      return (int)m;
    }
    if (m == 0xD8 || m == 0xD9 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { j = k + 1; continue; }
    if (k + 2 >= n) break;
    uint16_t segLen = ((uint16_t)b[k + 1] << 8) | b[k + 2];
    if (segLen < 2) break;
    j = k + 1 + segLen;
  }
  return -1;
}

static bool downloadPhotoUrl(const String& url, uint8_t** outBuf, size_t* outLen, String& err) {
  *outBuf = nullptr;
  *outLen = 0;
  err = "";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, url)) {
    err = "No se pudo iniciar descarga";
    return false;
  }

  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(12000);
  http.addHeader("Accept", "image/jpeg");
  http.setUserAgent("ESP32-Cat-Display/1.0");

  int code = http.GET();
  if (code != 200) {
    err = (code < 0) ? "Error red foto" : ("HTTP foto " + String(code));
    http.end();
    return false;
  }

  int len = http.getSize();
  uint8_t* buf = nullptr;
  const size_t cap = kPhotoMaxBytes;
  size_t targetLen = 0;

  if (len > 0) {
    if ((size_t)len > kPhotoMaxBytes) {
      err = "Tamano foto invalido";
      http.end();
      return false;
    }
    targetLen = (size_t)len;
  }

  buf = photoAlloc(cap);
  if (!buf) {
    err = "Sin memoria foto";
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  size_t received = 0;
  unsigned long lastDataMs = millis();
  while (http.connected() && (targetLen == 0 || received < targetLen)) {
    size_t avail = stream->available();
    if (avail > 0) {
      size_t toRead = avail;
      if (targetLen > 0) {
        size_t need = targetLen - received;
        if (toRead > need) toRead = need;
      }
      if (received + toRead > cap) {
        free(buf);
        err = "Foto muy grande";
        http.end();
        return false;
      }
      int r = stream->readBytes(buf + received, toRead);
      if (r > 0) {
        received += (size_t)r;
        lastDataMs = millis();
      }
    } else {
      delay(2);
      if (millis() - lastDataMs > 3000) {
        if (targetLen == 0) break;
        if (received >= targetLen) break;
      }
    }
  }
  http.end();

  if (received == 0 || (targetLen > 0 && received != targetLen)) {
    free(buf);
    err = "Foto incompleta";
    return false;
  }

  if (received < 4 || buf[0] != 0xFF || buf[1] != 0xD8) {
    free(buf);
    err = "Formato no JPEG";
    return false;
  }

  int sof = jpegSofMarker(buf, received);
  if (sof == 0xC2) {
    free(buf);
    err = "JPEG progresivo";
    return false;
  }
  if (received < 4 || buf[received - 2] != 0xFF || buf[received - 1] != 0xD9) {
    free(buf);
    err = "JPEG incompleto";
    return false;
  }

  *outBuf = buf;
  *outLen = received;
  return true;
}

bool fetchPhotoBackground() {
  if (photoUseUploaded) {
    String err;
    bool ok = loadUploadedPhotoFromFs(err);
    if (!ok) photoLastError = "Imagen fija: " + err;
    return ok;
  }
  if (!wifiConnected) return false;

  char url0[220];
  char url1[220];
  char url2[220];
  unsigned long seed = (unsigned long)(esp_random() ^ millis());
  String theme = photoTheme;
  theme.trim();
  if (theme.length() == 0) theme = "anime,girl";

  if (theme.equalsIgnoreCase("grayscale")) {
    snprintf(url0, sizeof(url0), "https://picsum.photos/400/272?grayscale&random=%lu", seed ^ 0xA5A5A5A5UL);
    snprintf(url1, sizeof(url1), "https://loremflickr.com/g/400/272/monochrome?lock=%lu", seed);
    snprintf(url2, sizeof(url2), "https://loremflickr.com/g/400/272/black-and-white?lock=%lu", seed + 12345UL);
  } else {
    snprintf(url0, sizeof(url0), "https://loremflickr.com/g/400/272/%s?lock=%lu", theme.c_str(), seed ^ 0xA5A5A5A5UL);
    snprintf(url1, sizeof(url1), "https://loremflickr.com/g/400/272/%s?lock=%lu", theme.c_str(), seed);
    snprintf(url2, sizeof(url2), "https://loremflickr.com/g/400/272/%s,manga?lock=%lu", theme.c_str(), seed + 12345UL);
  }

  // All candidates use lock so each entry requests different images.
  String candidates[3] = {String(url0), String(url1), String(url2)};
  String lastErr = "Sin imagen";

  for (int i = 0; i < 3; i++) {
    uint8_t* newBuf = nullptr;
    size_t newLen = 0;
    String err;
    if (downloadPhotoUrl(candidates[i], &newBuf, &newLen, err)) {
      clearPhotoCache();
      photoJpegData = newBuf;
      photoJpegLen = newLen;
      photoValid = true;
      photoLastFetch = millis();
      photoLastError = "OK";
      return true;
    }
    lastErr = err;
  }

  photoLastError = lastErr;
  return false;
}

static bool es8311WriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(0x18);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool es8311ReadReg(uint8_t reg, uint8_t* value) {
  Wire.beginTransmission(0x18);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)0x18, 1) != 1) return false;
  *value = Wire.read();
  return true;
}

static bool es8311InitPlayback80() {
  uint8_t regv = 0;
  uint8_t chipId1 = 0;
  uint8_t chipId2 = 0;
  if (!es8311ReadReg(0xFD, &chipId1) || !es8311ReadReg(0xFE, &chipId2)) {
    Serial.println("[AUDIO] ES8311 no responde por I2C");
    return false;
  }

  // Open sequence (from Espressif ES8311 driver)
  bool ok = true;
  ok &= es8311WriteReg(0x44, 0x08);
  ok &= es8311WriteReg(0x44, 0x08);
  ok &= es8311WriteReg(0x01, 0x30);
  ok &= es8311WriteReg(0x02, 0x00);
  ok &= es8311WriteReg(0x03, 0x10);
  ok &= es8311WriteReg(0x16, 0x24);
  ok &= es8311WriteReg(0x04, 0x10);
  ok &= es8311WriteReg(0x05, 0x00);
  ok &= es8311WriteReg(0x0B, 0x00);
  ok &= es8311WriteReg(0x0C, 0x00);
  ok &= es8311WriteReg(0x10, 0x1F);
  ok &= es8311WriteReg(0x11, 0x7F);
  ok &= es8311WriteReg(0x00, 0x80);
  if (!ok) {
    Serial.println("[AUDIO] fallo secuencia OPEN ES8311");
    return false;
  }

  if (!es8311ReadReg(0x00, &regv)) return false;
  regv &= 0xBF;  // slave mode
  if (!es8311WriteReg(0x00, regv)) return false;

  regv = 0x3F;   // use MCLK, no invert
  regv &= ~0x40;
  if (!es8311WriteReg(0x01, regv)) return false;

  if (!es8311ReadReg(0x06, &regv)) return false;
  regv &= ~0x20; // SCLK not inverted
  if (!es8311WriteReg(0x06, regv)) return false;

  ok = true;
  ok &= es8311WriteReg(0x13, 0x10);
  ok &= es8311WriteReg(0x1B, 0x0A);
  ok &= es8311WriteReg(0x1C, 0x6A);
  ok &= es8311WriteReg(0x44, 0x58);
  if (!ok) return false;

  // Fixed sample config for 16kHz with 4.096MHz MCLK (div=256)
  if (!es8311ReadReg(0x02, &regv)) return false;
  regv &= 0x07;
  regv |= (0 << 5);  // pre_div = 1
  regv |= (0 << 3);  // pre_multi = 1
  if (!es8311WriteReg(0x02, regv)) return false;
  if (!es8311WriteReg(0x05, 0x00)) return false;  // adc_div=1 dac_div=1

  if (!es8311ReadReg(0x03, &regv)) return false;
  regv &= 0x80;
  regv |= 0x10;  // adc_osr
  if (!es8311WriteReg(0x03, regv)) return false;

  if (!es8311ReadReg(0x04, &regv)) return false;
  regv &= 0x80;
  regv |= 0x20;  // dac_osr
  if (!es8311WriteReg(0x04, regv)) return false;

  if (!es8311ReadReg(0x07, &regv)) return false;
  regv &= 0xC0;  // lrck_h=0
  if (!es8311WriteReg(0x07, regv)) return false;
  if (!es8311WriteReg(0x08, 0xFF)) return false;  // lrck_l

  if (!es8311ReadReg(0x06, &regv)) return false;
  regv &= 0xE0;
  regv |= 0x03; // bclk_div = 4 => reg value 3
  if (!es8311WriteReg(0x06, regv)) return false;

  // I2S + 16-bit on SDP
  uint8_t dacIface = 0;
  uint8_t adcIface = 0;
  if (!es8311ReadReg(0x09, &dacIface) || !es8311ReadReg(0x0A, &adcIface)) return false;
  dacIface |= 0x0C;
  adcIface |= 0x0C;
  dacIface &= 0xFC;
  adcIface &= 0xFC;
  if (!es8311WriteReg(0x09, dacIface) || !es8311WriteReg(0x0A, adcIface)) return false;

  // Enable playback path (DAC mode)
  if (!es8311ReadReg(0x09, &dacIface) || !es8311ReadReg(0x0A, &adcIface)) return false;
  dacIface &= 0xBF;
  adcIface &= 0xBF;
  adcIface |= 0x40;   // keep ADC muted/off
  if (!es8311WriteReg(0x09, dacIface) || !es8311WriteReg(0x0A, adcIface)) return false;

  ok = true;
  ok &= es8311WriteReg(0x17, 0xBF);
  ok &= es8311WriteReg(0x0E, 0x02);
  ok &= es8311WriteReg(0x12, 0x00);
  ok &= es8311WriteReg(0x14, 0x1A);
  if (!ok) return false;

  if (!es8311ReadReg(0x14, &regv)) return false;
  regv &= ~0x40; // DMIC off
  if (!es8311WriteReg(0x14, regv)) return false;

  ok = true;
  ok &= es8311WriteReg(0x0D, 0x01);
  ok &= es8311WriteReg(0x15, 0x40);
  ok &= es8311WriteReg(0x37, 0x08);
  ok &= es8311WriteReg(0x45, 0x00);
  if (!ok) return false;

  // Volume 80%
  const int effPct = getEffectiveAudioPercent();
  const uint8_t dacVol = (uint8_t)roundf(255.0f * ((float)effPct / 100.0f));
  if (!es8311WriteReg(0x32, dacVol)) return false;
  if (!es8311ReadReg(0x31, &regv)) return false;
  regv &= 0x9F; // unmute
  if (!es8311WriteReg(0x31, regv)) return false;

  Serial.printf("[AUDIO] ES8311 OK (ID %02X %02X, VOL %u)\n", chipId1, chipId2, (unsigned)dacVol);
  return true;
}

void applyAudioVolumeToCodec() {
  if (!codecReady) return;
  int vol = getEffectiveAudioPercent();
  if (vol < 0) vol = 0;
  if (vol > 100) vol = 100;
  uint8_t dacVol = (uint8_t)roundf(255.0f * ((float)vol / 100.0f));
  es8311WriteReg(0x32, dacVol);
}

void applyRadioVolume() {
  int v = radioVolumePct;
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  // Linear 1:1 mapping: UI 100% == real 100%.
  int mapped = v;
  if (mapped < 0) mapped = 0;
  if (mapped > 100) mapped = 100;
  if (radioOut) {
    float gain = ((float)mapped / 100.0f) * 1.15f;
    radioOut->SetGain(gain);
  }
}

void stopRadioPlayback() {
  if (radioMp3) {
    radioMp3->stop();
    delete radioMp3;
    radioMp3 = nullptr;
  }
  if (radioAac) {
    radioAac->stop();
    delete radioAac;
    radioAac = nullptr;
  }
  if (radioBuff) {
    delete radioBuff;
    radioBuff = nullptr;
  }
  if (radioFile) {
    delete radioFile;
    radioFile = nullptr;
  }
  if (radioOut) {
    delete radioOut;
    radioOut = nullptr;
  }
  radioPlaying = false;
  radioStatus = "Detenida";
  radioNowPlaying = "";
}

void restoreBeepAudioEngine() {
  // Recreate beep I2S/codec path after radio teardown so alerts work immediately.
  initBootAudio();
  applyAudioVolumeToCodec();
}

bool startRadioPlayback() {
  if (!radioEngineReady) return false;
  String playUrl = normalizeRadioStreamUrlForDevice(radioStationUrl);
  if (playUrl.length() == 0) {
    radioStatus = "Sin emisora";
    radioPlaying = false;
    return false;
  }
  // Avoid two I2S engines driving the same codec/pins at once (beeps vs radio stream).
  if (audioReady) {
    i2sAudio.end();
    audioReady = false;
  }
  Serial.printf("[RADIO] connecttohost: %s\n", playUrl.c_str());
  stopRadioPlayback();

  radioFile = new AudioFileSourceICYStream(playUrl.c_str());
  const int rbSizes[] = {112, 96, 80, 64};
  for (size_t i = 0; i < sizeof(rbSizes) / sizeof(rbSizes[0]); ++i) {
    radioBuff = new AudioFileSourceBuffer(radioFile, rbSizes[i] * 1024);
    if (radioBuff) {
      Serial.printf("[RADIO] buffer=%dKB freeHeap=%u\n", rbSizes[i], (unsigned)ESP.getFreeHeap());
      break;
    }
  }
  if (!radioBuff) {
    radioStatus = "Sin memoria buffer";
    radioPlaying = false;
    delete radioFile;
    radioFile = nullptr;
    return false;
  }
  radioOut = new AudioOutputI2S();
  if (!radioOut) {
    radioStatus = "Sin memoria I2S";
    radioPlaying = false;
    delete radioBuff; radioBuff = nullptr;
    delete radioFile; radioFile = nullptr;
    return false;
  }
  radioOut->SetPinout(I2S_BCLK_PIN, I2S_LRCLK_PIN, I2S_DOUT_PIN);
  radioOut->SetOutputModeMono(true);
  applyRadioVolume();

  radioFile->RegisterMetadataCB(radioMetadataCb, (void*)"ICY");
  radioBuff->RegisterStatusCB(radioStatusCb, (void*)"BUF");

  bool ok = false;
  String lurl = playUrl;
  String lcodec = radioStationCodec;
  lurl.toLowerCase();
  lcodec.toLowerCase();
  bool preferAac = (lcodec.indexOf("aac") >= 0 || lurl.indexOf(".aac") >= 0 || lurl.indexOf(".m4a") >= 0);

  auto beginAac = [&]() -> bool {
    if (radioMp3) { delete radioMp3; radioMp3 = nullptr; }
    if (radioAac) { delete radioAac; radioAac = nullptr; }
    radioAac = new AudioGeneratorAAC();
    if (!radioAac) return false;
    radioAac->RegisterStatusCB(radioStatusCb, (void*)"AAC");
    return radioAac->begin(radioBuff, radioOut);
  };

  auto beginMp3 = [&]() -> bool {
    if (radioAac) { delete radioAac; radioAac = nullptr; }
    if (radioMp3) { delete radioMp3; radioMp3 = nullptr; }
    radioMp3 = new AudioGeneratorMP3();
    if (!radioMp3) return false;
    radioMp3->RegisterStatusCB(radioStatusCb, (void*)"MP3");
    return radioMp3->begin(radioBuff, radioOut);
  };

  ok = preferAac ? beginAac() : beginMp3();
  if (!ok) ok = preferAac ? beginMp3() : beginAac();

  radioPlaying = ok;
  radioStatus = ok ? "Conectando..." : "Error de stream";
  radioDecoderErrorBurst = 0;
  radioDecoderErrorWindowStart = 0;
  radioConnectStartMs = millis();
  radioLastEventMs = millis();
  if (!ok) radioNowPlaying = "";
  return ok;
}

String normalizeRadioStreamUrlForDevice(const String& url) {
  String u = url;
  u.trim();
  if (u.length() == 0) return u;

  String lu = u;
  lu.toLowerCase();

  // ESP8266Audio ICY stream is more reliable on plain HTTP for StreamTheWorld.
  if (lu.startsWith("https://playerservices.streamtheworld.com/")) {
    u.remove(0, 8);
    u = "http://" + u;
    return u;
  }

  if (lu.startsWith("https://") && lu.indexOf(".live.streamtheworld.com") > 0) {
    u.remove(0, 8);
    u = "http://" + u;
    return u;
  }

  return u;
}

void radioMetadataCb(void* cbData, const char* type, bool isUnicode, const char* str) {
  (void)cbData;
  (void)isUnicode;
  if (!type || !str) return;
  String t(type), s(str);
  t.trim();
  s.trim();
  if (s.length() == 0) return;
  radioLastEventMs = millis();
  if (t.equalsIgnoreCase("StreamTitle") || t.equalsIgnoreCase("icy-name")) {
    radioNowPlaying = s;
  }
  radioStatus = "Reproduciendo";
  Serial.printf("[RADIO][meta] %s = %s\n", t.c_str(), s.c_str());
}

void radioStatusCb(void* cbData, int code, const char* str) {
  const char* tag = cbData ? reinterpret_cast<const char*>(cbData) : "RAD";
  char msg[96] = {0};
  if (str) {
    strncpy(msg, str, sizeof(msg) - 1);
  }
  Serial.printf("[RADIO][%s] code=%d %s\n", tag, code, msg);
  radioLastEventMs = millis();
  if (code < 0) {
    unsigned long now = millis();
    if (radioDecoderErrorWindowStart == 0 || (now - radioDecoderErrorWindowStart) > 3000UL) {
      radioDecoderErrorWindowStart = now;
      radioDecoderErrorBurst = 1;
    } else {
      radioDecoderErrorBurst++;
    }
  }
}

void serviceRadioPlayback() {
  if (!radioPlaying) return;
  const uint32_t kMinHeapSafe = 70000U;
  uint32_t freeHeapNow = ESP.getFreeHeap();
  if (freeHeapNow < kMinHeapSafe) {
    Serial.printf("[RADIO] low heap guard: %u bytes, stopping stream\n", (unsigned)freeHeapNow);
    stopRadioPlayback();
    radioStatus = "Memoria baja";
    radioUserStopped = true;
    radioStartRequested = false;
    return;
  }
  if (radioDecoderErrorBurst >= 3) {
    Serial.printf("[RADIO] unstable stream guard: burst=%d\n", radioDecoderErrorBurst);
    stopRadioPlayback();
    radioStatus = "Stream inestable";
    radioUserStopped = true;
    radioStartRequested = false;
    return;
  }
  bool running = false;
  if (radioMp3) running = radioMp3->loop();
  else if (radioAac) running = radioAac->loop();
  if (!running) {
    radioPlaying = false;
    radioStatus = "Stream finalizado";
  } else {
    // Keep alive for streams without frequent metadata updates.
    radioLastEventMs = millis();
    if (radioStatus == "Conectando..." && (millis() - radioConnectStartMs) > 2500UL) {
      radioStatus = "Reproduciendo";
    }
  }
}

bool preflightRadioUrl(const String& url, String& why) {
  why = "";
  String u = url;
  u.trim();
  if (u.length() < 8) {
    why = "URL vacia";
    return false;
  }
  String lu = u;
  lu.toLowerCase();
  if (!(lu.startsWith("http://") || lu.startsWith("https://"))) {
    why = "URL invalida";
    return false;
  }
  HTTPClient http;
  http.setConnectTimeout(2500);
  http.setTimeout(2500);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(u)) {
    why = "No abre URL";
    return false;
  }
  int code = http.GET();
  if (code < 0) {
    why = "Sin respuesta";
    http.end();
    return false;
  }
  if (code >= 400) {
    why = "HTTP " + String(code);
    http.end();
    return false;
  }
  String ct = http.header("Content-Type");
  ct.toLowerCase();
  // Permit generic streams even if servers omit content-type, but reject clear HTML pages.
  if (ct.indexOf("text/html") >= 0) {
    why = "No es stream";
    http.end();
    return false;
  }
  http.end();
  return true;
}

bool initBootAudio() {
  pinMode(SPK_EN_PIN, OUTPUT);
  digitalWrite(SPK_EN_PIN, HIGH);  // enable speaker amp
  delay(5);
  i2sAudio.setPins(I2S_BCLK_PIN, I2S_LRCLK_PIN, I2S_DOUT_PIN, -1, I2S_MCLK_PIN);
  audioReady = i2sAudio.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  if (!audioReady) {
    Serial.println("[AUDIO] I2S no inicializo");
    return false;
  }
  codecReady = es8311InitPlayback80();
  if (!codecReady) {
    Serial.println("[AUDIO] ES8311 no inicializo");
    return false;
  }
  return true;
}

void audioSilenceMs(uint16_t ms) {
  if (!audioReady || ms == 0) return;
  const int sampleRate = 16000;
  int totalSamples = (sampleRate * (int)ms) / 1000;
  uint8_t frame[4] = {0, 0, 0, 0};  // L16 + R16
  for (int i = 0; i < totalSamples; i++) i2sAudio.write(frame, sizeof(frame));
}

void playToneHz(uint16_t freq, uint16_t ms, int16_t amp = 2800) {
  if (!audioReady || freq < 20 || ms == 0 || radioPlaying) return;
  const int sampleRate = 16000;
  int totalSamples = (sampleRate * (int)ms) / 1000;
  int halfWave = max(1, sampleRate / ((int)freq * 2));
  int scaledAmp = (int)amp * getEffectiveAudioPercent() / 100;
  if (scaledAmp < 200) scaledAmp = 200;
  if (scaledAmp > 30000) scaledAmp = 30000;
  int16_t sample = (int16_t)scaledAmp;
  uint8_t frame[4];
  for (int i = 0; i < totalSamples; i++) {
    if ((i % halfWave) == 0) sample = -sample;  // square wave
    frame[0] = (uint8_t)(sample & 0xFF);
    frame[1] = (uint8_t)((sample >> 8) & 0xFF);
    frame[2] = frame[0];
    frame[3] = frame[1];
    i2sAudio.write(frame, sizeof(frame));
  }
}

void playVolumeConfirmBeep() {
  // Short confirmation beep for remote/local volume updates.
  playToneHz(1450, 55);
  audioSilenceMs(15);
}

void bootBeepOk() {
  playToneHz(1700, 60);
  audioSilenceMs(25);
}

void bootBeepFail() {
  playToneHz(1100, 80);
  audioSilenceMs(30);
  playToneHz(720, 150);  // descending double tone
  audioSilenceMs(40);
}

void bootR2D2AllOk() {
  // Simple fanfare: "tin, tiririiin"
  const uint16_t notes[] = {1568, 1318, 1760, 2093};
  const uint16_t durs[]  = {  90,   70,   90,  220};
  for (int i = 0; i < 4; i++) {
    playToneHz(notes[i], durs[i]);
    audioSilenceMs(i == 0 ? 120 : 30);
  }
}


// ===== PAGE 0: DASHBOARD =====
void drawDashboardPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1);
  canvas.drawRect(1, 1, W-2, H-2, 1);

  int dow = rtc.getWeekday();
  canvas.drawRect(8, 8, 384, 38, 1);
  drawWiFiIcon(15, 18, wifiRSSI);
  canvas.setFont(&FONT_MEDIUM); canvas.setTextColor(1);
  canvas.setCursor(48, 33);
  if (wifiConnected) canvas.print(wifiRSSI); else canvas.print("--");
  canvas.fillRect(130, 12, 2, 30, 1);

  canvas.setFont(&FONT_MEDIUM); canvas.setCursor(150, 35);
  canvas.print(getTZLabel());

  canvas.fillRect(240, 12, 2, 30, 1);
  int batX = 310;
  canvas.drawRect(batX, 18, 60, 20, 1);
  canvas.fillRect(batX+60, 24, 3, 8, 1);
  int segs = batteryToSegments(batteryVoltage);
  for (int i = 0; i < segs; i++) canvas.fillRect(batX+2+(i*11), 20, 10, 16, 1);
  canvas.setFont(&FONT_MEDIUM); canvas.setCursor(250, 33);
  canvas.print(batteryVoltage, 1); canvas.print("V");

  // Temperature box
  canvas.drawRect(12, 55, 185, 78, 1); canvas.drawRect(13, 56, 183, 76, 1);
  drawThermometerIcon(20, 67);
  canvas.setFont(&FONT_SMALL); canvas.setCursor(45, 76); canvas.print("TEMPERATURA");
  String tempValue = String(temperature, 1);
  int16_t tx1, ty1, cx1, cy1;
  uint16_t tw, th, cw, ch;
  canvas.setFont(&FONT_XLARGE);
  canvas.getTextBounds(tempValue, 0, 0, &tx1, &ty1, &tw, &th);
  canvas.setFont(&FONT_MEDIUM);
  canvas.getTextBounds("C", 0, 0, &cx1, &cy1, &cw, &ch);
  int tBlockW = (int)tw + 6 + (int)cw;
  int tStartX = 12 + (185 - tBlockW) / 2;
  int tBaseY = 122;
  canvas.setFont(&FONT_XLARGE); canvas.setCursor(tStartX, tBaseY); canvas.print(tempValue);
  canvas.setFont(&FONT_MEDIUM); canvas.setCursor(tStartX + tw + 6, tBaseY); canvas.print("C");

  // Humidity box
  canvas.drawRect(203, 55, 185, 78, 1); canvas.drawRect(204, 56, 183, 76, 1);
  drawDropletIcon(211, 67);
  canvas.setFont(&FONT_SMALL); canvas.setCursor(237, 76); canvas.print("HUMEDAD");
  String humValue = String((int)humidity);
  int16_t vx1, vy1, px1, py1;
  uint16_t vw, vh, pw, ph;
  canvas.setFont(&FONT_XLARGE);
  canvas.getTextBounds(humValue, 0, 0, &vx1, &vy1, &vw, &vh);
  canvas.setFont(&FONT_MEDIUM);
  canvas.getTextBounds("%", 0, 0, &px1, &py1, &pw, &ph);
  int blockW = (int)vw + 6 + (int)pw;
  int startX = 203 + (185 - blockW) / 2;
  int baseY = 122;
  canvas.setFont(&FONT_XLARGE); canvas.setCursor(startX, baseY); canvas.print(humValue);
  canvas.setFont(&FONT_MEDIUM); canvas.setCursor(startX + vw + 6, baseY); canvas.print("%");

  canvas.fillRect(8, 145, 384, 3, 1);

  const char* fullDays[] = {"Domingo","Lunes","Martes","Miercoles","Jueves","Viernes","Sabado"};
  canvas.setFont(&FONT_MEDIUM); canvas.setCursor(15, 172);
  if (dow >= 0 && dow <= 6) canvas.print(fullDays[dow]);
  canvas.print(" ");
  canvas.print(rtc.getDay()); canvas.print("/");
  canvas.print(rtc.getMonth()); canvas.print("/");
  int yr = rtc.getYear() % 100;
  if (yr < 10) canvas.print("0");
  canvas.print(yr);
  String cityLabel = getDisplayLocation();
  if (cityLabel.length() > 16) cityLabel = cityLabel.substring(0, 16);
  canvas.setCursor(238, 172); canvas.print(cityLabel);

  char timeStr[6]; sprintf(timeStr, "%02d:%02d", hour24, minuteVal);
  canvas.setFont(&DSEG7_Classic_Bold_84); canvas.setCursor(18, 276); canvas.print(timeStr);
  if (!batterySaveMode) {
    char secStr[3]; sprintf(secStr, "%02d", secondVal);
    canvas.setFont(&DSEG7_Classic_Bold_36); canvas.setCursor(323, 238); canvas.print(secStr);
  }

  pushCanvasToRLCD(false);
}

// ===== PAGE 2: FOTO EN VIVO =====
void drawPhotoPage() {
  canvas.fillScreen(0);

  if (photoUseUploaded && (!photoValid || photoJpegData == nullptr || photoJpegLen == 0)) {
    String err;
    if (!loadUploadedPhotoFromFs(err)) photoLastError = "Imagen fija: " + err;
  }

  if (photoValid && photoJpegData != nullptr && photoJpegLen > 0) {
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(photoRenderCallback);
    JRESULT jr = TJpgDec.drawJpg(0, 0, photoJpegData, (uint32_t)photoJpegLen);
    if (jr != JDR_OK) {
      photoLastError = "Error decodificador " + String((int)jr);
      photoValid = false;
      canvas.fillScreen(0);
      printCentered(&FONT_MEDIUM, 132, "Sin imagen");
      String e = photoLastError;
      if (e.length() > 28) e = e.substring(0, 28);
      printCentered(&FONT_SMALL, 154, e.c_str());
    }
  } else {
    printCentered(&FONT_MEDIUM, 132, "Sin imagen");
    String e = photoLastError;
    e.trim();
    if (e.length() > 0 && !e.equalsIgnoreCase("Sin imagen")) {
      if (e.length() > 28) e = e.substring(0, 28);
      printCentered(&FONT_SMALL, 154, e.c_str());
    }
  }

  // Header overlay
  canvas.fillRect(0, 0, W, 28, 0);
  canvas.drawLine(0, 27, W - 1, 27, 1);

  char timeStr[10];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", hour24, minuteVal, secondVal);
  canvas.setFont(&FONT_MEDIUM);
  canvas.setTextColor(1);
  canvas.setCursor(8, 22);
  canvas.print(timeStr);

  char tempBuf[20];
  dtostrf(temperature, 0, 1, tempBuf);

  String sensorStr = String(tempBuf) + "\xB0" "C - " + String((int)humidity) + "%";
  int16_t tx, ty;
  uint16_t tw, th;
  canvas.setFont(&FONT_SMALL);
  canvas.getTextBounds(sensorStr, 0, 0, &tx, &ty, &tw, &th);
  canvas.setCursor(W - 8 - (int)tw, 20);
  canvas.print(sensorStr);

  // Bottom info band: full date in Spanish
  static const char* kDaysEs[] = {"Domingo","Lunes","Martes","Miercoles","Jueves","Viernes","Sabado"};
  static const char* kMonthsEs[] = {"Enero","Febrero","Marzo","Abril","Mayo","Junio","Julio","Agosto","Septiembre","Octubre","Noviembre","Diciembre"};

  int dow = rtc.getWeekday();
  int dd = rtc.getDay();
  int mm = rtc.getMonth();
  int yy = rtc.getYear();
  if (yy < 100) yy += 2000;
  if (dow < 0 || dow > 6) dow = 0;
  if (mm < 1 || mm > 12) mm = 1;

  String dateStr = String(kDaysEs[dow]) + " " + String(dd) + " de " + String(kMonthsEs[mm - 1]) + ", " + String(yy);
  canvas.fillRect(0, 272, W, 28, 0);
  canvas.drawLine(0, 271, W - 1, 271, 1);
  canvas.setFont(&FONT_SMALL);
  int16_t dx, dy;
  uint16_t dw, dh;
  canvas.getTextBounds(dateStr, 0, 0, &dx, &dy, &dw, &dh);
  canvas.setCursor((W - (int)dw) / 2 - dx, 292);
  canvas.print(dateStr);

  pushCanvasToRLCD(false);
}

// ===== PAGE 1: ANALOGUE CLOCK =====
void drawAnalogClockPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1);
  canvas.drawRect(1, 1, W-2, H-2, 1);

  // Inverted header bar â€” location + timezone
  canvas.fillRect(8, 8, 384, 22, 1);
  canvas.setTextColor(0);
  canvas.setFont(&FONT_SMALL);
  char hdrBuf[32];
  snprintf(hdrBuf, sizeof(hdrBuf), "%s  %s", getDisplayLocation().c_str(), getTZLabel());
  int16_t hx1, hy1; uint16_t htw, hth;
  canvas.getTextBounds(hdrBuf, 0, 24, &hx1, &hy1, &htw, &hth);
  canvas.setCursor((W - htw) / 2 - hx1, 24);
  canvas.print(hdrBuf);
  canvas.setTextColor(1);

  // Clock geometry â€” filled black face, R=110
  const int cx = 200, cy = 152, R = 110;

  // Filled black face
  canvas.fillCircle(cx, cy, R, 1);

  // Hour markers â€” white, thick at 12/3/6/9, medium elsewhere
  for (int i = 0; i < 12; i++) {
    float a = (i * 30 - 90) * 3.14159f / 180.0f;
    bool isCard = (i % 3 == 0);
    int r1 = R - 2, r2 = isCard ? R - 20 : R - 12;
    int x1 = cx + (int)(r1 * cosf(a)), y1 = cy + (int)(r1 * sinf(a));
    int x2 = cx + (int)(r2 * cosf(a)), y2 = cy + (int)(r2 * sinf(a));
    // Draw thick white marker as 2-3 adjacent lines
    canvas.drawLine(x1, y1, x2, y2, 0);
    if (isCard) {
      // Extra adjacent pixels for thick cardinal markers
      int px1 = cx + (int)(r1 * cosf(a + 0.04f)), py1 = cy + (int)(r1 * sinf(a + 0.04f));
      int px2 = cx + (int)(r2 * cosf(a + 0.04f)), py2 = cy + (int)(r2 * sinf(a + 0.04f));
      canvas.drawLine(px1, py1, px2, py2, 0);
      int qx1 = cx + (int)(r1 * cosf(a - 0.04f)), qy1 = cy + (int)(r1 * sinf(a - 0.04f));
      int qx2 = cx + (int)(r2 * cosf(a - 0.04f)), qy2 = cy + (int)(r2 * sinf(a - 0.04f));
      canvas.drawLine(qx1, qy1, qx2, qy2, 0);
    }
  }

  // Minute ticks â€” white, thin
  for (int i = 0; i < 60; i++) {
    if (i % 5 == 0) continue;
    float a = (i * 6 - 90) * 3.14159f / 180.0f;
    int x1 = cx + (int)((R-2)  * cosf(a)), y1 = cy + (int)((R-2)  * sinf(a));
    int x2 = cx + (int)((R-7) * cosf(a)), y2 = cy + (int)((R-7) * sinf(a));
    canvas.drawLine(x1, y1, x2, y2, 0);
  }

  // Hour numerals 12, 3, 6, 9 in white (setTextColor 0 = white on filled face)
  canvas.setTextColor(0);
  canvas.setFont(&FONT_MEDIUM);
  struct { const char* n; int a; } hnums[] = {{"12",0},{"3",90},{"6",180},{"9",270}};
  for (int i = 0; i < 4; i++) {
    float rad = (hnums[i].a - 90) * 3.14159f / 180.0f;
    int nr = R - 30;
    int16_t nx1, ny1; uint16_t ntw, nth;
    canvas.getTextBounds(hnums[i].n, 0, 0, &nx1, &ny1, &ntw, &nth);
    int tx = cx + (int)(nr * cosf(rad)) - ntw/2 - nx1;
    int ty = cy + (int)(nr * sinf(rad)) + nth/2;
    canvas.setCursor(tx, ty);
    canvas.print(hnums[i].n);
  }
  canvas.setTextColor(1);

  // Compute hand angles
  int h12    = hour24 % 12;
  float secF  = (float)secondVal;
  float minF  = (float)minuteVal + secF / 60.0f;
  float hourF = (float)h12 + minF / 60.0f;
  float hourAngle = hourF * 30.0f;   // degrees
  float minAngle  = minF  * 6.0f;
  float secAngle  = secF  * 6.0f;

  // Hour hand â€” white, thick (3 adjacent lines)
  float hRad = (hourAngle - 90.0f) * 3.14159f / 180.0f;
  int hx = cx + (int)(63 * cosf(hRad)), hy = cy + (int)(63 * sinf(hRad));
  canvas.drawLine(cx, cy, hx, hy, 0);
  canvas.drawLine(cx+1, cy,   hx+1, hy,   0);
  canvas.drawLine(cx,   cy+1, hx,   hy+1, 0);
  canvas.drawLine(cx-1, cy,   hx-1, hy,   0);
  canvas.drawLine(cx,   cy-1, hx,   hy-1, 0);

  // Minute hand â€” white, medium (2 adjacent lines)
  float mRad = (minAngle - 90.0f) * 3.14159f / 180.0f;
  int mhx = cx + (int)(90 * cosf(mRad)), mhy = cy + (int)(90 * sinf(mRad));
  canvas.drawLine(cx, cy, mhx, mhy, 0);
  canvas.drawLine(cx+1, cy,   mhx+1, mhy,   0);
  canvas.drawLine(cx,   cy+1, mhx,   mhy+1, 0);

  // Second hand â€” white, single pixel with tail
  float sRad = (secAngle - 90.0f) * 3.14159f / 180.0f;
  int shx = cx + (int)(98  * cosf(sRad)), shy  = cy + (int)(98  * sinf(sRad));
  int stx = cx + (int)(19  * cosf(sRad + 3.14159f)), sty = cy + (int)(19 * sinf(sRad + 3.14159f));
  canvas.drawLine(stx, sty, shx, shy, 0);

  // Centre cap â€” white filled, black dot
  canvas.fillCircle(cx, cy, 5, 0);
  canvas.fillCircle(cx, cy, 2, 1);

  // Bottom strip â€” divider line then date and indoor data
  canvas.fillRect(8, 270, 384, 2, 1);
  canvas.setFont(&FONT_SMALL); canvas.setTextColor(1);
  // Date left
  char dateBuf[16];
  const char* days[] = {"Dom","Lun","Mar","Mie","Jue","Vie","Sab"};
  int dow = rtc.getWeekday();
  snprintf(dateBuf, sizeof(dateBuf), "%s %d/%d/%02d",
    (dow>=0&&dow<=6)?days[dow]:"---",
    rtc.getDay(), rtc.getMonth(), rtc.getYear()%100);
  canvas.setCursor(14, 290); canvas.print(dateBuf);
  // Temp/humidity/battery right-aligned
  char infoBuf[24];
  snprintf(infoBuf, sizeof(infoBuf), "%.1fC  %d%%  %.1fV",
    temperature, (int)humidity, batteryVoltage);
  int16_t ix1, iy1; uint16_t itw, ith;
  canvas.getTextBounds(infoBuf, 0, 290, &ix1, &iy1, &itw, &ith);
  canvas.setCursor(W - 14 - itw - ix1, 290); canvas.print(infoBuf);

  pushCanvasToRLCD(false);
}

// ===== PAGE 2: AUS TIMEZONE CLOCK =====
void drawTimeZonePage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1);
  canvas.drawRect(1, 1, W-2, H-2, 1);

  // Inverted header bar â€” centred text
  canvas.fillRect(8, 8, 384, 22, 1);
  canvas.setTextColor(0); canvas.setFont(&FONT_SMALL);
  int16_t tzHx1, tzHy1; uint16_t tzHtw, tzHth;
  canvas.getTextBounds("ZONAS HORARIAS", 0, 24, &tzHx1, &tzHy1, &tzHtw, &tzHth);
  canvas.setCursor((W - tzHtw) / 2 - tzHx1, 24);
  canvas.print("ZONAS HORARIAS");
  canvas.setTextColor(1);

  // Vertical centre divider
  canvas.fillRect(199, 30, 2, 258, 1);

  time_t nowEpoch = time(nullptr);
  struct tm utcTm;
  gmtime_r(&nowEpoch, &utcTm);
  int utcMins = utcTm.tm_hour * 60 + utcTm.tm_min;

  struct TZEntry { const char* city; const char* tz; int offMins; } left[3], right[3];
  int localOffsetMins = (int)(gmtOffset_sec / 60);

  left[0] = { "Argentina", getTZLabel(), localOffsetMins };
  left[1] = { "Santiago",  "GMT-4",      -4 * 60 };
  left[2] = { "New York",  "GMT-5",      -5 * 60 };
  right[0]= { "UTC",       "UTC",         0 };
  right[1]= { "Madrid",    "CET",         1 * 60 };
  right[2]= { "Tokyo",     "JST",         9 * 60 };

  const int GT = 30, RH = 86, COLW = 191;
  const int LCOL = 8, RCOL = 201;

  for (int col = 0; col < 2; col++) {
    int lx = (col == 0) ? LCOL : RCOL;
    for (int i = 0; i < 3; i++) {
      TZEntry& z = (col == 0) ? left[i] : right[i];
      int ry = GT + i * RH;

      if (i > 0) canvas.fillRect(lx, ry, COLW, 1, 1);

      canvas.setFont(&FONT_SMALL);
      int16_t cx1, cy1; uint16_t ctw, cth;
      canvas.getTextBounds(z.city, 0, 0, &cx1, &cy1, &ctw, &cth);
      canvas.setCursor(lx + (COLW - ctw) / 2 - cx1, ry + 16);
      canvas.print(z.city);

      int16_t tx1, ty1; uint16_t ttw, tth;
      canvas.getTextBounds(z.tz, 0, 0, &tx1, &ty1, &ttw, &tth);
      canvas.setCursor(lx + (COLW - ttw) / 2 - tx1, ry + 32);
      canvas.print(z.tz);

      int tzMins = ((utcMins + z.offMins) % 1440 + 1440) % 1440;
      int tzH = tzMins / 60, tzM = tzMins % 60;
      char tsBuf[6]; sprintf(tsBuf, "%02d:%02d", tzH, tzM);
      canvas.setFont(&DSEG7_Classic_Bold_36);
      int16_t dx1, dy1; uint16_t dtw, dth;
      canvas.getTextBounds(tsBuf, 0, 0, &dx1, &dy1, &dtw, &dth);
      canvas.setCursor(lx + (COLW - dtw) / 2 - dx1, ry + RH - 8);
      canvas.print(tsBuf);
    }
  }

  canvas.setFont(&FONT_SMALL); canvas.setTextColor(1);
  pushCanvasToRLCD(false);
}

// ===== PAGE 3: CURRENT CONDITIONS =====
void drawCurrentWeatherPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1);
  canvas.drawRect(1, 1, W-2, H-2, 1);

  if (!weatherData.valid) {
    canvas.setFont(&FONT_LARGE); canvas.setTextColor(1);
    canvas.setCursor(60, 130); canvas.print("SIN DATOS DE CLIMA");
    canvas.setFont(&FONT_SMALL); canvas.setCursor(110, 165); canvas.print("Presiona boton KEY");
    pushCanvasToRLCD(false); return;
  }

  canvas.setFont(&FONT_SMALL); canvas.setTextColor(1);
  canvas.setCursor(12, 25); canvas.print("CONDICIONES ACTUALES  "); canvas.print(getDisplayLocation());

  canvas.setFont(&FONT_XLARGE); canvas.setCursor(12, 75); canvas.print(weatherData.currentTemp, 1);
  canvas.setFont(&FONT_LARGE);  canvas.print(" C");

  // Condition text â€” word-wrap into two lines if longer than ~20 chars
  canvas.setFont(&FONT_SMALL);
  String cond = weatherData.condition;
  const int condMaxChars = 20;
  if (cond.length() <= condMaxChars) {
    canvas.setCursor(12, 95); canvas.print(cond);
  } else {
    int sp = cond.lastIndexOf(' ', condMaxChars);
    if (sp < 1) sp = condMaxChars;
    canvas.setCursor(12, 95);  canvas.print(cond.substring(0, sp));
    canvas.setCursor(12, 109); canvas.print(cond.substring(sp + 1, min((int)cond.length(), sp + condMaxChars + 1)));
  }

  // Feels like â€” always on line 3
  canvas.setFont(&FONT_SMALL); canvas.setCursor(12, 123);
  canvas.print("Sensacion "); canvas.print(weatherData.feelsLike, 1); canvas.print(" C");

  canvas.fillRect(200, 34, 2, 102, 1);
  canvas.fillRect(302, 34, 2, 102, 1);

  const int boxTop = 34, boxBot = 138, slotH = (boxBot - boxTop) / 2;
  canvas.setFont(&FONT_SMALL);  canvas.setCursor(208, boxTop + 16);         canvas.print("MAX");
  canvas.setFont(&FONT_MEDIUM); canvas.setCursor(208, boxTop + 38);         canvas.print(weatherData.forecast[0].maxTemp, 1); canvas.setFont(&FONT_SMALL); canvas.print(" C");
  canvas.setFont(&FONT_SMALL);  canvas.setCursor(208, boxTop + slotH + 16); canvas.print("MIN");
  canvas.setFont(&FONT_MEDIUM); canvas.setCursor(208, boxTop + slotH + 38); canvas.print(weatherData.forecast[0].minTemp, 1); canvas.setFont(&FONT_SMALL); canvas.print(" C");
  canvas.setFont(&FONT_SMALL);  canvas.setCursor(310, boxTop + 16);         canvas.print("INDICE UV");
  canvas.setFont(&FONT_MEDIUM); canvas.setCursor(310, boxTop + 38);         canvas.print(weatherData.uvIndex, 1);
  canvas.setFont(&FONT_SMALL);  canvas.setCursor(310, boxTop + slotH + 16); canvas.print("HUMEDAD");
  canvas.setFont(&FONT_MEDIUM); canvas.setCursor(310, boxTop + slotH + 38); canvas.print(weatherData.humidity); canvas.print(" %");

  canvas.fillRect(8, 134, 384, 2, 1);

  int lx = 12, rx = 210, gy = 152, rowH = 42;
  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(lx, gy); canvas.print("VIENTO");
  canvas.setCursor(rx, gy); canvas.print("TEMP EXTERIOR");
  canvas.setFont(&FONT_MEDIUM);
  canvas.setCursor(lx, gy+20); canvas.print(weatherData.windSpeed, 1); canvas.print(" kph "); canvas.print(weatherData.windDir);
  canvas.setCursor(rx, gy+20); canvas.print(weatherData.currentTemp, 1); canvas.print(" C");
  gy += rowH;
  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(lx, gy); canvas.print("CALIDAD AIRE");
  canvas.setCursor(rx, gy); canvas.print("PM2.5");
  canvas.setFont(&FONT_MEDIUM);
  canvas.setCursor(lx, gy+20); canvas.print(weatherData.airQualityText);
  canvas.setCursor(rx, gy+20); canvas.print(weatherData.pm25, 1); canvas.print(" ug/m3");
  gy += rowH;
  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(lx, gy); canvas.print("ACTUALIZADO");
  canvas.setCursor(rx, gy); canvas.print("LLUVIA");
  canvas.setFont(&FONT_MEDIUM);
  canvas.setCursor(lx, gy+20); canvas.print((millis() - weatherData.lastUpdate) / 60000); canvas.print(" min");
  canvas.setCursor(rx, gy+20); canvas.print(weatherData.precipMM, 1); canvas.print(" mm");

  canvas.fillRect(200, 136, 2, 132, 1);
  canvas.fillRect(8, 268, 384, 2, 1);
  pushCanvasToRLCD(false);
}

// ===== PAGE 5: 3-DAY FORECAST =====
void drawForecastPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1);
  canvas.drawRect(1, 1, W-2, H-2, 1);

  if (!weatherData.valid) {
    canvas.setFont(&FONT_LARGE); canvas.setTextColor(1);
    canvas.setCursor(60, 130); canvas.print("SIN DATOS DE CLIMA");
    canvas.setFont(&FONT_SMALL); canvas.setCursor(110, 165); canvas.print("Presiona boton KEY");
    pushCanvasToRLCD(false); return;
  }

  canvas.setFont(&FONT_SMALL); canvas.setTextColor(1);
  canvas.setCursor(12, 25); canvas.print("PRONOSTICO 3 DIAS  "); canvas.print(getDisplayLocation());

  const int cardW = 122, cardY = 36, hdrH = 24;
  const int rowH = 40, condH = 52;
  const int cardH = hdrH + rowH + rowH + condH + rowH + rowH;

  for (int i = 0; i < 3; i++) {
    int cx = 10 + i * (cardW + 4);
    canvas.drawRect(cx, cardY, cardW, cardH, 1);
    canvas.fillRect(cx+1, cardY+1, cardW-2, hdrH-1, 1);
    String dayStr = weatherData.forecast[i].day;
    if (dayStr.length() >= 10) dayStr = dayStr.substring(5);
    canvas.setTextColor(0); canvas.setFont(&FONT_SMALL);
    canvas.setCursor(cx+6, cardY+17); canvas.print(dayStr);
    canvas.setTextColor(1);

    int y = cardY + hdrH + 2;
    canvas.setFont(&FONT_SMALL);  canvas.setCursor(cx+6, y+11); canvas.print("MAX");
    canvas.setFont(&FONT_MEDIUM); canvas.setCursor(cx+6, y+30); canvas.print(weatherData.forecast[i].maxTemp, 0); canvas.print(" C");
    y += rowH;
    canvas.setFont(&FONT_SMALL);  canvas.setCursor(cx+6, y+11); canvas.print("MIN");
    canvas.setFont(&FONT_MEDIUM); canvas.setCursor(cx+6, y+30); canvas.print(weatherData.forecast[i].minTemp, 0); canvas.print(" C");
    y += rowH;
    canvas.setFont(&FONT_SMALL);  canvas.setCursor(cx+6, y+11); canvas.print("ESTADO");
    String fcond = weatherData.forecast[i].condition;
    if (fcond.length() > 13) {
      int sp = fcond.lastIndexOf(' ', 13);
      if (sp > 0) {
        canvas.setCursor(cx+6, y+28); canvas.print(fcond.substring(0, sp));
        canvas.setCursor(cx+6, y+42); canvas.print(fcond.substring(sp+1, min((int)fcond.length(), sp+14)));
      } else {
        canvas.setCursor(cx+6, y+28); canvas.print(fcond.substring(0, 13));
      }
    } else {
      canvas.setCursor(cx+6, y+28); canvas.print(fcond);
    }
    y += condH;
    canvas.setFont(&FONT_SMALL);  canvas.setCursor(cx+6, y+11); canvas.print("LLUVIA");
    canvas.setFont(&FONT_MEDIUM); canvas.setCursor(cx+6, y+30); canvas.print(weatherData.forecast[i].precipMM, 1); canvas.print("mm");
    y += rowH;
    canvas.setFont(&FONT_SMALL);  canvas.setCursor(cx+6, y+11); canvas.print("PROB.");
    canvas.setFont(&FONT_MEDIUM); canvas.setCursor(cx+6, y+30);
    if (i == 0 && hourlyData.valid) {
      int maxChance = 0;
      for (int h = 0; h < 6; h++) if (hourlyData.rainChance[h] > maxChance) maxChance = hourlyData.rainChance[h];
      canvas.print(maxChance); canvas.print(" %");
    } else {
      canvas.print(weatherData.forecast[i].rainChance); canvas.print(" %");
    }
  }

  // No bottom divider line â€” cards contain all content
  canvas.setFont(&FONT_SMALL); canvas.setCursor(12, 285);
  canvas.print("Act. "); canvas.print((millis() - weatherData.lastUpdate) / 60000); canvas.print("m");
  pushCanvasToRLCD(false);
}

// ===== ASTRONOMY HELPERS =====
int parseTimeToMinutes(const String& t) {
  if (t.length() < 4) return 0;
  int colon = t.indexOf(':'); if (colon < 0) return 0;
  int h = t.substring(0, colon).toInt();
  int m = t.substring(colon+1, colon+3).toInt();
  bool pm = (t.indexOf("PM") >= 0 || t.indexOf("pm") >= 0);
  bool am = (t.indexOf("AM") >= 0 || t.indexOf("am") >= 0);
  if (pm && h != 12) h += 12;
  if (am && h == 12) h  = 0;
  return h * 60 + m;
}

String minutesToTimeStr(int mins) {
  mins = ((mins % 1440) + 1440) % 1440;
  char buf[6]; sprintf(buf, "%02d:%02d", mins / 60, mins % 60);
  return String(buf);
}

void drawMoonIcon(int cx, int cy, int r, const String& phase) {
  String p = normalizeMoonPhase(phase);
  if (p == "New Moon")  { canvas.drawCircle(cx, cy, r, 1); return; }
  if (p == "Full Moon") { canvas.fillCircle(cx, cy, r, 1); return; }
  bool waxing = (p == "Waxing Crescent" || p == "First Quarter" || p == "Waxing Gibbous");
  canvas.fillCircle(cx, cy, r, 1);
  int ox = 0;
  if      (p == "Waxing Crescent" || p == "Waning Crescent") ox = r - 2;
  else if (p == "First Quarter"   || p == "Last Quarter")    ox = 0;
  else if (p == "Waxing Gibbous"  || p == "Waning Gibbous")  ox = -(r - 2);
  int darkX = waxing ? cx - ox : cx + ox;
  canvas.fillCircle(darkX, cy, r, 0);
  canvas.drawCircle(cx, cy, r, 1);
}

// ===== PAGE 6: ASTRONOMY =====
void drawAstronomyPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1);
  canvas.drawRect(1, 1, W-2, H-2, 1);
  canvas.setFont(&FONT_SMALL); canvas.setTextColor(1);
  const char* fullDays[] = {"Domingo","Lunes","Martes","Miercoles","Jueves","Viernes","Sabado"};
  int dow = rtc.getWeekday();
  canvas.setCursor(12, 24);
  if (dow >= 0 && dow <= 6) canvas.print(fullDays[dow]);
  canvas.print("  ");
  canvas.print(rtc.getDay()); canvas.print("/"); canvas.print(rtc.getMonth()); canvas.print("/");
  int yr = rtc.getYear() % 100; if (yr < 10) canvas.print("0"); canvas.print(yr);
  canvas.print("  ASTRONOMIA  "); canvas.print(getDisplayLocation());

  if (!astroData.valid) {
    canvas.setFont(&FONT_MEDIUM); canvas.setCursor(80, 160); canvas.print("SIN DATOS ASTRON.");
    pushCanvasToRLCD(false); return;
  }

  canvas.drawRect(8, 36, 384, 72, 1); canvas.drawRect(9, 37, 382, 70, 1);
  int lx = 20, rx = 210;
  int sunriseMins = parseTimeToMinutes(astroData.sunrise);
  int sunsetMins  = parseTimeToMinutes(astroData.sunset);
  int dayLenMins  = sunsetMins - sunriseMins;
  int noonMins    = sunriseMins + dayLenMins / 2;
  int dlH = dayLenMins / 60, dlM = dayLenMins % 60;

  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(lx, 56); canvas.print("AMANECER");
  canvas.setCursor(rx, 56); canvas.print("ATARDECER");
  canvas.setFont(&FONT_LARGE);
  canvas.setCursor(lx, 84); canvas.print(astroData.sunrise);
  canvas.setCursor(rx, 84); canvas.print(astroData.sunset);
  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(lx, 100); canvas.print("Dia: "); canvas.print(dlH); canvas.print("h "); canvas.print(dlM); canvas.print("m");
  canvas.setCursor(rx, 100); canvas.print("Mediodia: "); canvas.print(minutesToTimeStr(noonMins));

  const int moonTop = 112, moonBot = 266, moonRows = 3;
  const int moonSlotH = (moonBot - moonTop - 4) / moonRows;
  canvas.drawRect(8, moonTop, 384, moonBot - moonTop, 1);
  canvas.drawRect(9, moonTop+1, 382, moonBot - moonTop - 2, 1);
  drawMoonIcon(340, moonTop + (moonBot - moonTop)/2, 28, astroData.moonPhase);

  String phase = normalizeMoonPhase(astroData.moonPhase);
  int illum = astroData.moonIllumination;
  String nextPhase = ""; float daysUntil = 0.0f;
  if      (phase == "New Moon")        { nextPhase = "Waxing Crescent"; daysUntil = 3.7f - (illum / 100.0f * 3.7f); }
  else if (phase == "Waxing Crescent") { nextPhase = "First Quarter";   daysUntil = 3.7f - (illum / 50.0f * 3.7f); }
  else if (phase == "First Quarter")   { nextPhase = "Waxing Gibbous";  daysUntil = 3.7f - ((illum-50) / 50.0f * 3.7f); }
  else if (phase == "Waxing Gibbous")  { nextPhase = "Full Moon";       daysUntil = 3.7f - (illum / 100.0f * 3.7f); }
  else if (phase == "Full Moon")       { nextPhase = "Waning Gibbous";  daysUntil = 3.7f - ((100-illum) / 100.0f * 3.7f); }
  else if (phase == "Waning Gibbous")  { nextPhase = "Last Quarter";    daysUntil = 3.7f - ((100-illum) / 50.0f * 3.7f); }
  else if (phase == "Last Quarter")    { nextPhase = "Waning Crescent"; daysUntil = 3.7f - ((50-illum) / 50.0f * 3.7f); }
  else if (phase == "Waning Crescent") { nextPhase = "New Moon";        daysUntil = 3.7f - (illum / 100.0f * 3.7f); }
  if (daysUntil < 0.5f) daysUntil = 0.5f;

  for (int r = 0; r < moonRows; r++) {
    int sy = moonTop + 8 + r * moonSlotH;
    canvas.setFont(&FONT_SMALL);
    switch (r) {
      case 0:
        canvas.setCursor(lx, sy+12); canvas.print("SAL LUNA"); canvas.setCursor(rx, sy+12); canvas.print("PUE LUNA");
        canvas.setFont(&FONT_MEDIUM);
        canvas.setCursor(lx, sy+32); canvas.print(astroData.moonrise); canvas.setCursor(rx, sy+32); canvas.print(astroData.moonset);
        break;
      case 1:
        canvas.setCursor(lx, sy+12); canvas.print("FASE"); canvas.setCursor(rx, sy+12); canvas.print("ILUM");
        canvas.setFont(&FONT_MEDIUM);
        canvas.setCursor(lx, sy+32); canvas.print(moonPhaseEs(phase)); canvas.setCursor(rx, sy+32); canvas.print(illum); canvas.print(" %");
        break;
      case 2:
        canvas.setCursor(lx, sy+12); canvas.print("PROX FASE"); canvas.setCursor(rx, sy+12); canvas.print("FALTAN");
        canvas.setFont(&FONT_MEDIUM);
        canvas.setCursor(lx, sy+32); canvas.print(moonPhaseEs(nextPhase)); canvas.setCursor(rx, sy+32); canvas.print((int)roundf(daysUntil)); canvas.print(" dias");
        break;
    }
  }
  pushCanvasToRLCD(false);
}

// ===== PAGE 7: LUNAR ORBIT =====
void drawLunarOrbitPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1);
  canvas.drawRect(1, 1, W-2, H-2, 1);
  canvas.setFont(&FONT_SMALL); canvas.setTextColor(1);
  canvas.setCursor(12, 24); canvas.print("ORBITA LUNAR  "); canvas.print(getDisplayLocation());

  if (!astroData.valid) {
    canvas.setFont(&FONT_MEDIUM); canvas.setCursor(80, 160); canvas.print("SIN DATOS ASTRON.");
    pushCanvasToRLCD(false); return;
  }

  String phase = normalizeMoonPhase(astroData.moonPhase);
  int illum    = astroData.moonIllumination;

  // Moon age approximation from phase
  int age = 0;
  if      (phase == "New Moon")        age = 0;
  else if (phase == "Waxing Crescent") age = 4;
  else if (phase == "First Quarter")   age = 7;
  else if (phase == "Waxing Gibbous")  age = 11;
  else if (phase == "Full Moon")       age = 15;
  else if (phase == "Waning Gibbous")  age = 18;
  else if (phase == "Last Quarter")    age = 22;
  else if (phase == "Waning Crescent") age = 26;
  // (cycleProgress not used â€” age drives the progress arc directly)

  // Next phase
  int daysUntilNext = 0;
  if      (phase == "New Moon")        daysUntilNext = 4;
  else if (phase == "Waxing Crescent") daysUntilNext = 3;
  else if (phase == "First Quarter")   daysUntilNext = 4;
  else if (phase == "Waxing Gibbous")  daysUntilNext = 4;
  else if (phase == "Full Moon")       daysUntilNext = 4;
  else if (phase == "Waning Gibbous")  daysUntilNext = 3;
  else if (phase == "Last Quarter")    daysUntilNext = 4;
  else if (phase == "Waning Crescent") daysUntilNext = 4;

  // Phase angle on orbit + next phase angle for midpoint calc
  int moonAngleDeg = 0, nextAngleDeg = 45;
  if      (phase == "New Moon")        { moonAngleDeg =   0; nextAngleDeg =  45; }
  else if (phase == "Waxing Crescent") { moonAngleDeg =  45; nextAngleDeg =  90; }
  else if (phase == "First Quarter")   { moonAngleDeg =  90; nextAngleDeg = 135; }
  else if (phase == "Waxing Gibbous")  { moonAngleDeg = 135; nextAngleDeg = 180; }
  else if (phase == "Full Moon")       { moonAngleDeg = 180; nextAngleDeg = 225; }
  else if (phase == "Waning Gibbous")  { moonAngleDeg = 225; nextAngleDeg = 270; }
  else if (phase == "Last Quarter")    { moonAngleDeg = 270; nextAngleDeg = 315; }
  else if (phase == "Waning Crescent") { moonAngleDeg = 315; nextAngleDeg = 360; }

  // ===== ORBIT DIAGRAM â€” full height, no bottom panel =====
  const int ox = 200, oy = 158, orb = 100;
  for (int a = 0; a < moonAngleDeg; a++) {
    float r1 = a * 3.14159f / 180.0f;
    float r2 = (a+1) * 3.14159f / 180.0f;
    int x1 = ox + (int)((orb-5) * cosf(r1)), y1 = oy + (int)((orb-5) * sinf(r1));
    int x2 = ox + (int)((orb-5) * cosf(r2)), y2 = oy + (int)((orb-5) * sinf(r2));
    canvas.drawLine(x1, y1, x2, y2, 1);
    x1 = ox + (int)((orb-6) * cosf(r1)); y1 = oy + (int)((orb-6) * sinf(r1));
    x2 = ox + (int)((orb-6) * cosf(r2)); y2 = oy + (int)((orb-6) * sinf(r2));
    canvas.drawLine(x1, y1, x2, y2, 1);
    x1 = ox + (int)((orb-7) * cosf(r1)); y1 = oy + (int)((orb-7) * sinf(r1));
    x2 = ox + (int)((orb-7) * cosf(r2)); y2 = oy + (int)((orb-7) * sinf(r2));
    canvas.drawLine(x1, y1, x2, y2, 1);
  }

  // Orbit circle
  canvas.drawCircle(ox, oy, orb, 1);

  // Earth at centre
  canvas.fillCircle(ox, oy, 8, 1);
  canvas.fillCircle(ox, oy, 4, 0);
  canvas.setTextColor(0); canvas.setFont(&FONT_SMALL);
  canvas.setCursor(ox-4, oy+4); canvas.print("E");
  canvas.setTextColor(1);

  // Phase markers and labels â€” all outside the orbit
  struct { int angle; const char* lbl; const char* sub; } markers[] = {
    {   0, "Nueva",  "Luna"  },
    {  45, "Crec.",  "Luna" },
    {  90, "1er",    "Cuarto"   },
    { 135, "Gib.",   "Crec."  },
    { 180, "Llena", "Luna"  },
    { 225, "Gib.",   "Meng."  },
    { 270, "Ult.",   "Cuarto"   },
    { 315, "Luna",   "Meng." },
  };

  for (int i = 0; i < 8; i++) {
    float rad = markers[i].angle * 3.14159f / 180.0f;
    int ir = orb - 5, or2 = orb + 5;
    canvas.drawLine(ox+(int)(ir*cosf(rad)), oy+(int)(ir*sinf(rad)),
                    ox+(int)(or2*cosf(rad)), oy+(int)(or2*sinf(rad)), 1);

    int lx, ly;

    if (markers[i].angle == 270) {
      // Last Qtr â€” top of orbit, place below dot to avoid header
      lx = ox - 17;
      ly = oy - orb + 22;  // cap top=71, dot edge=62, gap=9px
    } else if (markers[i].angle == 90) {
      // 1st Qtr â€” bottom of orbit, place above dot to avoid page dots
      lx = ox - 8;
      ly = oy + orb - 42;  // row2 bot=233, dot edge=262, gap=29px
    } else {
      float lr = orb + 28;  // push labels well clear of moon icon (r=9)
      lx = ox + (int)(lr * cosf(rad));
      ly = oy + (int)(lr * sinf(rad));
      bool isLeft = markers[i].angle > 90 && markers[i].angle < 270;
      bool isTop  = markers[i].angle > 180 && markers[i].angle < 360;
      if (isLeft) lx -= 38; else lx -= 8;
      if (isTop)  ly -= 18; else ly -= 2;
    }

    canvas.setFont(&FONT_SMALL);
    canvas.setCursor(lx, ly);    canvas.print(markers[i].lbl);
    canvas.setCursor(lx, ly+16); canvas.print(markers[i].sub);
    // Days until next phase on the upcoming phase marker
    // nextAngleDeg can be 360 (Waning Crescentâ†’New Moon) â€” clamp to 360%360=0 correctly matches angle 0
    if (markers[i].angle == (nextAngleDeg % 360)) {
      canvas.setCursor(lx, ly+32); canvas.print("en "); canvas.print(daysUntilNext); canvas.print("d");
    }
  }

  // Moon position on orbit
  float moonRad = moonAngleDeg * 3.14159f / 180.0f;
  int mx = ox + (int)(orb * cosf(moonRad));
  int my = oy + (int)(orb * sinf(moonRad));

  // Draw moon phase icon
  drawMoonIcon(mx, my, 9, phase);

  // Fixed labels flanking Earth icon â€” ILLUM left, AGE right, stable at all moon positions
  // Left side: shifted further left to give clear gap from Earth edge (ox-8)
  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(ox - 72, oy - 6);  canvas.print("ILUM");
  canvas.setFont(&FONT_MEDIUM);
  canvas.setCursor(ox - 72, oy + 14); canvas.print(illum); canvas.print("%");

  // Right side: left-edge of text starts ~10px right of Earth edge (ox+8), so anchor at ox+18
  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(ox + 18, oy - 6);  canvas.print("EDAD");
  canvas.setFont(&FONT_MEDIUM);
  canvas.setCursor(ox + 18, oy + 14); canvas.print(age); canvas.print("d");

  pushCanvasToRLCD(false);
}

// ===== PAGE 8: SUN POSITION =====
void drawSunArcPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1); canvas.drawRect(1, 1, W-2, H-2, 1);
  canvas.setFont(&FONT_SMALL); canvas.setTextColor(1);
  canvas.setCursor(12, 25); canvas.print("POSICION SOL  "); canvas.print(getDisplayLocation());

  if (!astroData.valid) {
    canvas.setFont(&FONT_MEDIUM); canvas.setCursor(80, 160); canvas.print("SIN DATOS ASTRON.");
    pushCanvasToRLCD(false); return;
  }

  const int cx = 200, cy = 200, r = 145, arcMarginX = 28;
  canvas.fillRect(arcMarginX, cy, W - arcMarginX*2, 2, 1);
  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(arcMarginX, cy - 6); canvas.print("E");
  canvas.setCursor(W - arcMarginX - 8, cy - 6); canvas.print("W");

  for (int deg = 0; deg <= 180; deg++) {
    float rad = deg * 3.14159f / 180.0f;
    int x = cx - (int)(r * cosf(rad)), y = cy - (int)(r * sinf(rad));
    if (x >= 0 && x < W && y >= 0 && y < H) canvas.drawPixel(x, y, 1);
  }

  int noonX = cx;
  for (int y = cy - r; y < cy; y += 5) canvas.drawFastVLine(noonX, y, 3, 1);
  canvas.setFont(&FONT_SMALL); canvas.setCursor(noonX - 18, cy - r - 6); canvas.print("MEDIODIA");

  int sunriseMins = parseTimeToMinutes(astroData.sunrise);
  int sunsetMins  = parseTimeToMinutes(astroData.sunset);
  int nowMins     = hour24 * 60 + minuteVal;
  int dayLen      = sunsetMins - sunriseMins;
  if (dayLen <= 0) dayLen = 1;  // guard against bad API data

  // Sunrise/sunset times â€” 10px below horizon line so they don't touch it
  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(arcMarginX + 2, cy + 20); canvas.print(astroData.sunrise);
  canvas.setCursor(W - arcMarginX - 52, cy + 20); canvas.print(astroData.sunset);

  bool isDaytime = (nowMins >= sunriseMins && nowMins <= sunsetMins);

  if (isDaytime) {
    float sunAngleDeg = (float)(nowMins - sunriseMins) / (float)dayLen * 180.0f;
    float sunRad = sunAngleDeg * 3.14159f / 180.0f;
    int sunX = cx - (int)(r * cosf(sunRad)), sunY = cy - (int)(r * sinf(sunRad));
    canvas.fillCircle(sunX, sunY, 9, 1); canvas.fillCircle(sunX, sunY, 5, 0);
    for (int i = 0; i < 8; i++) {
      float a = i * 3.14159f / 4.0f;
      canvas.drawLine(sunX+(int)(11*cosf(a)), sunY+(int)(11*sinf(a)), sunX+(int)(15*cosf(a)), sunY+(int)(15*sinf(a)), 1);
    }
  } else {
    int nightX = (nowMins < sunriseMins) ? cx - r : cx + r;
    canvas.drawCircle(nightX, cy + 12, 8, 1);
    canvas.setFont(&FONT_SMALL); canvas.setCursor(cx - 20, cy + 26); canvas.print("NOCHE");
  }

  // Divider and bottom info panel â€” pushed down to give clear space from arc times
  canvas.fillRect(8, 230, 384, 2, 1);
  int dlH = dayLen / 60, dlM = dayLen % 60, noonMins = sunriseMins + dayLen / 2;
  String countdownLabel, countdownVal;
  if (isDaytime) {
    int ml = sunsetMins - nowMins; countdownLabel = "PUESTA EN"; countdownVal = String(ml/60) + "h " + String(ml%60) + "m";
  } else if (nowMins < sunriseMins) {
    int ml = sunriseMins - nowMins; countdownLabel = "SALIDA EN"; countdownVal = String(ml/60) + "h " + String(ml%60) + "m";
  } else {
    int ml = (sunriseMins + 1440) - nowMins; countdownLabel = "SALIDA EN"; countdownVal = String(ml/60) + "h " + String(ml%60) + "m";
  }

  int col1 = 14, col2 = 148, col3 = 282;
  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(col1, 250); canvas.print("DURACION DIA");
  canvas.setCursor(col2, 250); canvas.print("MEDIODIA");
  canvas.setCursor(col3, 250); canvas.print(countdownLabel);
  canvas.setFont(&FONT_MEDIUM);
  canvas.setCursor(col1, 270); canvas.print(dlH); canvas.print("h "); canvas.print(dlM); canvas.print("m");
  canvas.setCursor(col2, 270); canvas.print(minutesToTimeStr(noonMins));
  canvas.setCursor(col3, 270); canvas.print(countdownVal);
  canvas.fillRect(140, 230, 1, 42, 1); canvas.fillRect(274, 230, 1, 42, 1);
  pushCanvasToRLCD(false);
}

// ===== PAGE 13: SYSTEM INFO =====
void drawSystemPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1); canvas.drawRect(1, 1, W-2, H-2, 1);

  canvas.setFont(&FONT_SMALL); canvas.setTextColor(1);
  canvas.setCursor(12, 24); canvas.print("SISTEMA");

  // 5 rows at 44px each â€” label at y+12, value at y+32, 18px gap between them
  const int lx = 14, rx = 204, rowH = 44, gy0 = 36;
  canvas.fillRect(198, gy0, 2, rowH * 5, 1);

  auto drawDetail = [&](int x, int y, const char* lbl, String val, bool useSmall = false) {
    canvas.setFont(&FONT_SMALL);  canvas.setCursor(x, y+12); canvas.print(lbl);
    canvas.setFont(useSmall ? &FONT_SMALL : &FONT_MEDIUM); canvas.setCursor(x, y+32); canvas.print(val);
  };

  // Build value strings
  String wifiVal = wifiConnected ? String(wifiRSSI) + " dBm" : "Sin red";
  String ipStr   = wifiConnected ? WiFi.localIP().toString() : "Sin conectar";
  String wxVal   = "Sin datos";
  if (weatherData.valid) {
    unsigned long nf = 1800000UL - min((unsigned long)1800000UL, millis() - weatherData.lastUpdate);
    wxVal = String(nf/60000) + "m " + String((nf%60000)/1000) + "s";
  }
  unsigned long upSec = millis() / 1000;
  char uptimeStr[12];
  sprintf(uptimeStr, "%02d:%02d:%02d", (int)(upSec/3600), (int)((upSec%3600)/60), (int)(upSec%60));
  String ntpStr = "Nunca";
  if (ntpLastSync > 0) {
    unsigned long syncAgo = (millis() - ntpLastSync) / 60000;
    ntpStr = syncAgo < 60 ? String(syncAgo) + " min" : String(syncAgo/60) + " h";
  }
  String ssidStr = (wifiConnected && WiFi.SSID().length() > 0) ? WiFi.SSID() : "--";
  if (ssidStr.length() > 20) ssidStr = ssidStr.substring(0, 20); // FONT_SMALL safe up to ~26 chars
  int total = sensorReadCount + sensorFailCount;
  String fwStr = currentFirmwareName;
  if (fwStr.length() == 0) fwStr = "N/D";
  if (fwStr.length() > 20) fwStr = fwStr.substring(0, 20);

  int gy = gy0;
  drawDetail(lx, gy, "WIFI",         wifiVal);
  drawDetail(rx, gy, "IP",   ipStr);       gy += rowH;
  drawDetail(lx, gy, "CLIMA",      weatherData.valid ? "Reciente" : "Sin datos");
  drawDetail(rx, gy, "REF CLIMA",   wxVal);        gy += rowH;
  drawDetail(lx, gy, "ENCENDIDO",       String(uptimeStr));
  drawDetail(rx, gy, "SYNC NTP",     ntpStr);       gy += rowH;
  drawDetail(lx, gy, "RED",      ssidStr, true);
  drawDetail(rx, gy, "LECTURAS",  String(total));      gy += rowH;
  drawDetail(lx, gy, "SENSOR",       (sensorFailCount == 0) ? "OK" : "Errores");
  drawDetail(rx, gy, "FIRMWARE", fwStr, true);

  canvas.fillRect(8, 264, 384, 2, 1);
  pushCanvasToRLCD(false);
}

// ===== PAGE 4: HOURLY FORECAST ===== (note: function defined here, called from draw())
void drawHourlyPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1); canvas.drawRect(1, 1, W-2, H-2, 1);
  canvas.setFont(&FONT_SMALL); canvas.setTextColor(1);
  canvas.setCursor(12, 24); canvas.print("PRONOSTICO HORA  "); canvas.print(getDisplayLocation());

  if (!hourlyData.valid) {
    canvas.setFont(&FONT_MEDIUM); canvas.setCursor(80, 150); canvas.print("SIN DATOS HORA");
    pushCanvasToRLCD(false); return;
  }

  const int cols = 6, colW = 62, colGap = 2, startX = 11;
  const int topY = 36, hdrH = 24, rowH = 40, totalH = hdrH + rowH * 5;
  const int yTime = topY, yTemp = yTime+hdrH, yRainPc = yTemp+rowH, yRainMM = yRainPc+rowH, yUV = yRainMM+rowH, yWind = yUV+rowH;

  for (int i = 0; i < cols; i++) {
    int cx = startX + i * (colW + colGap);
    canvas.drawRect(cx, topY, colW, totalH, 1);
    canvas.fillRect(cx+1, yTime+1, colW-2, hdrH-2, 1);
    canvas.setTextColor(0); canvas.setFont(&FONT_SMALL);
    canvas.setCursor(cx+5, yTime+17); canvas.print(hourlyData.time[i]);
    canvas.setTextColor(1); canvas.setFont();
    int16_t lbx, lby; uint16_t lbw, lbh;
    canvas.getTextBounds("TEMP", 0, 0, &lbx, &lby, &lbw, &lbh);
    canvas.setCursor(cx + ((colW - (int)lbw) / 2), yTemp+11);   canvas.print("TEMP");
    canvas.setFont(&FONT_MEDIUM);
    canvas.setCursor(cx+2, yTemp+37);   canvas.print((int)hourlyData.temp[i]); canvas.print("c");
    canvas.setFont();
    canvas.getTextBounds("LLUVIA", 0, 0, &lbx, &lby, &lbw, &lbh);
    canvas.setCursor(cx + ((colW - (int)lbw) / 2), yRainPc+11); canvas.print("LLUVIA");
    canvas.setFont(&FONT_MEDIUM);
    canvas.setCursor(cx+2, yRainPc+37); canvas.print(hourlyData.rainChance[i]); canvas.print("%");
    canvas.setFont();
    canvas.getTextBounds("MM", 0, 0, &lbx, &lby, &lbw, &lbh);
    canvas.setCursor(cx + ((colW - (int)lbw) / 2), yRainMM+11); canvas.print("MM");
    canvas.setFont(&FONT_MEDIUM);
    canvas.setCursor(cx+2, yRainMM+37); canvas.print(hourlyData.rainMM[i], 1);
    canvas.setFont();
    canvas.getTextBounds("UV", 0, 0, &lbx, &lby, &lbw, &lbh);
    canvas.setCursor(cx + ((colW - (int)lbw) / 2), yUV+11);     canvas.print("UV");
    canvas.setFont(&FONT_MEDIUM);
    canvas.setCursor(cx+2, yUV+37);     canvas.print(hourlyData.uvIndex[i], 1);
    canvas.setFont();
    canvas.getTextBounds("VIENTO", 0, 0, &lbx, &lby, &lbw, &lbh);
    canvas.setCursor(cx + ((colW - (int)lbw) / 2), yWind+11);   canvas.print("VIENTO");
    canvas.setFont(&FONT_MEDIUM);
    canvas.setCursor(cx+2, yWind+37);   canvas.print((int)hourlyData.windSpeed[i]); canvas.print("k");
  }

  // No divider line â€” footer sits cleanly below the grid
  canvas.setFont(&FONT_SMALL); canvas.setCursor(12, 285);
  canvas.print("Act ");
  if (weatherData.valid) { canvas.print((millis() - weatherData.lastUpdate) / 60000); canvas.print("m"); }
  else { canvas.print("--"); }
  pushCanvasToRLCD(false);
}

// ===== GRAPH HELPERS =====
GraphBounds calcGraphBounds(float* data, int count, float minRange, float pad) {
  GraphBounds b = { 999.0f, -999.0f, 0.0f };
  for (int i = 0; i < count; i++) { if (data[i] < b.mn) b.mn = data[i]; if (data[i] > b.mx) b.mx = data[i]; }
  b.rng = b.mx - b.mn;
  if (b.rng < minRange) b.rng = minRange;
  b.mn -= pad; b.mx += pad; b.rng = b.mx - b.mn;
  return b;
}

int calcTrend(float* data, int si, int pts) {
  if (pts < 3) return 0;
  float delta = data[(si + pts - 1) % HISTORY_SIZE] - data[(si + pts - 3) % HISTORY_SIZE];
  if (delta > 0.5f) return 1; if (delta < -0.5f) return -1; return 0;
}

void drawTrendArrow(int x, int y, int trend) {
  if (trend == 1)       { canvas.fillTriangle(x, y-8, x-5, y, x+5, y, 1); canvas.fillRect(x-2, y, 4, 5, 1); }
  else if (trend == -1) { canvas.fillTriangle(x, y+8, x-5, y, x+5, y, 1); canvas.fillRect(x-2, y-5, 4, 5, 1); }
  else                  { canvas.fillRect(x-6, y-3, 12, 2, 1); canvas.fillRect(x-6, y+1, 12, 2, 1); }
}

void drawEnhancedGraph(float* data, int si, int pts, int gX, int gY, int gW, int gH, GraphBounds b, const char* unit) {
  canvas.drawRect(gX-1, gY-1, gW+2, gH+2, 1);
  for (int i = 1; i <= 3; i++) canvas.drawLine(gX, gY+(i*gH/4), gX+gW, gY+(i*gH/4), 1);
  for (int i = 0; i <= 4; i++) canvas.drawLine(gX+(i*gW/4), gY, gX+(i*gW/4), gY+gH, 1);

  int minIdx = -1, maxIdx = -1; float minVal = 9999.0f, maxVal = -9999.0f;
  for (int i = 0; i < pts; i++) {
    float v = data[(si+i) % HISTORY_SIZE];
    if (v < minVal) { minVal = v; minIdx = i; }
    if (v > maxVal) { maxVal = v; maxIdx = i; }
  }
  for (int i = 0; i < pts-1; i++) {
    int i1 = (si+i) % HISTORY_SIZE, i2 = (si+i+1) % HISTORY_SIZE;
    int x1 = gX+(i*gW/(HISTORY_SIZE-1)), x2 = gX+((i+1)*gW/(HISTORY_SIZE-1));
    int y1 = gY+gH-(int)((data[i1]-b.mn)/b.rng*gH), y2 = gY+gH-(int)((data[i2]-b.mn)/b.rng*gH);
    canvas.drawLine(x1, y1, x2, y2, 1); canvas.drawLine(x1, y1-1, x2, y2-1, 1);
  }
  if (minIdx >= 0) {
    int mx = gX+(minIdx*gW/(HISTORY_SIZE-1)), my = gY+gH-(int)((minVal-b.mn)/b.rng*gH);
    canvas.fillTriangle(mx, my+2, mx-4, my-5, mx+4, my-5, 1);
    canvas.setFont(&FONT_SMALL); canvas.setCursor(constrain(mx-8,gX,gX+gW-20), my+14); canvas.print(minVal, 1);
  }
  if (maxIdx >= 0) {
    int mx = gX+(maxIdx*gW/(HISTORY_SIZE-1)), my = gY+gH-(int)((maxVal-b.mn)/b.rng*gH);
    canvas.fillTriangle(mx, my-2, mx-4, my+5, mx+4, my+5, 1);
    canvas.setFont(&FONT_SMALL); canvas.setCursor(constrain(mx-8,gX,gX+gW-20), my-5); canvas.print(maxVal, 1);
  }
  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(gX-38, gY+5);      canvas.print(b.mx, 0); canvas.print(unit);
  canvas.setCursor(gX-38, gY+gH/2+4); canvas.print(((b.mx+b.mn)/2.0f), 0); canvas.print(unit);
  canvas.setCursor(gX-38, gY+gH-2);   canvas.print(b.mn, 0); canvas.print(unit);
  const char* xLabels[] = { "6h", "4.5h", "3h", "1.5h", "0h" };
  for (int i = 0; i <= 4; i++) { int lx = gX+(i*gW/4)-(i==4?12:6); canvas.setCursor(lx, gY+gH+14); canvas.print(xLabels[i]); }
}

// ===== PAGE 11: TEMP GRAPH =====
void drawTempGraphPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1); canvas.drawRect(1, 1, W-2, H-2, 1);
  drawThermometerIcon(16, 14);
  canvas.setFont(&FONT_SMALL); canvas.setTextColor(1);
  canvas.setCursor(38, 22); canvas.print("TEMP INTERIOR");
  canvas.setCursor(38, 38); canvas.print("HISTORIAL 6 HORAS");
  // Current value â€” FONT_LARGE fits header box without clipping
  canvas.setFont(&FONT_LARGE); canvas.setCursor(220, 38); canvas.print(temperature, 1);
  canvas.setFont(&FONT_SMALL); canvas.print(" C");
  int tTrend = calcTrend(history.tempHistory, history.currentIndex, history.sampleCount);
  drawTrendArrow(375, 22, tTrend);

  if (history.sampleCount < 2) {
    canvas.setFont(&FONT_MEDIUM); canvas.setCursor(85, 155); canvas.print("RECOPILANDO DATOS");
    canvas.setFont(&FONT_SMALL);  canvas.setCursor(95, 178); canvas.print("Grafico en 15 min");
    pushCanvasToRLCD(false); return;
  }

  int gX = 44, gY = 52, gW = 336, gH = 168;
  GraphBounds b = calcGraphBounds(history.tempHistory, HISTORY_SIZE, 5.0f, 1.0f);
  drawEnhancedGraph(history.tempHistory, history.currentIndex, min((int)history.sampleCount,(int)HISTORY_SIZE), gX, gY, gW, gH, b, "c");

  // Bottom stats bar â€” verified column positions, min 7px between all items
  canvas.drawRect(8, 244, 384, 24, 1);
  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(8,   261); canvas.print("0h:");
  canvas.setCursor(39,  261); canvas.print(temperature, 1); canvas.print("c");
  canvas.setCursor(90,  261); canvas.print("MIN:");
  canvas.setCursor(134, 261); canvas.print(b.mn+1.0f, 1); canvas.print("c");
  canvas.setCursor(200, 261); canvas.print("MAX:");
  canvas.setCursor(250, 261); canvas.print(b.mx-1.0f, 1); canvas.print("c");
  canvas.setCursor(308, 261);
  if (tTrend == 1) canvas.print("SUBE"); else if (tTrend == -1) canvas.print("BAJA"); else canvas.print("ESTABLE");
  pushCanvasToRLCD(false);
}

// ===== PAGE 12: HUMIDITY GRAPH =====
void drawHumidityGraphPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1); canvas.drawRect(1, 1, W-2, H-2, 1);
  drawDropletIcon(16, 14);
  canvas.setFont(&FONT_SMALL); canvas.setTextColor(1);
  canvas.setCursor(38, 22); canvas.print("HUMEDAD INTERIOR");
  canvas.setCursor(38, 38); canvas.print("HISTORIAL 6 HORAS");
  // Current value â€” FONT_LARGE fits header box without clipping
  canvas.setFont(&FONT_LARGE); canvas.setCursor(220, 38); canvas.print((int)humidity);
  canvas.setFont(&FONT_SMALL); canvas.print(" %");
  int hTrend = calcTrend(history.humidityHistory, history.currentIndex, history.sampleCount);
  drawTrendArrow(375, 22, hTrend);

  if (history.sampleCount < 2) {
    canvas.setFont(&FONT_MEDIUM); canvas.setCursor(85, 155); canvas.print("RECOPILANDO DATOS");
    canvas.setFont(&FONT_SMALL);  canvas.setCursor(95, 178); canvas.print("Grafico en 15 min");
    pushCanvasToRLCD(false); return;
  }

  int gX = 44, gY = 52, gW = 336, gH = 168;
  GraphBounds b = calcGraphBounds(history.humidityHistory, HISTORY_SIZE, 10.0f, 2.0f);
  if (b.mn < 0.0f) b.mn = 0.0f; if (b.mx > 100.0f) b.mx = 100.0f;
  b.rng = b.mx - b.mn; if (b.rng < 1.0f) b.rng = 1.0f;
  drawEnhancedGraph(history.humidityHistory, history.currentIndex, min((int)history.sampleCount,(int)HISTORY_SIZE), gX, gY, gW, gH, b, "%");

  // Bottom stats bar â€” verified column positions, min 7px between all items
  canvas.drawRect(8, 244, 384, 24, 1);
  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(8,   261); canvas.print("0h:");
  canvas.setCursor(39,  261); canvas.print((int)humidity); canvas.print("%");
  canvas.setCursor(90,  261); canvas.print("MIN:");
  canvas.setCursor(134, 261); canvas.print(b.mn+2.0f, 1); canvas.print("%");
  canvas.setCursor(200, 261); canvas.print("MAX:");
  canvas.setCursor(250, 261); canvas.print(b.mx-2.0f, 1); canvas.print("%");
  canvas.setCursor(308, 261);
  if (hTrend == 1) canvas.print("SUBE"); else if (hTrend == -1) canvas.print("BAJA"); else canvas.print("ESTABLE");
  pushCanvasToRLCD(false);
}

// ===== SEASONS HELPER =====
// Returns day of year (1-365)
int dayOfYear(int day, int month, int year) {
  const int dpm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  int doy = 0;
  for (int m = 0; m < month - 1; m++) doy += dpm[m];
  if (leap && month > 2) doy++;
  return doy + day;
}

// Approximate solstice/equinox day-of-year for a given year
// Returns {marchEq, juneSol, septEq, decSol}
SeasonEvents calcSeasonEvents(int year) {
  // Simplified approximation â€” accurate to within 1-2 days
  SeasonEvents e;
  float y = year + 0.5f;
  e.marchEq = (int)(79.3125f + 0.2422f * (y - 2000) - (int)((y - 2000) / 4.0f));
  e.juneSol = (int)(171.3125f + 0.2422f * (y - 2000) - (int)((y - 2000) / 4.0f));
  e.septEq  = (int)(264.3125f + 0.2422f * (y - 2000) - (int)((y - 2000) / 4.0f));
  e.decSol  = (int)(354.3125f + 0.2422f * (y - 2000) - (int)((y - 2000) / 4.0f));
  return e;
}

// Southern Hemisphere season from day of year
SeasonInfo getSeasonInfo(int doy, int year) {
  SeasonEvents e = calcSeasonEvents(year);
  SeasonInfo s;
  if (doy >= e.decSol || doy < e.marchEq) {
    int since = doy >= e.decSol ? doy - e.decSol : doy + (365 - e.decSol);
    int until = doy >= e.decSol ? (e.marchEq + 365 - doy) : (e.marchEq - doy);
    s.name = "VERANO"; s.daysSince = since; s.daysUntil = until;
    s.nextEvent = "Equinoccio Otono"; s.nextEventDoy = e.marchEq;
  } else if (doy >= e.marchEq && doy < e.juneSol) {
    s.name = "OTONO"; s.daysSince = doy - e.marchEq; s.daysUntil = e.juneSol - doy;
    s.nextEvent = "Solsticio Invierno"; s.nextEventDoy = e.juneSol;
  } else if (doy >= e.juneSol && doy < e.septEq) {
    s.name = "INVIERNO"; s.daysSince = doy - e.juneSol; s.daysUntil = e.septEq - doy;
    s.nextEvent = "Equinoccio Primavera"; s.nextEventDoy = e.septEq;
  } else {
    s.name = "PRIMAVERA"; s.daysSince = doy - e.septEq; s.daysUntil = e.decSol - doy;
    s.nextEvent = "Solsticio Verano"; s.nextEventDoy = e.decSol;
  }
  return s;
}

// Convert day-of-year to date string "DD Mon"
String doyToDateStr(int doy, int year) {
  int dpm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  const char* months[] = {"Ene","Feb","Mar","Abr","May","Jun","Jul","Ago","Sep","Oct","Nov","Dic"};
  bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  if (leap) dpm[1] = 29;
  int m = 0;
  int days = doy;
  while (m < 12 && days > dpm[m]) { days -= dpm[m]; m++; }
  char buf[10];
  snprintf(buf, sizeof(buf), "%d %s", days, months[m]);
  return String(buf);
}

// ===== PAGE 9: EARTH & SEASONS =====
void drawSeasonsPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1);
  canvas.drawRect(1, 1, W-2, H-2, 1);
  canvas.setFont(&FONT_SMALL); canvas.setTextColor(1);
  canvas.setCursor(12, 24); canvas.print("TIERRA Y ESTACIONES  "); canvas.print(getDisplayLocation());

  int day   = rtc.getDay();
  int month = rtc.getMonth();
  int year  = rtc.getYear();
  int doy   = dayOfYear(day, month, year);
  SeasonEvents e = calcSeasonEvents(year);
  SeasonInfo   s = getSeasonInfo(doy, year);

  // ===== ORBIT DIAGRAM â€” full height, no bottom panel =====
  // Centre at y=158, orx=130, ory=100 â€” fits within border with clearance for outside labels
  const int ox = 200, oy = 158, orx = 130, ory = 100;

  // Orbit ellipse
  for (int deg = 0; deg < 360; deg += 1) {
    float rad = deg * 3.14159f / 180.0f;
    int x = ox + (int)(orx * cosf(rad));
    int y = oy + (int)(ory * sinf(rad));
    if (x >= 0 && x < W && y >= 0 && y < H) canvas.drawPixel(x, y, 1);
  }

  // Sun at centre
  canvas.fillCircle(ox, oy, 7, 1);
  canvas.fillCircle(ox, oy, 3, 0);
  for (int i = 0; i < 8; i++) {
    float a = i * 3.14159f / 4.0f;
    canvas.drawLine(ox+(int)(9*cosf(a)), oy+(int)(9*sinf(a)),
                    ox+(int)(13*cosf(a)), oy+(int)(13*sinf(a)), 1);
  }
  canvas.setFont(&FONT_SMALL); canvas.setTextColor(0);
  canvas.setCursor(ox-4, oy+4); canvas.print("S");
  canvas.setTextColor(1);

  // Season markers â€” EQU/SOL + date, mathematically positioned clear of orbit and borders
  struct { int mdoy; const char* lbl; float angle; } markers[] = {
    { e.marchEq, "EQU", 0.0f   },
    { e.juneSol, "SOL", 270.0f },
    { e.septEq,  "EQU", 180.0f },
    { e.decSol,  "SOL", 90.0f  },
  };

  for (int i = 0; i < 4; i++) {
    float rad = markers[i].angle * 3.14159f / 180.0f;
    int mx = ox + (int)(orx * cosf(rad));
    int my = oy + (int)(ory * sinf(rad));
    canvas.fillCircle(mx, my, 4, 1);
    int tx, ty;
    // Right (MAR EQ) â€” both rows above dot, 8px clear of orbit
    if (markers[i].angle == 0.0f)   { tx = mx + 10; ty = my - 25; }
    // Left (SEP EQ) â€” pushed fully left of orbit; '22 Sep'=57px wide, mx=70, need tx<5
    if (markers[i].angle == 180.0f) { tx = mx - 68; ty = my - 25; }
    // Top (JUN SOL) â€” below dot inside ellipse, pushed down clear of orbit
    if (markers[i].angle == 270.0f) { tx = mx - 16; ty = my + 22; }
    // Bottom (DEC SOL) â€” both rows above dot, 9px clear of orbit
    if (markers[i].angle == 90.0f)  { tx = mx - 16; ty = my - 26; }
    canvas.setFont(&FONT_SMALL);
    canvas.setCursor(tx, ty);    canvas.print(markers[i].lbl);
    canvas.setCursor(tx, ty+14); canvas.print(doyToDateStr(markers[i].mdoy, year));
  }

  // Season names in the 4 corners â€” fixed positions outside the ellipse
  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(14,  52);  canvas.print("INVIERNO");  // top-left  (JUN SOL side)
  canvas.setCursor(316, 52);  canvas.print("OTONO");  // top-right (MAR EQ side)
  canvas.setCursor(14,  272); canvas.print("PRIMAVERA");  // bot-left  (SEP EQ side)
  canvas.setCursor(316, 272); canvas.print("VERANO");  // bot-right (DEC SOL side)

  // Earth position
  float earthAngle;
  if      (doy >= e.decSol)                  earthAngle = 90.0f  - (float)(doy - e.decSol)    / (float)(e.marchEq + 365 - e.decSol) * 90.0f;
  else if (doy < e.marchEq)                  earthAngle = 90.0f  - (float)(doy + 365 - e.decSol) / (float)(e.marchEq + 365 - e.decSol) * 90.0f;
  else if (doy >= e.marchEq && doy < e.juneSol) earthAngle = 360.0f - (float)(doy - e.marchEq) / (float)(e.juneSol - e.marchEq) * 90.0f;
  else if (doy >= e.juneSol && doy < e.septEq)  earthAngle = 270.0f - (float)(doy - e.juneSol) / (float)(e.septEq  - e.juneSol) * 90.0f;
  else                                           earthAngle = 180.0f - (float)(doy - e.septEq)  / (float)(e.decSol  - e.septEq)  * 90.0f;

  float erad = earthAngle * 3.14159f / 180.0f;
  int ex = ox + (int)(orx * cosf(erad));
  int ey = oy + (int)(ory * sinf(erad));
  canvas.fillCircle(ex, ey, 6, 1);
  canvas.fillCircle(ex, ey, 3, 0);
  int elx = ex + (ex > ox ? 9 : -28);
  int ely = ey + (ey > oy ? 14 : -6);
  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(elx, ely);    canvas.print("AHORA");
  canvas.setCursor(elx, ely+12); canvas.print(s.daysUntil); canvas.print("d");

  pushCanvasToRLCD(false);
}
// ===== PAGE 10: SEASONS ORBIT DIAGRAM =====
void drawSeasonsOrbitPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1);
  canvas.drawRect(1, 1, W-2, H-2, 1);

  int day_  = rtc.getDay();
  int mon_  = rtc.getMonth();
  int year_ = rtc.getYear();
  int doy   = dayOfYear(day_, mon_, year_);
  SeasonEvents e = calcSeasonEvents(year_);
  SeasonInfo   s = getSeasonInfo(doy, year_);

  // Days until each event
  int dJun = e.juneSol - doy; if (dJun <= 0) dJun += 365;
  int dSep = e.septEq  - doy; if (dSep <= 0) dSep += 365;
  int dDec = e.decSol  - doy; if (dDec <= 0) dDec += 365;
  int dMar = e.marchEq - doy; if (dMar <= 0) dMar += 365;
  int minDays = dJun;
  if (dSep < minDays) minDays = dSep;
  if (dDec < minDays) minDays = dDec;
  if (dMar < minDays) minDays = dMar;

  // â”€â”€ Header â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  canvas.setFont(&FONT_SMALL); canvas.setTextColor(1);
  canvas.setCursor(12, 24);
  canvas.print("TIERRA Y ESTACIONES "); canvas.print(getDisplayLocation());

  String evtStr = String(s.nextEvent);
  int sp = evtStr.indexOf(' ');
  if (sp > 0) evtStr = evtStr.substring(sp + 1);
  evtStr.replace("Equinox",  "EQ");
  evtStr.replace("Solstice", "SOL");
  String evtFull = evtStr + " " + doyToDateStr(s.nextEventDoy, year_);
  int16_t ex1, ey1; uint16_t etw, eth;
  canvas.getTextBounds(evtFull.c_str(), 0, 0, &ex1, &ey1, &etw, &eth);
  canvas.setCursor(388 - (int)etw, 24);
  canvas.print(evtFull);

  // â”€â”€ Corner countdown labels â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  canvas.setFont(&FONT_SMALL);
  int cDays[4]  = { dSep, dJun, dDec, dMar }; // TL=SEP EQU, TR=JUN SOL, BL=DEC SOL, BR=MAR EQU
  int cX[4]     = { 10,   390,  10,   390  };
  int cY[4]     = { 52,   52,   261,  261  };
  bool cRight[4]= { false,true, false,true  };
  for (int i = 0; i < 4; i++) {
    String lbl = (cDays[i] == 0) ? String("HOY") : String(cDays[i]) + "d";
    int16_t bx1, by1; uint16_t btw, bth;
    canvas.getTextBounds(lbl.c_str(), 0, 0, &bx1, &by1, &btw, &bth);
    int tx = cRight[i] ? cX[i] - (int)btw - 4 : cX[i] + 2;
    canvas.setCursor(tx, cY[i]);
    canvas.print(lbl);
    if (cDays[i] == minDays) {
      canvas.drawRect(tx - 4, cY[i] - 16, (int)btw + 8, 23, 1);
    }
  }

  // â”€â”€ Orbit geometry â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  const int ocx = 200, ocy = 158, orx = 130, ory = 100;

  // â”€â”€ Quadrant fills (scanline) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  for (int fy = 0; fy < H; fy++) {
    float fdy = fy - ocy;
    float disc = 1.0f - (fdy * fdy) / (float)(ory * ory);
    if (disc < 0.0f) continue;
    float fex = orx * sqrtf(disc);
    for (int fx = ocx - (int)fex; fx <= ocx + (int)fex; fx++) {
      float fdx = fx - ocx;
      float fangle = atan2f(fdy, fdx) * 180.0f / 3.14159f;
      if (fangle < 0.0f) fangle += 360.0f;
      // All 4 quadrants = entire ellipse interior
      canvas.drawPixel(fx, fy, 1);
    }
  }

  // â”€â”€ Quadrant dividing lines (crosshairs) drawn white to split fills â”€â”€â”€â”€â”€â”€â”€
  // Horizontal line (SEP EQ to MAR EQ)
  for (int fx = ocx - orx + 2; fx <= ocx + orx - 2; fx++)
    canvas.drawPixel(fx, ocy, 0);
  // Vertical line (JUN SOL to DEC SOL)
  for (int fy = ocy - ory + 2; fy <= ocy + ory - 2; fy++)
    canvas.drawPixel(ocx, fy, 0);

  // â”€â”€ Dashed white crosshairs over the solid white lines â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  for (int fx = ocx - orx + 2; fx < ocx + orx - 2; fx += 9) {
    for (int k = 0; k < 5 && fx+k < ocx+orx-2; k++)
      canvas.drawPixel(fx+k, ocy, 0);
  }
  for (int fy = ocy - ory + 2; fy < ocy + ory - 2; fy += 9) {
    for (int k = 0; k < 5 && fy+k < ocy+ory-2; k++)
      canvas.drawPixel(ocx, fy+k, 0);
  }

  // â”€â”€ Ellipse outline: white gap then black â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  for (int deg = 0; deg < 360; deg++) {
    float rd = deg * 3.14159f / 180.0f;
    canvas.drawPixel(ocx + (int)(orx * cosf(rd)), ocy + (int)(ory * sinf(rd)), 0);
  }
  for (int deg = 0; deg < 360; deg++) {
    float rd = deg * 3.14159f / 180.0f;
    canvas.drawPixel(ocx + (int)(orx * cosf(rd)), ocy + (int)(ory * sinf(rd)), 1);
  }

  // â”€â”€ Earth angle â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  float earthAngle;
  if      (doy >= e.decSol)                        earthAngle = 90.0f  - (float)(doy - e.decSol)       / (float)(e.marchEq + 365 - e.decSol) * 90.0f;
  else if (doy < e.marchEq)                        earthAngle = 90.0f  - (float)(doy + 365 - e.decSol) / (float)(e.marchEq + 365 - e.decSol) * 90.0f;
  else if (doy >= e.marchEq && doy < e.juneSol)    earthAngle = 360.0f - (float)(doy - e.marchEq)      / (float)(e.juneSol - e.marchEq)      * 90.0f;
  else if (doy >= e.juneSol && doy < e.septEq)     earthAngle = 270.0f - (float)(doy - e.juneSol)      / (float)(e.septEq  - e.juneSol)      * 90.0f;
  else                                              earthAngle = 180.0f - (float)(doy - e.septEq)       / (float)(e.decSol  - e.septEq)       * 90.0f;

  // â”€â”€ Progress arc (white, inside orbit at 88%) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  float nextAngle;
  if      (dMar == minDays) nextAngle = 0.0f;
  else if (dDec == minDays) nextAngle = 90.0f;
  else if (dSep == minDays) nextAngle = 180.0f;
  else                      nextAngle = 270.0f;

  float arcRX = orx * 0.88f, arcRY = ory * 0.88f;
  float sweepEnd = nextAngle;
  if (sweepEnd > earthAngle) sweepEnd -= 360.0f;
  for (float aa = earthAngle; aa >= sweepEnd; aa -= 0.5f) {
    float rd = aa * 3.14159f / 180.0f;
    int apx = ocx + (int)(arcRX * cosf(rd));
    int apy = ocy + (int)(arcRY * sinf(rd));
    canvas.drawPixel(apx,   apy,   0);
    canvas.drawPixel(apx,   apy-1, 0);
    canvas.drawPixel(apx,   apy+1, 0);
  }

  // â”€â”€ Sun at centre â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  canvas.fillCircle(ocx, ocy, 16, 0);
  canvas.fillCircle(ocx, ocy, 9,  1);
  canvas.fillCircle(ocx, ocy, 5,  0);
  for (int i = 0; i < 8; i++) {
    float ra = i * 3.14159f / 4.0f;
    canvas.drawLine(ocx+(int)(11*cosf(ra)), ocy+(int)(11*sinf(ra)),
                    ocx+(int)(15*cosf(ra)), ocy+(int)(15*sinf(ra)), 1);
  }
  canvas.setTextColor(0); canvas.setCursor(ocx-4, ocy+4); canvas.print("S");
  canvas.setTextColor(1);

  // â”€â”€ Season names in white â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  const char* sNames[] = { "VERANO", "PRIMAVERA", "INVIERNO", "OTONO" };
  float       sAngles[] = { 45.0f, 135.0f, 225.0f, 315.0f };
  canvas.setFont(&FONT_SMALL);
  for (int i = 0; i < 4; i++) {
    float rd = sAngles[i] * 3.14159f / 180.0f;
    int snx = ocx + (int)(orx * 0.56f * cosf(rd));
    int sny = ocy + (int)(ory * 0.56f * sinf(rd));
    int16_t sx1, sy1; uint16_t stw, sth;
    canvas.getTextBounds(sNames[i], 0, 0, &sx1, &sy1, &stw, &sth);
    canvas.setTextColor(0);
    canvas.setCursor(snx - (int)stw/2, sny + 4);
    canvas.print(sNames[i]);
    canvas.setTextColor(1);
  }

  // â”€â”€ Month ticks + labels â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  const char* months[] = {"Mar","Abr","May","Jun","Jul","Ago","Sep","Oct","Nov","Dic","Ene","Feb"};
  canvas.setFont(&FONT_SMALL); canvas.setTextColor(1);
  for (int i = 0; i < 12; i++) {
    float mrad = -i * 30.0f * 3.14159f / 180.0f;
    int mix = ocx + (int)(orx * cosf(mrad));
    int miy = ocy + (int)(ory * sinf(mrad));
    int mtx = ocx + (int)((orx+9) * cosf(mrad));
    int mty = ocy + (int)((ory+9) * sinf(mrad));
    canvas.drawLine(mix, miy, mtx, mty, 1);
    float mlr  = (i == 0 || i == 6) ? orx + 30.0f : orx + 20.0f;
    float mlry = (i == 0 || i == 6) ? ory + 30.0f : ory + 20.0f;
    int mlx = ocx + (int)(mlr  * cosf(mrad));
    int mly = ocy + (int)(mlry * sinf(mrad));
    int16_t mx1, my1; uint16_t mtw, mth;
    canvas.getTextBounds(months[i], 0, 0, &mx1, &my1, &mtw, &mth);
    canvas.setCursor(mlx - (int)mtw/2, mly + 4);
    canvas.print(months[i]);
  }

  // â”€â”€ Bullseye markers at SOL/EQ points â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  int bullA[] = {0, 90, 180, 270};
  for (int i = 0; i < 4; i++) {
    float rd = bullA[i] * 3.14159f / 180.0f;
    int bpx = ocx + (int)(orx * cosf(rd));
    int bpy = ocy + (int)(ory * sinf(rd));
    canvas.fillCircle(bpx, bpy, 8, 0);
    canvas.drawCircle(bpx, bpy, 6, 1);
    canvas.drawCircle(bpx, bpy, 5, 1);
    canvas.fillCircle(bpx, bpy, 4, 0);
    canvas.fillCircle(bpx, bpy, 2, 1);
  }

  // â”€â”€ Earth dot â€” flashing â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  unsigned long nowMs = millis();
  if (nowMs - earthFlashLast >= 600) {
    earthFlashOn   = !earthFlashOn;
    earthFlashLast = nowMs;
  }
  if (earthFlashOn) {
    float erd = earthAngle * 3.14159f / 180.0f;
    int epx = ocx + (int)(orx * cosf(erd));
    int epy = ocy + (int)(ory * sinf(erd));
    canvas.fillCircle(epx, epy, 8, 0);
    canvas.drawCircle(epx, epy, 8, 1);
    canvas.fillCircle(epx, epy, 5, 1);
    canvas.fillCircle(epx, epy, 3, 0);
  }

  pushCanvasToRLCD(false);
}

void drawKumaDashboardPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1);
  canvas.drawRect(1, 1, W - 2, H - 2, 1);

  canvas.setTextColor(1);
  printCentered(&FONT_MEDIUM, 27, "Dashboard KUMA");

  if (!wifiConnected) {
    canvas.setFont(&FONT_SMALL);
    printCentered(&FONT_SMALL, 98, "Sin WiFi");
    printCentered(&FONT_SMALL, 122, "Conecta WiFi para ver monitores");
    pushCanvasToRLCD(false);
    return;
  }

  if (!kumaData.valid) {
    canvas.setFont(&FONT_SMALL);
    printCentered(&FONT_SMALL, 98, "Sin datos de Uptime Kuma");
    printCentered(&FONT_SMALL, 122, "Reintentando...");
    pushCanvasToRLCD(false);
    return;
  }

  const int contentTop = 36;
  const int contentBottom = H - 8;
  const int leftX = 10;
  const int rightX = W - 10;
  const int rowHeight = 33;
  const int groupHeight = 19;
  const int beansX = 252;
  const int beansY = 8;
  const int beanW = 4;
  const int beanH = 11;
  const int beanGap = 2;
  const int nameX = 24;
  const int nameMaxW = beansX - nameX - 8;
  int hiddenItems = 0;

  auto fitText = [&](String s, int maxW) {
    int16_t tx, ty;
    uint16_t tw, th;
    canvas.getTextBounds(s, 0, 0, &tx, &ty, &tw, &th);
    if ((int)tw <= maxW) return s;
    while (s.length() > 1) {
      s.remove(s.length() - 1);
      String t = s + ".";
      canvas.getTextBounds(t, 0, 0, &tx, &ty, &tw, &th);
      if ((int)tw <= maxW) return t;
    }
    return String(".");
  };

  int y = contentTop + 6;
  canvas.setFont(&FONT_SMALL);

  for (int gi = 0; gi < kumaData.groupCount; gi++) {
    const KumaGroup& g = kumaData.groups[gi];
    if (y + groupHeight >= contentBottom) {
      for (int rgi = gi; rgi < kumaData.groupCount; rgi++) hiddenItems += kumaData.groups[rgi].monitorCount;
      break;
    }

    canvas.fillRect(leftX, y, rightX - leftX, groupHeight, 1);
    canvas.setTextColor(0);
    canvas.setCursor(leftX + 4, y + 14);
    canvas.print(fitText(g.name, rightX - leftX - 8));
    canvas.setTextColor(1);
    y += groupHeight + 2;

    for (int mi = 0; mi < g.monitorCount; mi++) {
      if (y + rowHeight >= contentBottom) {
        hiddenItems += (g.monitorCount - mi);
        for (int rgi = gi + 1; rgi < kumaData.groupCount; rgi++) hiddenItems += kumaData.groups[rgi].monitorCount;
        break;
      }

      int midx = g.monitorIdx[mi];
      if (midx < 0 || midx >= kumaData.monitorCount) continue;
      const KumaMonitor& m = kumaData.monitors[midx];

      canvas.drawFastHLine(leftX, y + rowHeight - 1, rightX - leftX, 1);

      String baseName = m.name;
      int paren = baseName.indexOf("(");
      if (paren > 0) {
        baseName = baseName.substring(0, paren);
        baseName.trim();
      }
      String name = fitText(baseName, nameMaxW);
      int nameBaselineY = y + (rowHeight / 2) + 6;
      canvas.setCursor(nameX, nameBaselineY);
      canvas.print(name);
      int by = y + beansY;
      for (int b = 0; b < KUMA_BEANS; b++) {
        int bx = beansX + b * (beanW + beanGap);
        if (m.beans[b] == 1) canvas.fillRect(bx, by, beanW, beanH, 1);
        else canvas.drawRect(bx, by, beanW, beanH, 1);
      }

      y += rowHeight;
    }
    y += 2;
  }

  if (hiddenItems > 0 && y < contentBottom - 2) {
    canvas.setCursor(leftX, contentBottom - 2);
    canvas.print("+");
    canvas.print(hiddenItems);
    canvas.print(" monitores mas...");
  }

  pushCanvasToRLCD(false);
}

void drawWifiNetworksPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1);
  canvas.drawRect(1, 1, W - 2, H - 2, 1);
  canvas.setTextColor(1);
  printCentered(&FONT_MEDIUM, 22, "Redes WiFi");

  if (!wifiConnected) {
    canvas.setFont(&FONT_SMALL);
    printCentered(&FONT_SMALL, 98, "Sin WiFi conectado");
    printCentered(&FONT_SMALL, 122, "No se puede escanear redes");
    pushCanvasToRLCD(false);
    return;
  }

  updateWifiScan();
  if (wifiScanInProgress) {
    pollWifiScanResults();
  }

  if (wifiScanInProgress || wifiScanCount == 0) {
    canvas.setFont(&FONT_MEDIUM);
    printCentered(&FONT_MEDIUM, 130, "Escaneando redes WiFi...");
    pushCanvasToRLCD(false);
    return;
  }

  const int left = 10;
  const int top = 40;
  const int gap = 8;
  const int colW = (W - left * 2 - gap);
  const int oneColW = colW / 2;
  const int rowH = 40;
  const int rowsPerCol = 6;

  auto fitText = [&](String s, int maxW) {
    int16_t tx, ty;
    uint16_t tw, th;
    canvas.getTextBounds(s, 0, 0, &tx, &ty, &tw, &th);
    if ((int)tw <= maxW) return s;
    while (s.length() > 1) {
      s.remove(s.length() - 1);
      String t = s + ".";
      canvas.getTextBounds(t, 0, 0, &tx, &ty, &tw, &th);
      if ((int)tw <= maxW) return t;
    }
    return String(".");
  };

  canvas.setFont(&FONT_SMALL);
  for (int col = 0; col < 2; col++) {
    int x = left + col * (oneColW + gap);
    for (int row = 0; row < rowsPerCol; row++) {
      int idx = col * rowsPerCol + row;
      int y = top + row * rowH;
      canvas.drawRect(x, y, oneColW, rowH - 2, 1);

      if (idx >= wifiScanCount) continue;
      const WifiScanEntry& n = wifiScanList[idx];
      String ssid = fitText(n.ssid, oneColW - 52);
      String enc = wifiEncShort(n.enc);

      canvas.setCursor(x + 6, y + 14);
      canvas.print(ssid);
      canvas.setCursor(x + 6, y + 30);
      canvas.print(enc);
      drawWiFiBarsCompact(x + oneColW - 28, y + 15, n.rssi);
    }
  }

  pushCanvasToRLCD(false);
}

void drawIssPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1);
  canvas.drawRect(1, 1, W - 2, H - 2, 1);
  canvas.setTextColor(1);
  printCentered(&FONT_MEDIUM, 22, "ISS");

  if (!wifiConnected) {
    canvas.setFont(&FONT_SMALL);
    printCentered(&FONT_SMALL, 108, "Sin WiFi");
    printCentered(&FONT_SMALL, 132, "No se puede consultar ISS");
    pushCanvasToRLCD(false);
    return;
  }

  const int mapX = 10;
  const int mapY = 34;
  const int mapW = W - 20;
  const int mapH = 176;
  const int mapInnerX = mapX + 1;
  const int mapInnerY = mapY + 1;
  canvas.drawRect(mapX, mapY, mapW, mapH, 1);

  auto lonToX = [&](float lon) -> int {
    while (lon > 180.0f) lon -= 360.0f;
    while (lon < -180.0f) lon += 360.0f;
    float n = (lon + 180.0f) / 360.0f;
    return mapInnerX + (int)(n * (float)(ISS_WORLD_MAP_W - 1));
  };
  auto latToY = [&](float lat) -> int {
    if (lat > 90.0f) lat = 90.0f;
    if (lat < -90.0f) lat = -90.0f;
    float n = (90.0f - lat) / 180.0f;
    return mapInnerY + (int)(n * (float)(ISS_WORLD_MAP_H - 1));
  };

  // Background world map with political borders (pre-rendered 1-bit bitmap).
  canvas.drawBitmap(mapInnerX, mapInnerY, ISS_WORLD_MAP_BITS, ISS_WORLD_MAP_W, ISS_WORLD_MAP_H, 1);

  if (issOrbitValid && issOrbitCount > 1) {
    for (int i = 1; i < issOrbitCount; i++) {
      float lonA = issOrbitLon[i - 1];
      float lonB = issOrbitLon[i];
      // Prevent a long line crossing the whole map at antimeridian wrap.
      if (fabsf(lonA - lonB) > 180.0f) continue;
      int x1 = lonToX(lonA);
      int y1 = latToY(issOrbitLat[i - 1]);
      int x2 = lonToX(lonB);
      int y2 = latToY(issOrbitLat[i]);
      canvas.drawLine(x1, y1, x2, y2, 1);
    }
  }

  if (detectedLocationValid) {
    int ux = lonToX(detectedLon);
    int uy = latToY(detectedLat);
    // House marker for local position.
    canvas.fillRect(ux - 3, uy - 1, 7, 6, 1);
    canvas.fillTriangle(ux - 5, uy - 1, ux + 5, uy - 1, ux, uy - 7, 1);
    canvas.drawRect(ux - 1, uy + 1, 3, 4, 0);
  }

  if (issData.valid) {
    int sx = lonToX(issData.longitude);
    int sy = latToY(issData.latitude);
    canvas.fillCircle(sx, sy, 4, 1);
    canvas.drawCircle(sx, sy, 7, 1);
    canvas.setFont(&FONT_SMALL);
    canvas.setCursor(sx + 9, sy + 4);
    canvas.print("ISS");
  }

  // Draw 1200 km home alert ring on top so it is always visible.
  if (detectedLocationValid) {
    const float kAlertRadiusKm = 1200.0f;
    int pxPrev = 0, pyPrev = 0;
    bool hasPrev = false;
    for (int a = 0; a <= 360; a += 4) {
      float clat = 0.0f, clon = 0.0f;
      destinationPointKm(detectedLat, detectedLon, (float)a, kAlertRadiusKm, clat, clon);
      int px = lonToX(clon);
      int py = latToY(clat);
      if (hasPrev) {
        if (abs(px - pxPrev) < (ISS_WORLD_MAP_W / 2)) {
          canvas.drawLine(pxPrev, pyPrev, px, py, 1);
          canvas.drawLine(pxPrev + 1, pyPrev, px + 1, py, 1); // thicken ring
        }
      }
      if ((a % 30) == 0) canvas.fillCircle(px, py, 1, 1);    // reference marks
      pxPrev = px;
      pyPrev = py;
      hasPrev = true;
    }
    int lx = lonToX(detectedLon) + 8;
    int ly = latToY(detectedLat) - 10;
    if (lx > (mapX + mapW - 58)) lx = mapX + mapW - 58;
    if (ly < (mapY + 10)) ly = mapY + 10;
    // No text label here; keep only the 1200 km visual ring.
  }

  canvas.setFont(&FONT_SMALL);
  int y0 = 228;
  auto fitLine = [&](String s, int maxW) {
    int16_t tx, ty;
    uint16_t tw, th;
    canvas.getTextBounds(s, 0, 0, &tx, &ty, &tw, &th);
    if ((int)tw <= maxW) return s;
    while (s.length() > 1) {
      s.remove(s.length() - 1);
      String t = s + ".";
      canvas.getTextBounds(t, 0, 0, &tx, &ty, &tw, &th);
      if ((int)tw <= maxW) return t;
    }
    return String(".");
  };

  if (issData.valid) {
    String l1 = "ISS " + String(issData.latitude, 1) + "," + String(issData.longitude, 1);
    String l2 = "Vel " + String((int)issData.velocityKmh) + " km/h  Alt " + String((int)issData.altitudeKm) + " km";
    canvas.setCursor(12, y0);
    canvas.print(fitLine(l1, W - 24));
    canvas.setCursor(12, y0 + 18);
    canvas.print(fitLine(l2, W - 24));
  } else {
    String l1 = "ISS: sin datos";
    String l2 = issData.lastError.length() > 0 ? issData.lastError : "Sin informacion";
    canvas.setCursor(12, y0);
    canvas.print(fitLine(l1, W - 24));
    canvas.setCursor(12, y0 + 18);
    canvas.print(fitLine(l2, W - 24));
  }

  String city = getDisplayLocation();
  city.trim();
  canvas.setCursor(12, y0 + 36);
  canvas.print(fitLine("Ciudad: " + city, W - 24));
  if (issOrbitFetchInProgress) {
    canvas.setCursor(12, y0 + 54);
    canvas.print("Calculando orbita...");
  } else if (!issOrbitValid && issOrbitLastError.length() > 0) {
    canvas.setCursor(12, y0 + 54);
    canvas.print(fitLine("Orbita: " + issOrbitLastError, W - 24));
  }

  pushCanvasToRLCD(false);
}

String radioCountryFullName(const String& cc) {
  if (cc == "--") return "-";
  if (cc.equalsIgnoreCase("AR")) return "Argentina";
  if (cc.equalsIgnoreCase("AU")) return "Australia";
  if (cc.equalsIgnoreCase("BO")) return "Bolivia";
  if (cc.equalsIgnoreCase("CA")) return "Canada";
  if (cc.equalsIgnoreCase("UY")) return "Uruguay";
  if (cc.equalsIgnoreCase("CL")) return "Chile";
  if (cc.equalsIgnoreCase("BR")) return "Brasil";
  if (cc.equalsIgnoreCase("CN")) return "China";
  if (cc.equalsIgnoreCase("CO")) return "Colombia";
  if (cc.equalsIgnoreCase("DE")) return "Alemania";
  if (cc.equalsIgnoreCase("US")) return "Estados Unidos";
  if (cc.equalsIgnoreCase("ES")) return "Espana";
  if (cc.equalsIgnoreCase("FR")) return "Francia";
  if (cc.equalsIgnoreCase("GB")) return "Reino Unido";
  if (cc.equalsIgnoreCase("IT")) return "Italia";
  if (cc.equalsIgnoreCase("JP")) return "Japon";
  if (cc.equalsIgnoreCase("KR")) return "Corea del Sur";
  if (cc.equalsIgnoreCase("MX")) return "Mexico";
  if (cc.equalsIgnoreCase("PE")) return "Peru";
  if (cc.equalsIgnoreCase("PY")) return "Paraguay";
  if (cc.equalsIgnoreCase("RU")) return "Rusia";
  if (cc.equalsIgnoreCase("VE")) return "Venezuela";
  return cc;
}

String webBatteryBadgeCss() {
  return String(
    "<style>"
    ".wbslot{position:static;display:flex;justify-content:flex-end;align-items:center;margin-left:auto;margin-top:1px}"
    ".wbatt{display:flex;align-items:center;gap:0;padding:3px 6px;border:1px solid #35506c;border-radius:999px;background:rgba(11,20,33,.55)}"
    ".wbpack{width:40px;height:15px;border:1px solid #9ab9db;border-radius:4px;display:flex;align-items:center;padding:1px;box-sizing:border-box;gap:1px;background:#0c1624;box-shadow:inset 0 0 0 1px rgba(255,255,255,.03)}"
    ".wbtip{width:3px;height:8px;background:#9ab9db;border-radius:2px;margin-left:-1px}"
    ".wbseg{flex:1;height:100%;background:#1a2a3c;border-radius:1px;opacity:.55}"
    ".wbseg.on{opacity:1}"
    ".wbatt.ok .wbseg.on{background:linear-gradient(180deg,#4af5a1 0%,#24bf75 100%)}"
    ".wbatt.mid .wbseg.on{background:linear-gradient(180deg,#ffd257 0%,#d9a22b 100%)}"
    ".wbatt.low .wbseg.on{background:linear-gradient(180deg,#ff7c7c 0%,#d64545 100%)}"
    ".wbbolt{display:none;font-size:15px;font-weight:700;line-height:1;margin-left:5px;color:#ffe98a;text-shadow:0 0 10px rgba(255,233,138,.9),0 0 4px rgba(255,210,87,.95);transform:translateY(-.2px)}"
    ".wbbolt.on{opacity:1;color:#ffe98a;text-shadow:0 0 10px rgba(255,233,138,.9),0 0 4px rgba(255,210,87,.95)}"
    ".wbatt.charging .wbbolt{display:block}"
    "</style>");
}

String webBatteryBadgeHtml() {
  int segs = batteryToSegments(batteryVoltage);
  if (segs < 0) segs = 0;
  if (segs > 5) segs = 5;
  String cls = (segs <= 1) ? "low" : ((segs <= 3) ? "mid" : "ok");
  String h = "<div class='wbslot'><div id='webBatt' class='wbatt " + cls + String(batteryCharging ? " charging" : "") + "' title='Bateria'><div class='wbpack'>";
  for (int i = 0; i < 5; ++i) h += "<div class='wbseg" + String(i < segs ? " on" : "") + "'></div>";
  h += "</div><div class='wbtip'></div>";
  h += "<div class='wbbolt on'>&#9889;</div>";
  h += "</div></div>";
  return h;
}

String cleanStationDisplayName(String s) {
  s.trim();
  int b = s.lastIndexOf('[');
  int e = s.lastIndexOf(']');
  if (b >= 0 && e > b) {
    s.remove(b);
    s.trim();
  }
  return s;
}

void drawRadioPage() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1);
  canvas.drawRect(1, 1, W - 2, H - 2, 1);
  canvas.setTextColor(1);
  // Top bar: time left, title centered, battery right
  char timeFull[6];
  sprintf(timeFull, "%02d:%02d", hour24, minuteVal);
  canvas.setFont(&FONT_MEDIUM);
  canvas.setCursor(12, 34);
  canvas.print(timeFull);
  printCentered(&FONT_MEDIUM, 34, "Radio");

  // Battery graphic (dashboard style) without voltage text
  int batX = 310;
  canvas.drawRect(batX, 18, 60, 20, 1);
  canvas.fillRect(batX + 60, 24, 3, 8, 1);
  int segs = batteryToSegments(batteryVoltage);
  for (int i = 0; i < segs; i++) canvas.fillRect(batX + 2 + (i * 11), 20, 10, 16, 1);

  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(14, 56);
  canvas.print("Configurar desde la Web");

  auto drawCard = [&](int y, const String& label, const String& value, int valueMaxChars = 34) {
    const int x = 10;
    const int w = W - 20;
    const int h = 34;
    canvas.drawRect(x, y, w, h, 1);
    canvas.fillRect(x + 1, y + 1, 86, h - 2, 1);
    canvas.setTextColor(0);
    canvas.setCursor(x + 6, y + 22);
    canvas.print(label);
    canvas.setTextColor(1);
    String v = value;
    if ((int)v.length() > valueMaxChars) v = v.substring(0, valueMaxChars - 1) + ".";
    canvas.setCursor(x + 94, y + 22);
    canvas.print(v);
  };

  String station = radioStationName.length() > 0 ? cleanStationDisplayName(radioStationName) : String("(sin seleccionar)");
  String status = radioStatus.length() > 0 ? radioStatus : String("-");
  String now = radioNowPlaying.length() > 0 ? radioNowPlaying : String("-");

  drawCard(68,  "Pais",    radioCountryFullName(radioCountry), 34);
  drawCard(106, "Emisora", station, 30);
  drawCard(144, "Volumen", String(radioVolumePct) + "%", 10);
  drawCard(182, "Estado",  status, 31);
  drawCard(220, "Ahora",   now, 32);

  // Footer with internal sensor readings + dashboard icons
  canvas.fillRect(8, 264, 384, 2, 1);
  drawThermometerIcon(14, 270);
  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(40, 286);
  canvas.print(temperature, 1);
  canvas.print("C");
  drawDropletIcon(118, 274);
  canvas.setCursor(144, 286);
  canvas.print((int)humidity);
  canvas.print("%");

  pushCanvasToRLCD(false);
}

void draw() {
  switch (currentPage) {
    case 0:  drawDashboardPage();      break;
    case 1:  drawKumaDashboardPage();  break;
    case 2:  drawPhotoPage();          break;
    case 3:  drawTimeZonePage();       break;
    case 4:  drawCurrentWeatherPage(); break;
    case 5:  drawHourlyPage();         break;
    case 6:  drawForecastPage();       break;
    case 7:  drawAstronomyPage();      break;
    case 8:  drawTempGraphPage();      break;
    case 9:  drawHumidityGraphPage();  break;
    case 10: drawSystemPage();         break;
    case 11: drawWifiNetworksPage();   break;
    case 12: drawIssPage();            break;
    case 13: drawRadioPage();          break;
  }
}

void drawOtaProgressScreen() {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1);
  canvas.drawRect(1, 1, W - 2, H - 2, 1);
  canvas.setTextColor(1);
  canvas.setFont(&FONT_MEDIUM);
  int16_t tx, ty; uint16_t tw, th;
  String msg = "Actualizando firmware...";
  canvas.getTextBounds(msg, 0, 0, &tx, &ty, &tw, &th);
  int x = (W - (int)tw) / 2;
  int y = 156;
  canvas.setCursor(x, y);
  canvas.print(msg);
  pushCanvasToRLCD(false);
}

void drawOtaProgressOverlay() {
  // Minimal floating dialog over current canvas content.
  const int ww = 300;
  const int wh = 92;
  const int wx = (W - ww) / 2;
  const int wy = (H - wh) / 2;
  canvas.fillRect(wx, wy, ww, wh, 0);
  canvas.drawRect(wx, wy, ww, wh, 1);
  canvas.drawRect(wx + 1, wy + 1, ww - 2, wh - 2, 1);
  canvas.setFont(&FONT_MEDIUM);
  String msg = "Actualizando firmware...";
  int16_t tx, ty; uint16_t tw, th;
  canvas.getTextBounds(msg, 0, 0, &tx, &ty, &tw, &th);
  int x = wx + (ww - (int)tw) / 2;
  int y = wy + ((wh - (int)th) / 2) - ty;
  canvas.setCursor(x, y);
  canvas.print(msg);
  pushCanvasToRLCD(false);
}

// ===== WIFI STATUS SCREENS =====
void drawWifiStatusScreen(const char* title, const String& line1, const String& line2, const String& line3 = "") {
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1);
  canvas.drawRect(1, 1, W-2, H-2, 1);

  canvas.setTextColor(1);
  printCentered(&FONT_MEDIUM, 44, title);

  canvas.setFont(&FONT_SMALL);
  canvas.setCursor(16, 96);
  canvas.print(line1);
  canvas.setCursor(16, 126);
  canvas.print(line2);
  if (line3.length() > 0) {
    canvas.setCursor(16, 156);
    canvas.print(line3);
  }

  pushCanvasToRLCD(false);
}

void showConnectedNoDataScreen() {
  String ss = WiFi.SSID();
  if (ss.length() == 0) ss = "(desconocido)";
  String ip = WiFi.localIP().toString();
  drawWifiStatusScreen(
    "WiFi Conectado",
    "SSID: " + ss,
    "IP: " + ip
  );
}

float deg2rad(float d) { return d * (3.14159265358979323846f / 180.0f); }

float haversineKm(float lat1, float lon1, float lat2, float lon2) {
  const float R = 6371.0f;
  float dLat = deg2rad(lat2 - lat1);
  float dLon = deg2rad(lon2 - lon1);
  float a = sinf(dLat * 0.5f) * sinf(dLat * 0.5f) +
            cosf(deg2rad(lat1)) * cosf(deg2rad(lat2)) *
            sinf(dLon * 0.5f) * sinf(dLon * 0.5f);
  float c = 2.0f * atanf(sqrtf(a) / sqrtf(max(0.000001f, 1.0f - a)));
  return R * c;
}

void destinationPointKm(float latDeg, float lonDeg, float bearingDeg, float distKm, float& outLatDeg, float& outLonDeg) {
  const float R = 6371.0f;
  float d = distKm / R;
  float br = deg2rad(bearingDeg);
  float lat1 = deg2rad(latDeg);
  float lon1 = deg2rad(lonDeg);
  float sinLat1 = sinf(lat1), cosLat1 = cosf(lat1);
  float sinD = sinf(d), cosD = cosf(d);
  float lat2 = asinf(sinLat1 * cosD + cosLat1 * sinD * cosf(br));
  float lon2 = lon1 + atan2f(sinf(br) * sinD * cosLat1, cosD - sinLat1 * sinf(lat2));
  outLatDeg = lat2 * (180.0f / 3.14159265358979323846f);
  outLonDeg = lon2 * (180.0f / 3.14159265358979323846f);
  while (outLonDeg > 180.0f) outLonDeg -= 360.0f;
  while (outLonDeg < -180.0f) outLonDeg += 360.0f;
}

float issFootprintRadiusKm(float altitudeKm) {
  const float R = 6371.0f;
  if (altitudeKm < 0.0f) altitudeKm = 0.0f;
  float arg = R / (R + altitudeKm);
  if (arg < -1.0f) arg = -1.0f;
  if (arg > 1.0f) arg = 1.0f;
  return R * acosf(arg);
}

bool isIssOverHomeFootprint() {
  if (!detectedLocationValid || !issData.valid) return false;
  float d = haversineKm(detectedLat, detectedLon, issData.latitude, issData.longitude);
  float r = issFootprintRadiusKm(issData.altitudeKm);
  return d <= r;
}

void maybeBatteryCriticalBeep(unsigned long now) {
  if (!batteryCriticalBeepEnabled) return;
  if (!isBatteryCriticalSoon()) return;
  if (now - lastBatteryCriticalBeepMs < 60000UL) return;
  lastBatteryCriticalBeepMs = now;
  playToneHz(900, 130);
  audioSilenceMs(40);
  playToneHz(780, 130);
}

void maybeIssPassBeep(unsigned long now) {
  if (!issFootprintBeepEnabled) return;
  if (currentPage != 12) return;
  if (!detectedLocationValid || !issData.valid) return;

  // Proximity alert model:
  // - start beeping inside 1200 km from Home
  // - 6 s cadence
  // - pitch rises as ISS gets closer, lowers as it moves away
  // - triple short beep at entry and exit
  const float kAlertRadiusKm = 1200.0f;
  const int kToneNearHz = 1800;  // very close
  const int kToneFarHz = 650;    // near edge of 1200 km
  float d = haversineKm(detectedLat, detectedLon, issData.latitude, issData.longitude);
  bool inside = (d <= kAlertRadiusKm);

  if (inside && !issWasInsideAlertRange) {
    for (int i = 0; i < 3; i++) {
      playToneHz(1400, 55);
      audioSilenceMs(40);
    }
  } else if (!inside && issWasInsideAlertRange) {
    for (int i = 0; i < 3; i++) {
      playToneHz(900, 55);
      audioSilenceMs(40);
    }
  }
  issWasInsideAlertRange = inside;

  if (!inside) return;
  if (now - lastIssPassBeepMs < 6000UL) return;
  lastIssPassBeepMs = now;

  float closeness = 1.0f - (d / kAlertRadiusKm); // 0 far .. 1 close
  if (closeness < 0.0f) closeness = 0.0f;
  if (closeness > 1.0f) closeness = 1.0f;
  int tone = kToneFarHz + (int)((kToneNearHz - kToneFarHz) * closeness);
  playToneHz(tone, 110);
}

String sanitizeThemeInput(String t) {
  t.trim();
  t.toLowerCase();
  while (t.indexOf(" ,") >= 0) t.replace(" ,", ",");
  while (t.indexOf(", ") >= 0) t.replace(", ", ",");
  while (t.indexOf(",,") >= 0) t.replace(",,", ",");
  while (t.indexOf("  ") >= 0) t.replace("  ", " ");
  t.trim();
  return t;
}

String mqttTopic(const String& leaf) {
  String r = mqttTopicRoot;
  r.trim();
  if (r.length() == 0) r = "MQTT_TOPIC_DISABLED";
  if (r.endsWith("/")) r.remove(r.length() - 1);
  return r + "/" + leaf;
}

String mqttUrlDecode(const String& in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < in.length()) {
      char h1 = in[i + 1];
      char h2 = in[i + 2];
      auto hexv = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
        if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
        return -1;
      };
      int v1 = hexv(h1), v2 = hexv(h2);
      if (v1 >= 0 && v2 >= 0) {
        out += (char)((v1 << 4) | v2);
        i += 2;
      } else {
        out += c;
      }
    } else {
      out += c;
    }
  }
  return out;
}

String mqttFormValue(const String& payload, const String& key) {
  String needle = key + "=";
  int s = payload.indexOf(needle);
  if (s < 0) return "";
  s += needle.length();
  int e = payload.indexOf('&', s);
  if (e < 0) e = payload.length();
  return mqttUrlDecode(payload.substring(s, e));
}

void mqttPublishState() {
  if (!mqttClient.connected()) return;
  int segs = (int)roundf((batteryVoltage - 3.40f) / (4.20f - 3.40f) * 5.0f);
  if (segs < 0) segs = 0;
  if (segs > 5) segs = 5;
  String rs = radioStatus;
  rs.replace("\"", "'");
  String payload =
    String("{\"page\":") + String(currentPage) +
    ",\"temp\":" + String(temperature, 1) +
    ",\"hum\":" + String(humidity, 0) +
    ",\"battV\":" + String(batteryVoltage, 2) +
    ",\"battSegs\":" + String(segs) +
    ",\"charging\":" + String(batteryCharging ? "true" : "false") +
    ",\"wifiRssi\":" + String(wifiRSSI) +
    ",\"radio\":\"" + rs + "\"" +
    ",\"fw\":\"" + currentFirmwareName + "\"}";
  mqttClient.publish(mqttTopic("state/device").c_str(), payload.c_str(), true);
}

void mqttPublishImageState() {
  if (!mqttClient.connected()) return;
  String theme = photoTheme; theme.replace("\\", "\\\\"); theme.replace("\"", "\\\"");
  String status = photoUploadStatus; status.replace("\\", "\\\\"); status.replace("\"", "\\\"");
  String payload =
    String("{\"photoTheme\":\"") + theme +
    "\",\"photoRefreshMinutes\":" + String(photoRefreshMinutes) +
    ",\"photoUseUploaded\":" + String(photoUseUploaded ? "true" : "false") +
    ",\"photoUploadStatus\":\"" + status + "\"}";
  mqttClient.publish(mqttTopic("state/image").c_str(), payload.c_str(), true);
}

void mqttPublishRadioState() {
  if (!mqttClient.connected()) return;
  String st = radioStatus; st.replace("\\", "\\\\"); st.replace("\"", "\\\"");
  String nm = radioStationName; nm.replace("\\", "\\\\"); nm.replace("\"", "\\\"");
  String url = radioStationUrl; url.replace("\\", "\\\\"); url.replace("\"", "\\\"");
  String codec = radioStationCodec; codec.replace("\\", "\\\\"); codec.replace("\"", "\\\"");
  String presets = "[";
  for (int i = 0; i < RADIO_PRESET_COUNT; i++) {
    String pn = radioPresetName[i]; pn.replace("\\", "\\\\"); pn.replace("\"", "\\\"");
    if (i) presets += ",";
    presets += String("{\"idx\":") + String(i + 1) +
               ",\"name\":\"" + pn + "\"}";
  }
  presets += "]";
  String payload =
    String("{\"playing\":") + (radioPlaying ? "true" : "false") +
    ",\"volume\":" + String(radioVolumePct) +
    ",\"status\":\"" + st + "\"" +
    ",\"country\":\"" + radioCountry + "\"" +
    ",\"filterCodec\":\"" + radioFilterCodec + "\"" +
    ",\"filterBitrateMax\":" + String(radioFilterBitrateMax) +
    ",\"stationName\":\"" + nm + "\"" +
    ",\"stationUrl\":\"" + url + "\"" +
    ",\"stationCodec\":\"" + codec + "\"" +
    ",\"presets\":" + presets + "}";
  mqttClient.publish(mqttTopic("state/radio").c_str(), payload.c_str(), true);
}

void mqttPublishSystemState() {
  if (!mqttClient.connected()) return;
  String mh = mqttHost; mh.replace("\\", "\\\\"); mh.replace("\"", "\\\"");
  String mu = mqttUser; mu.replace("\\", "\\\\"); mu.replace("\"", "\\\"");
  String rt = mqttTopicRoot; rt.replace("\\", "\\\\"); rt.replace("\"", "\\\"");
  String payload =
    String("{\"mqttEnabled\":") + (mqttEnabled ? "true" : "false") +
    ",\"mqttConnected\":" + (mqttConnected ? String("true") : String("false")) +
    ",\"mqttHost\":\"" + mh + "\"" +
    ",\"mqttPort\":" + String(mqttPort) +
    ",\"mqttUser\":\"" + mu + "\"" +
    ",\"mqttTopicRoot\":\"" + rt + "\"" +
    ",\"audioVolumePct\":" + String(audioVolumePct) +
    ",\"beepBattery\":" + String(batteryCriticalBeepEnabled ? "true" : "false") +
    ",\"beepIss\":" + String(issFootprintBeepEnabled ? "true" : "false") +
    ",\"displayInvert\":" + String(displayInvertMode ? "true" : "false") +
    ",\"batterySave\":" + String(batterySaveMode ? "true" : "false") +
    "}";
  mqttClient.publish(mqttTopic("state/system").c_str(), payload.c_str(), true);
}

void mqttMessageCallback(char* topic, byte* payload, unsigned int length) {
  String t = String(topic);
  String p = "";
  for (unsigned int i = 0; i < length; i++) p += (char)payload[i];
  p.trim();
  String base = mqttTopic("cmd/");
  if (!t.startsWith(base)) return;
  String cmd = t.substring(base.length());

  if (cmd == "page") {
    int np = p.toInt();
    if (np >= 0 && np < totalPages) currentPage = np;
  } else if (cmd == "reboot") {
    if (p == "1" || p == "true" || p == "now") ESP.restart();
  } else if (cmd == "audio/vol") {
    int v = p.toInt();
    v = ((v + 2) / 5) * 5;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    bool changed = (audioVolumePct != v);
    audioVolumePct = v;
    applyAudioVolumeToCodec();
    if (changed) playVolumeConfirmBeep();
    saveRuntimeConfig();
  } else if (cmd == "state/get") {
    mqttPublishState();
    mqttPublishImageState();
    mqttPublishRadioState();
    mqttPublishSystemState();
  } else if (cmd == "system/save") {
    bool mqttChanged = false;
    bool hasMqttEn = (p.indexOf("mqtt_en=") >= 0);
    if (hasMqttEn) {
      String en = mqttFormValue(p, "mqtt_en");
      bool newMqttEnabled = (en == "1" || en == "true" || en == "on");
      if (newMqttEnabled != mqttEnabled) {
        mqttEnabled = newMqttEnabled;
        mqttChanged = true;
      }
    }
    String h = mqttFormValue(p, "mqtt_h");
    if (h.length() > 0 && h != mqttHost) {
      mqttHost = h;
      mqttChanged = true;
    }
    mqttHost.trim();
    if (mqttHost.length() == 0) mqttHost = "MQTT_HOST_DISABLED";
    int pp = mqttFormValue(p, "mqtt_p").toInt();
    if (pp > 0 && pp <= 65535 && pp != mqttPort) {
      mqttPort = pp;
      mqttChanged = true;
    }
    String u = mqttFormValue(p, "mqtt_u");
    String pw = mqttFormValue(p, "mqtt_pw");
    String rt = mqttFormValue(p, "mqtt_rt");
    if (u.length() > 0 && u != mqttUser) {
      mqttUser = u;
      mqttChanged = true;
    }
    if (pw.length() > 0 && pw != mqttPass) {
      mqttPass = pw;
      mqttChanged = true;
    }
    if (rt.length() > 0 && rt != mqttTopicRoot) {
      mqttTopicRoot = rt;
      mqttChanged = true;
    }
    mqttTopicRoot.trim();
    mqttTopicRoot.toLowerCase();
    if (mqttTopicRoot.length() == 0) mqttTopicRoot = "MQTT_TOPIC_DISABLED";

    // Accept board-side system updates in the same command:
    // volume + beep toggles (checkboxes can come with different names).
    String vStr = mqttFormValue(p, "vol");
    if (vStr.length() == 0) vStr = mqttFormValue(p, "audio_vol");
    if (vStr.length() == 0) vStr = mqttFormValue(p, "audioVolumePct");
    if (vStr.length() > 0) {
      int v = vStr.toInt();
      v = ((v + 2) / 5) * 5;
      if (v < 0) v = 0;
      if (v > 100) v = 100;
      bool changed = (audioVolumePct != v);
      audioVolumePct = v;
      applyAudioVolumeToCodec();
      if (changed) playVolumeConfirmBeep();
    }

    bool hasBeepBat = (p.indexOf("beep_bat=") >= 0) || (p.indexOf("beepBattery=") >= 0);
    if (hasBeepBat) {
      String bb = mqttFormValue(p, "beep_bat");
      if (bb.length() == 0) bb = mqttFormValue(p, "beepBattery");
      bb.toLowerCase();
      batteryCriticalBeepEnabled = (bb == "1" || bb == "true" || bb == "on");
    }
    bool hasBeepIss = (p.indexOf("beep_iss=") >= 0) || (p.indexOf("beepIss=") >= 0);
    if (hasBeepIss) {
      String bi = mqttFormValue(p, "beep_iss");
      if (bi.length() == 0) bi = mqttFormValue(p, "beepIss");
      bi.toLowerCase();
      issFootprintBeepEnabled = (bi == "1" || bi == "true" || bi == "on");
    }
    bool hasDispInv = (p.indexOf("disp_inv=") >= 0) || (p.indexOf("displayInvert=") >= 0);
    if (hasDispInv) {
      String di = mqttFormValue(p, "disp_inv");
      if (di.length() == 0) di = mqttFormValue(p, "displayInvert");
      di.toLowerCase();
      displayInvertMode = (di == "1" || di == "true" || di == "on");
    }
    bool hasBatSave = (p.indexOf("bat_save=") >= 0) || (p.indexOf("batterySave=") >= 0);
    if (hasBatSave) {
      String bs = mqttFormValue(p, "bat_save");
      if (bs.length() == 0) bs = mqttFormValue(p, "batterySave");
      bs.toLowerCase();
      batterySaveMode = (bs == "1" || bs == "true" || bs == "on");
    }

    applyPowerProfile();
    saveRuntimeConfig();
    if (mqttChanged) {
      mqttClient.disconnect();
      mqttConnected = false;
      mqttStatusText = "Reconfigurado";
      mqttApplyConfig();
    } else {
      mqttPublishSystemState();
    }
  } else if (cmd == "image/save") {
    String theme = sanitizeThemeInput(mqttFormValue(p, "theme"));
    String custom = sanitizeThemeInput(mqttFormValue(p, "theme_custom"));
    int refreshMin = mqttFormValue(p, "img_refresh_min").toInt();
    if (refreshMin < 1) refreshMin = 1;
    if (refreshMin > 10) refreshMin = 10;
    if (custom.length() > 0) theme = custom;
    if (theme.length() == 0) theme = photoTheme;
    photoTheme = theme;
    photoRefreshMinutes = refreshMin;
    photoUseUploaded = false;
    photoUploadStatus = "Modo online activo";
    saveRuntimeConfig();
    photoRefreshRequested = true;
    mqttPublishImageState();
  } else if (cmd == "image/uploaded/enable") {
    String err;
    if (loadUploadedPhotoFromFs(err)) {
      photoUseUploaded = true;
      photoRefreshRequested = false;
      photoUploadStatus = "Imagen fija activa (400x272)";
      saveRuntimeConfig();
    } else {
      photoUploadStatus = "Error al cargar: " + err;
    }
    mqttPublishImageState();
  } else if (cmd == "image/uploaded/disable") {
    photoUseUploaded = false;
    photoUploadStatus = "Modo online activo";
    saveRuntimeConfig();
    photoRefreshRequested = true;
    mqttPublishImageState();
  } else if (cmd == "radio/save") {
    if (radioPlaying || radioStartRequested) {
      radioUserStopped = true;
      radioStartRequested = false;
      stopRadioPlayback();
    }
    String cc = mqttFormValue(p, "country_code");
    String fCodec = mqttFormValue(p, "filter_codec");
    String fBr = mqttFormValue(p, "filter_bitrate_max");
    String stName = mqttFormValue(p, "station_name");
    String stUrl = mqttFormValue(p, "station_url");
    String stCodec = mqttFormValue(p, "station_codec");
    cc.trim(); cc.toUpperCase();
    fCodec.trim(); fCodec.toLowerCase();
    int fBrMax = fBr.toInt();
    if (cc != "--" && cc.length() != 2) cc = "AR";
    if (fCodec != "mp3" && fCodec != "aac" && fCodec != "aacp" && fCodec != "any") fCodec = "mp3";
    if (fBrMax != 32 && fBrMax != 48 && fBrMax != 64 && fBrMax != 96 && fBrMax != 128) fBrMax = 96;
    stName.trim(); stUrl.trim(); stCodec.trim(); stCodec.toLowerCase();
    if (stName.length() == 0) stName = kFixedStationName;
    if (stUrl.length() == 0) stUrl = kFixedStationUrl;
    if (stCodec.length() == 0) {
      String lurl = stUrl; lurl.toLowerCase();
      stCodec = (lurl.indexOf(".aac") >= 0 || lurl.indexOf(".m4a") >= 0) ? "aac" : "mp3";
    }
    int rvol = mqttFormValue(p, "radio_vol").toInt();
    rvol = ((rvol + 2) / 5) * 5;
    if (rvol < 0) rvol = 0;
    if (rvol > 100) rvol = 100;
    radioCountry = cc;
    radioFilterCodec = fCodec;
    radioFilterBitrateMax = fBrMax;
    radioStationName = stName;
    radioStationUrl = stUrl;
    radioStationCodec = stCodec;
    radioVolumePct = rvol;
    radioMuted = false;
    currentPage = 13; // Keep radio engine enabled (safety logic stops stream outside page 14)
    saveRuntimeConfig();
    applyRadioVolume();
    radioUserStopped = false;
    radioStartRequested = true;
    mqttPublishRadioState();
  } else if (cmd == "radio/stop") {
    radioUserStopped = true;
    radioStartRequested = false;
    stopRadioPlayback();
    mqttPublishRadioState();
  } else if (cmd == "radio/live") {
    int rvol = mqttFormValue(p, "vol").toInt();
    rvol = ((rvol + 2) / 5) * 5;
    if (rvol < 0) rvol = 0;
    if (rvol > 100) rvol = 100;
    radioVolumePct = rvol;
    applyRadioVolume();
    radioMuted = false;
    mqttPublishRadioState();
  } else if (cmd == "radio/preset/save") {
    int idx = mqttFormValue(p, "preset_slot").toInt();
    if (idx < 1 || idx > RADIO_PRESET_COUNT) idx = 1;
    idx -= 1;
    String name = mqttFormValue(p, "preset_name");
    String url = mqttFormValue(p, "station_url");
    String codec = mqttFormValue(p, "station_codec");
    String cc = mqttFormValue(p, "country_code");
    String stName = mqttFormValue(p, "station_name");
    name.trim(); url.trim(); codec.trim(); codec.toLowerCase(); cc.trim(); cc.toUpperCase(); stName.trim();
    if (codec.length() == 0) codec = "mp3";
    if (cc != "--" && cc.length() != 2) cc = "AR";
    bool isManual = (url == stName);
    if (isManual) cc = "--";
    if (url.length() > 0) {
      if (name.length() == 0) name = stName;
      name.trim();
      if (name.length() == 0) name = "Preset " + String(idx + 1);
      radioPresetName[idx] = name;
      radioPresetUrl[idx] = url;
      radioPresetCodec[idx] = codec;
      radioPresetCountry[idx] = cc;
      saveRuntimeConfig();
    }
    mqttPublishRadioState();
  } else if (cmd == "radio/preset/load") {
    int idx = mqttFormValue(p, "idx").toInt();
    if (idx < 1 || idx > RADIO_PRESET_COUNT) idx = 1;
    idx -= 1;
    if (radioPresetUrl[idx].length() > 0) {
      radioStationName = radioPresetName[idx];
      radioStationUrl = radioPresetUrl[idx];
      radioStationCodec = radioPresetCodec[idx];
      radioCountry = radioPresetCountry[idx];
      radioCountry.trim();
      radioCountry.toUpperCase();
      if (radioCountry != "--" && radioCountry.length() != 2) radioCountry = "AR";
      if (radioStationCodec.length() == 0) radioStationCodec = "mp3";
      currentPage = 13;
      radioUserStopped = false;
      radioStartRequested = true;
      saveRuntimeConfig();
    }
    mqttPublishRadioState();
  } else if (cmd == "radio/preset/delete") {
    int idx = mqttFormValue(p, "idx").toInt();
    if (idx < 1 || idx > RADIO_PRESET_COUNT) idx = 1;
    idx -= 1;
    radioPresetName[idx] = "";
    radioPresetUrl[idx] = "";
    radioPresetCodec[idx] = "mp3";
    radioPresetCountry[idx] = "AR";
    saveRuntimeConfig();
    mqttPublishRadioState();
  }
}

void mqttApplyConfig() {
  mqttClient.setServer(mqttHost.c_str(), mqttPort);
  mqttClient.setCallback(mqttMessageCallback);
  mqttClient.setBufferSize(1024);
}

void mqttEnsureConnected(unsigned long now) {
  if (!mqttEnabled || !wifiConnected) {
    if (mqttClient.connected()) mqttClient.disconnect();
    mqttConnected = false;
    mqttStatusText = mqttEnabled ? "Sin WiFi" : "Deshabilitado";
    return;
  }
  if (mqttClient.connected()) {
    mqttConnected = true;
    mqttStatusText = "Conectado";
    return;
  }
  if (now - mqttLastConnectAttemptMs < 5000UL) return;
  mqttLastConnectAttemptMs = now;
  mqttStatusText = "Conectando...";
  String clientId = "RLCD42-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = false;
  String willTopic = mqttTopic("event/status");
  if (mqttUser.length() > 0) {
    ok = mqttClient.connect(clientId.c_str(), mqttUser.c_str(), mqttPass.c_str(),
                            willTopic.c_str(), 0, true, "offline");
  } else {
    ok = mqttClient.connect(clientId.c_str(), nullptr, nullptr,
                            willTopic.c_str(), 0, true, "offline");
  }
  if (ok) {
    mqttConnected = true;
    mqttStatusText = "Conectado";
    mqttClient.subscribe(mqttTopic("cmd/page").c_str());
    mqttClient.subscribe(mqttTopic("cmd/reboot").c_str());
    mqttClient.subscribe(mqttTopic("cmd/audio/vol").c_str());
    mqttClient.subscribe(mqttTopic("cmd/state/get").c_str());
    mqttClient.subscribe(mqttTopic("cmd/system/save").c_str());
    mqttClient.subscribe(mqttTopic("cmd/image/save").c_str());
    mqttClient.subscribe(mqttTopic("cmd/image/uploaded/enable").c_str());
    mqttClient.subscribe(mqttTopic("cmd/image/uploaded/disable").c_str());
    mqttClient.subscribe(mqttTopic("cmd/radio/save").c_str());
    mqttClient.subscribe(mqttTopic("cmd/radio/stop").c_str());
    mqttClient.subscribe(mqttTopic("cmd/radio/live").c_str());
    mqttClient.subscribe(mqttTopic("cmd/radio/preset/save").c_str());
    mqttClient.subscribe(mqttTopic("cmd/radio/preset/load").c_str());
    mqttClient.subscribe(mqttTopic("cmd/radio/preset/delete").c_str());
    mqttClient.publish(mqttTopic("event/status").c_str(), "online", true);
    mqttPublishState();
    mqttPublishImageState();
    mqttPublishRadioState();
    mqttPublishSystemState();
    mqttLastStatePublishMs = now;
  } else {
    mqttConnected = false;
    mqttStatusText = "Error " + String(mqttClient.state());
  }
}

bool webAuthOk() {
  if (webAdminPassword.length() < 8) updateWebAdminPasswordFromWiFi();
  if (!configServer.authenticate("admin", webAdminPassword.c_str())) {
    configServer.requestAuthentication(BASIC_AUTH, webAuthRealm.c_str(), "Autenticacion requerida");
    return false;
  }
  return true;
}

String htmlEscape(const String& s) {
  String o = s;
  o.replace("&", "&amp;");
  o.replace("<", "&lt;");
  o.replace(">", "&gt;");
  o.replace("\"", "&quot;");
  return o;
}

void handleConfigRoot() {
  if (!webAuthOk()) return;
  String ip = wifiConnected ? WiFi.localIP().toString() : String("sin red");
  String ss = wifiConnected ? WiFi.SSID() : String("--");
  String city = getDisplayLocation();
  float mins = estimateBatteryMinutesLeft(batteryVoltage);
  static const char* kPageNames[totalPages] = {
    "Principal",
    "Dashboard KUMA",
    "Imagen",
    "Zonas horarias",
    "Clima actual",
    "Pronostico por hora",
    "Pronostico diario",
    "Astronomia",
    "Grafico temperatura",
    "Grafico humedad",
    "Sistema",
    "Redes WiFi",
    "ISS",
    "Radio"
  };
  String pageButtons = "";
  for (int i = 0; i < totalPages; ++i) {
    String active = (i == currentPage) ? " active" : "";
    if (i == 2) {
      pageButtons += "<a class='pbtn" + active + "' href='/image' data-page='" + String(i) + "'>" + String(i + 1) + ". " + htmlEscape(kPageNames[i]) + "</a>";
    } else if (i == 10) {
      pageButtons += "<a class='pbtn" + active + "' href='/system' data-page='" + String(i) + "'>" + String(i + 1) + ". " + htmlEscape(kPageNames[i]) + "</a>";
    } else if (i == 13) {
      pageButtons += "<a class='pbtn" + active + "' href='/radio' data-page='" + String(i) + "'>" + String(i + 1) + ". " + htmlEscape(kPageNames[i]) + "</a>";
    } else {
      pageButtons += "<a class='pbtn" + active + "' href='#' data-page='" + String(i) + "'>" + String(i + 1) + ". " + htmlEscape(kPageNames[i]) + "</a>";
    }
  }

  String html =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>RLCD Config</title>"
    "<style>"
    ":root{--bg:#0b1119;--panel:#101a27;--accent:#39d98a;--txt:#e7eef7;--muted:#93a4b8;}"
    "body{margin:0;background:radial-gradient(900px 500px at 80% -10%,#1a2e47 0%,var(--bg) 55%);color:var(--txt);font-family:Consolas,Monaco,monospace;}"
    ".w{max-width:860px;margin:24px auto;padding:0 14px;}"
    ".card{background:linear-gradient(180deg,#122034 0%,var(--panel) 100%);border:1px solid #2a3b50;border-radius:12px;padding:16px;box-shadow:0 10px 30px rgba(0,0,0,.35);}"
    "h1{font-size:22px;margin:0 0 8px 0;color:var(--accent)}"
    ".cardhead{display:flex;justify-content:space-between;align-items:flex-start;gap:10px;margin-bottom:8px}"
    ".cardhead h1{margin:0}"
    ".grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}"
    ".formfull{grid-column:1/-1;width:100%}"
    ".k{color:var(--muted)}"
    "label{display:block;margin:10px 0 6px}"
    "input,select{width:100%;background:#0b1421;color:var(--txt);border:1px solid #2c425f;border-radius:8px;padding:10px;box-sizing:border-box}"
    "input[type=range]{width:100%;max-width:100%;min-width:0;box-sizing:border-box;padding:0;margin:0;border:0;display:block}"
    ".row{display:flex;gap:12px;align-items:center;flex-wrap:wrap}"
    ".chk{display:flex;gap:8px;align-items:center}"
    ".beepWrap{margin-top:10px}"
    ".beepTitle{font-weight:700;color:var(--muted);margin:0 0 6px 0}"
    ".beepRow{display:grid;grid-template-columns:repeat(2,minmax(190px,1fr));gap:10px;align-items:center}"
    ".sw{display:flex;align-items:center;justify-content:space-between;gap:10px;background:#0f1b2a;border:1px solid #2c425f;border-radius:10px;padding:8px 10px}"
    ".swlbl{font-size:14px;color:var(--txt)}"
    ".switch{position:relative;display:inline-block;width:46px;height:24px;flex:0 0 auto}"
    ".switch input{opacity:0;width:0;height:0}"
    ".slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:#2a3f57;transition:.2s;border-radius:24px;border:1px solid #3a5676}"
    ".slider:before{position:absolute;content:'';height:18px;width:18px;left:2px;top:2px;background:#d6e6f7;transition:.2s;border-radius:50%}"
    ".switch input:checked + .slider{background:#39d98a;border-color:#39d98a}"
    ".switch input:checked + .slider:before{transform:translateX(22px);background:#062013}"
    ".actions{margin-top:14px;width:100%;display:flex;justify-content:space-between;align-items:center;gap:10px;flex-wrap:nowrap}"
    ".btn{background:var(--accent);color:#062013;border:0;padding:10px 14px;border-radius:8px;font-weight:700;cursor:pointer;text-decoration:none;display:inline-block}"
    ".subcard{margin-top:12px;background:linear-gradient(180deg,#122034 0%,var(--panel) 100%);border:1px solid #2a3b50;border-radius:12px;padding:12px}"
    ".subttl{font-size:15px;font-weight:700;color:var(--accent);margin:0 0 8px 0}"
    ".pagegrid{margin-top:12px;display:grid;grid-template-columns:1fr 1fr;gap:8px}"
    ".pbtn{background:#182637;color:#d8e6f7;border:1px solid #36506c;padding:9px 10px;border-radius:8px;text-decoration:none;display:block;text-align:center;font-weight:700}"
    ".pbtn.active{background:#274161;border-color:#4f7aa6;color:#ffffff}"
    ".small{font-size:12px;color:var(--muted)}"
    ".prog{height:10px;background:#213347;border:1px solid #35506c;border-radius:999px;overflow:hidden;max-width:420px}"
    ".prog>span{display:block;height:100%;width:0%;background:linear-gradient(90deg,#1f91ff,#39d98a);transition:width .2s ease}"
    ".state{font-size:16px;color:var(--accent) !important;font-weight:700}"
    ".ota{margin-top:14px;padding:10px;border:1px solid #2a3b50;border-radius:10px;background:#0d1624}"
    ".otaTitle{font-weight:700;color:var(--accent);margin:0 0 8px 0}"
    ".prog{height:8px;background:#1a2a3c;border-radius:99px;overflow:hidden}"
    ".bar{height:100%;width:0;background:#39d98a}"
    "@media(max-width:780px){.grid{grid-template-columns:1fr}.beepRow{grid-template-columns:1fr}.actions{display:flex;justify-content:space-between;align-items:center;flex-wrap:nowrap}.actions .btn{width:auto;text-align:center}}"
    "</style>" + webBatteryBadgeCss() + "</head><body><div class='w'><div class='card'>"
    "<div class='cardhead'><h1>RLCD Control Panel</h1>" + webBatteryBadgeHtml() + "</div>"
    "<div class='grid'>"
    "<div><div><span class='k'>SSID:</span> " + htmlEscape(ss) + "</div>"
    "<div><span class='k'>IP:</span> " + htmlEscape(ip) + "</div>"
    "<div><span class='k'>Ciudad:</span> " + htmlEscape(city) + "</div></div>"
    "<div><div>"
    "<div><span class='k'>Autonomia est.:</span> " + String((int)mins) + " min</div>"
    "<div><span class='k'>Voltaje batt:</span> " + String(batteryVoltage, 2) + "V</div>"
    "<div><span class='k'>Audio:</span> " + String(audioVolumePct) + "%</div></div>"
    "</div>"
    "<form class='formfull' method='POST' action='/save'>"
    "<label>Volumen audio: <span id='vtxt'>" + String(audioVolumePct) + "%</span></label>"
    "<input type='range' min='0' max='100' step='5' name='vol' id='vol' value='" + String(audioVolumePct) + "'>"
    "<div class='beepWrap'>"
    ""
    "<div class='beepRow'>"
    "<div class='sw'><span class='swlbl'>Beep Bateria</span><label class='switch'><input id='beep_bat' type='checkbox' name='beep_bat' " + String(batteryCriticalBeepEnabled ? "checked" : "") + "><span class='slider'></span></label></div>"
    "<div class='sw'><span class='swlbl'>Beep ISS</span><label class='switch'><input id='beep_iss' type='checkbox' name='beep_iss' " + String(issFootprintBeepEnabled ? "checked" : "") + "><span class='slider'></span></label></div>"
    "<div class='sw'><span class='swlbl'>Modo oscuro</span><label class='switch'><input id='disp_inv' type='checkbox' name='disp_inv' " + String(displayInvertMode ? "checked" : "") + "><span class='slider'></span></label></div>"
    "<div class='sw'><span class='swlbl'>Ahorro bateria</span><label class='switch'><input id='bat_save' type='checkbox' name='bat_save' " + String(batterySaveMode ? "checked" : "") + "><span class='slider'></span></label></div>"
    "</div>"
    "</div>"
    "<div class='actions'>"
    "<button class='btn' type='button' id='rebootBtn'>Resetear dispositivo</button>"
    "</div>"
    "</form>"
    "<p class='small'>Login: admin / clave de tu WiFi actual</p>"
    "</div>"
    "<div class='subcard'>"
    "<p class='subttl'>Pantallas</p>"
    "<div class='pagegrid'>" + pageButtons + "</div>"
    "</div>"
    "</div></div>"
    "<script>"
    "const v=document.getElementById('vol');const t=document.getElementById('vtxt');"
    "const swBat=document.getElementById('beep_bat');"
    "const swIss=document.getElementById('beep_iss');"
    "const swInv=document.getElementById('disp_inv');"
    "const swSave=document.getElementById('bat_save');"
    "const rebootBtn=document.getElementById('rebootBtn');"
    "let bt=0,bp='';"
    "let saveT=0;"
    "let localSwitchTs=0;"
    "function postSaveNow(){"
    "const p=new URLSearchParams();"
    "if(v)p.set('vol',v.value||'0');"
    "if(swBat&&swBat.checked)p.set('beep_bat','on');"
    "if(swIss&&swIss.checked)p.set('beep_iss','on');"
    "if(swInv&&swInv.checked)p.set('disp_inv','on');"
    "if(swSave&&swSave.checked)p.set('bat_save','on');"
    "fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()}).catch(()=>{});"
    "}"
    "function markLocalSwitch(){localSwitchTs=Date.now();}"
    "function queueSave(){clearTimeout(saveT);saveT=setTimeout(postSaveNow,40);}"
    "function beepPreview(){if(!v)return;const nv=v.value;if(nv===bp)return;bp=nv;"
    "clearTimeout(bt);bt=setTimeout(()=>{fetch('/beep?v='+encodeURIComponent(nv)).catch(()=>{});},140);}"
    "document.querySelectorAll('.pbtn').forEach(function(b){"
    "b.addEventListener('click',function(e){"
    "e.preventDefault();"
    "const p=this.getAttribute('data-page');"
    "if(p==='2'){window.location.href='/image';return;}"
    "if(p==='10'){window.location.href='/system';return;}"
    "if(p==='13'){window.location.href='/radio';return;}"
    "fetch('/setpage?p='+encodeURIComponent(p)).then(()=>location.reload()).catch(()=>{});"
    "});"
    "});"
    "function syncBatteryBadge(d){"
    "const b=document.getElementById('webBatt');if(!b||!d)return;"
    "const s=parseInt(d.batterySegs||0,10);const segs=b.querySelectorAll('.wbseg');"
    "for(let i=0;i<segs.length;i++){if(i<s)segs[i].classList.add('on');else segs[i].classList.remove('on');}"
    "b.classList.remove('low','mid','ok');if(s<=1)b.classList.add('low');else if(s<=3)b.classList.add('mid');else b.classList.add('ok');"
    "if(d.batteryCharging)b.classList.add('charging');else b.classList.remove('charging');"
    "}"
    "function syncCurrentPage(){fetch('/panelstate').then(r=>r.json()).then(d=>{"
    "if(!d||typeof d.currentPage==='undefined')return;"
    "document.querySelectorAll('.pbtn').forEach(function(b){b.classList.remove('active');});"
    "const a=document.querySelector('.pbtn[data-page=\"'+d.currentPage+'\"]');"
    "if(a)a.classList.add('active');"
    "syncBatteryBadge(d);"
    "const maySyncSwitch=(Date.now()-localSwitchTs)>1200;"
    "if(maySyncSwitch){"
    "const bs=document.getElementById('bat_save');if(bs&&typeof d.batterySave!=='undefined')bs.checked=!!d.batterySave;"
    "const di=document.getElementById('disp_inv');if(di&&typeof d.displayInvert!=='undefined')di.checked=!!d.displayInvert;"
    "}"
    "}).catch(()=>{});}"
    "setInterval(syncCurrentPage,1000);"
    "syncCurrentPage();"
    "if(v&&t){v.addEventListener('input',()=>{t.textContent=v.value+'%';beepPreview();});v.addEventListener('change',queueSave);}"
    "if(swBat)swBat.addEventListener('change',()=>{markLocalSwitch();queueSave();});"
    "if(swIss)swIss.addEventListener('change',()=>{markLocalSwitch();queueSave();});"
    "if(swInv)swInv.addEventListener('change',()=>{markLocalSwitch();queueSave();});"
    "if(swSave)swSave.addEventListener('change',()=>{markLocalSwitch();queueSave();});"
    "if(rebootBtn){rebootBtn.addEventListener('click',()=>{if(!confirm('Desea resetear el dispositivo ahora?'))return;fetch('/reboot').then(()=>{alert('Reiniciando...');}).catch(()=>{});});}"
    "</script>"
    "</body></html>";
  configServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  configServer.sendHeader("Pragma", "no-cache");
  configServer.sendHeader("Expires", "0");
  configServer.send(200, "text/html; charset=utf-8", html);
}

void handleConfigSystemPage() {
  if (!webAuthOk()) return;
  currentPage = 10;
  String ipStr   = wifiConnected ? WiFi.localIP().toString() : "Sin conectar";
  String wifiVal = wifiConnected ? String(wifiRSSI) + " dBm" : "Sin red";
  unsigned long upSec = millis() / 1000;
  char uptimeStr[12];
  sprintf(uptimeStr, "%02d:%02d:%02d", (int)(upSec/3600), (int)((upSec%3600)/60), (int)(upSec%60));
  String ntpStr = "Nunca";
  if (ntpLastSync > 0) {
    unsigned long syncAgo = (millis() - ntpLastSync) / 60000;
    ntpStr = syncAgo < 60 ? String(syncAgo) + " min" : String(syncAgo/60) + " h";
  }
  String ssidStr = (wifiConnected && WiFi.SSID().length() > 0) ? WiFi.SSID() : "--";
  String fwStr = currentFirmwareName;
  if (fwStr.length() == 0) fwStr = "N/D";
  int wifiBars = 0;
  if (wifiConnected) {
    if (wifiRSSI >= -60) wifiBars = 4;
    else if (wifiRSSI >= -70) wifiBars = 3;
    else if (wifiRSSI >= -80) wifiBars = 2;
    else if (wifiRSSI >= -90) wifiBars = 1;
  }

  String html =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>RLCD Sistema</title>"
    "<style>"
    ":root{--bg:#0b1119;--panel:#101a27;--accent:#39d98a;--txt:#e7eef7;--muted:#93a4b8;}"
    "body{margin:0;background:radial-gradient(900px 500px at 80% -10%,#1a2e47 0%,var(--bg) 55%);color:var(--txt);font-family:Consolas,Monaco,monospace;}"
    ".w{max-width:920px;margin:24px auto;padding:0 14px;}"
    ".card{background:linear-gradient(180deg,#122034 0%,var(--panel) 100%);border:1px solid #2a3b50;border-radius:12px;padding:16px;box-shadow:0 10px 30px rgba(0,0,0,.35);}"
    "h1{font-size:22px;margin:0 0 12px 0;color:var(--accent)}"
    ".cardhead{display:flex;justify-content:space-between;align-items:flex-start;gap:10px;margin-bottom:12px}"
    ".cardhead h1{margin:0}"
    ".grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}"
    ".k{color:var(--muted)}"
    ".sig{display:inline-flex;align-items:flex-end;gap:2px;margin-left:8px;vertical-align:middle;position:relative;top:-5px}"
    ".sb{width:4px;background:#35506c;border-radius:1px;display:inline-block}"
    ".sb.b1{height:6px}.sb.b2{height:9px}.sb.b3{height:12px}.sb.b4{height:15px}"
    ".sb.on{background:#39d98a}"
    ".btn{background:var(--accent);color:#062013;border:0;padding:10px 14px;border-radius:8px;font-weight:700;cursor:pointer;text-decoration:none;display:inline-block}"
    ".btn2{background:#203246;color:#d8e6f7;border:1px solid #39506a;padding:10px 14px;border-radius:8px;font-weight:700;text-decoration:none;display:inline-block}"
    ".row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}"
    ".subcard{margin-top:12px;background:linear-gradient(180deg,#122034 0%,var(--panel) 100%);border:1px solid #2a3b50;border-radius:12px;padding:12px}"
    ".subttl{font-size:15px;font-weight:700;color:var(--accent);margin:0 0 8px 0}"
    "label{display:block;margin:10px 0 6px}"
    "input{width:100%;background:#0b1421;color:var(--txt);border:1px solid #2c425f;border-radius:8px;padding:10px;box-sizing:border-box}"
    ".small{font-size:12px;color:var(--muted)}"
    ".prog{height:8px;background:#1a2a3c;border-radius:99px;overflow:hidden}"
    ".bar{height:100%;width:0;background:#39d98a}"
    "@media(max-width:780px){.grid{grid-template-columns:1fr}}"
    "</style>" + webBatteryBadgeCss() + "</head><body>"
    "<div class='w'><div class='card'>"
    "<div class='cardhead'><h1>Sistema</h1>" + webBatteryBadgeHtml() + "</div>"
    "<div class='grid'>"
    "<div>"
    "<div><span class='k'>Red:</span> " + htmlEscape(ssidStr) +
      " <span class='sig'>"
      "<span class='sb b1" + String(wifiBars >= 1 ? " on" : "") + "'></span>"
      "<span class='sb b2" + String(wifiBars >= 2 ? " on" : "") + "'></span>"
      "<span class='sb b3" + String(wifiBars >= 3 ? " on" : "") + "'></span>"
      "<span class='sb b4" + String(wifiBars >= 4 ? " on" : "") + "'></span>"
      "</span></div>"
    "<div><span class='k'>IP:</span> " + htmlEscape(ipStr) + "</div>"
    "<div><span class='k'>Clima:</span> " + String(weatherData.valid ? "Reciente" : "Sin datos") + "</div>"
    "<div><span class='k'>Encendido:</span> " + String(uptimeStr) + "</div>"
    "</div>"
    "<div>"
    "<div><span class='k'>Sync NTP:</span> " + htmlEscape(ntpStr) + "</div>"
    "<div><span class='k'>Sensor:</span> " + String((sensorFailCount == 0) ? "OK" : "Errores") + "</div>"
    "<div><span class='k'>Firmware:</span> " + htmlEscape(fwStr) + "</div>"
    "</div>"
    "</div>"
    "<div class='row' style='margin-top:12px'>"
    "<a class='btn2' href='/'>Volver al panel principal</a>"
    "</div>"
    "</div>"
    "<div class='subcard'>"
    "<p class='subttl'>OTA Firmware</p>"
    "<div class='row'>"
    "<input type='file' id='otaFile' accept='.bin,application/octet-stream' style='max-width:360px'>"
    "<button class='btn' type='button' id='otaBtn'>Actualizar OTA</button>"
    "</div>"
    "<div class='prog' style='margin-top:8px'><div class='bar' id='otaBar'></div></div>"
    "<p class='small' id='otaTxt'>Estado OTA: " + htmlEscape(otaStatus) + "</p>"
    "</div>"
    "</div>"
    "<script>"
    "(()=>{const u=new URL(window.location.href);if(!u.searchParams.has('v')){u.searchParams.set('v',Date.now().toString());window.location.replace(u.toString());}})();"
    "const otaFile=document.getElementById('otaFile');"
    "const otaBtn=document.getElementById('otaBtn');"
    "const otaBar=document.getElementById('otaBar');"
    "const otaTxt=document.getElementById('otaTxt');"
    "function syncBatteryBadge(d){const b=document.getElementById('webBatt');if(!b||!d)return;const s=parseInt(d.batterySegs||0,10);const segs=b.querySelectorAll('.wbseg');for(let i=0;i<segs.length;i++){if(i<s)segs[i].classList.add('on');else segs[i].classList.remove('on');}b.classList.remove('low','mid','ok');if(s<=1)b.classList.add('low');else if(s<=3)b.classList.add('mid');else b.classList.add('ok');if(d.batteryCharging)b.classList.add('charging');else b.classList.remove('charging');}"
    "function pollState(){fetch('/panelstate').then(r=>r.json()).then(d=>{syncBatteryBadge(d);}).catch(()=>{});}"
    "function pollOta(){fetch('/ota-status').then(r=>r.json()).then(d=>{if(otaTxt&&d&&d.status)otaTxt.textContent='Estado OTA: '+d.status;}).catch(()=>{});}"
    "if(otaBtn){otaBtn.addEventListener('click',()=>{if(!otaFile||!otaFile.files||otaFile.files.length===0){alert('Seleccione un .bin');return;}const f=otaFile.files[0];if(!confirm('Actualizar firmware por OTA ahora?'))return;const xhr=new XMLHttpRequest();xhr.open('POST','/ota-upload',true);xhr.setRequestHeader('X-Firmware-Size',String(f.size||0));xhr.upload.onprogress=function(e){if(!e.lengthComputable)return;const p=Math.max(0,Math.min(100,Math.round((e.loaded*100)/e.total)));if(otaBar)otaBar.style.width=p+'%';if(otaTxt)otaTxt.textContent='Estado OTA: subiendo '+p+'%';};xhr.onload=function(){if(xhr.status===200){if(otaBar)otaBar.style.width='100%';if(otaTxt)otaTxt.textContent='Estado OTA: firmware recibido, reiniciando...';}else{if(otaTxt)otaTxt.textContent='Estado OTA: error HTTP '+xhr.status;}};xhr.onerror=function(){if(otaTxt)otaTxt.textContent='Estado OTA: error de red';};const fd=new FormData();fd.append('firmware',f);xhr.send(fd);});}"
    "setInterval(pollState,1000);pollState();setInterval(pollOta,1500);pollOta();"
    "</script>"
    "</body></html>";

  configServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  configServer.sendHeader("Pragma", "no-cache");
  configServer.sendHeader("Expires", "0");
  configServer.send(200, "text/html; charset=utf-8", html);
}

void handleConfigImagePage() {
  if (!webAuthOk()) return;
  currentPage = 2;
  bool themeIsPreset = (photoTheme == "silhouette" || photoTheme == "anime,girl" || photoTheme == "anime" || photoTheme == "city" ||
                        photoTheme == "nature" || photoTheme == "grayscale");
  String modeCurrent = photoUseUploaded ? "upload" : "online";
  String html =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>RLCD Imagen</title>"
    "<style>"
    ":root{--bg:#0b1119;--panel:#101a27;--accent:#39d98a;--txt:#e7eef7;--muted:#93a4b8;}"
    "body{margin:0;background:radial-gradient(900px 500px at 80% -10%,#1a2e47 0%,var(--bg) 55%);color:var(--txt);font-family:Consolas,Monaco,monospace;}"
    ".w{max-width:920px;margin:24px auto;padding:0 14px;}"
    ".card{background:linear-gradient(180deg,#122034 0%,var(--panel) 100%);border:1px solid #2a3b50;border-radius:12px;padding:16px;box-shadow:0 10px 30px rgba(0,0,0,.35);}"
    "h1{font-size:22px;margin:0 0 12px 0;color:var(--accent)}"
    ".cardhead{display:flex;justify-content:space-between;align-items:flex-start;gap:10px;margin-bottom:12px}"
    ".cardhead h1{margin:0}"
    ".cardhead{display:flex;justify-content:space-between;align-items:center;gap:10px;margin-bottom:12px}"
    ".cardhead h1{margin:0}"
    "label{display:block;margin:10px 0 6px}"
    "input,select{width:100%;background:#0b1421;color:var(--txt);border:1px solid #2c425f;border-radius:8px;padding:10px;box-sizing:border-box}"
    ".row{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:14px}"
    ".btn{background:var(--accent);color:#062013;border:0;padding:10px 14px;border-radius:8px;font-weight:700;cursor:pointer;text-decoration:none;display:inline-block}"
    ".btn2{background:#203246;color:#d8e6f7;border:1px solid #39506a;padding:10px 14px;border-radius:8px;font-weight:700;text-decoration:none;display:inline-block}"
    ".small{font-size:12px;color:var(--muted)}"
    ".prog{height:10px;background:#213347;border:1px solid #35506c;border-radius:999px;overflow:hidden;max-width:420px}"
    ".prog>span{display:block;height:100%;width:0%;background:linear-gradient(90deg,#1f91ff,#39d98a);transition:width .2s ease}"
    ".mt16{margin-top:16px}"
    ".sep{height:1px;background:#2a3b50;margin:14px 0}"
    "</style>" + webBatteryBadgeCss() + "</head><body><div class='w'><div class='card'>"
    "<div class='cardhead'><h1>Imagen</h1>" + webBatteryBadgeHtml() + "</div>"
    "<label>Tipo</label>"
    "<select id='imgTypeSel'>"
    "<option value='online'" + String(modeCurrent == "online" ? " selected" : "") + ">Imagen OnLine</option>"
    "<option value='upload'" + String(modeCurrent == "upload" ? " selected" : "") + ">Subir imagen</option>"
    "</select>"
    "<div class='row'>"
    "<a class='btn2' href='/'>Volver al panel principal</a>"
    "</div>"
    "</div></div>"
    "<div class='w'><div class='card' id='cardOnline'>"
    "<h1>Imagen OnLine</h1>"
    "<form method='POST' action='/image-save'>"
    "<label>Tema actual: <b>" + htmlEscape(photoTheme) + "</b></label>"
    "<label>Predefinidos:</label>"
    "<select name='theme'>"
    "<option value=''" + String(themeIsPreset ? "" : " selected") + "></option>"
    "<option value='silhouette'" + String(photoTheme == "silhouette" ? " selected" : "") + ">silhouette</option>"
    "<option value='anime,girl'" + String(photoTheme == "anime,girl" ? " selected" : "") + ">anime,girl</option>"
    "<option value='anime'" + String(photoTheme == "anime" ? " selected" : "") + ">anime</option>"
    "<option value='city'" + String(photoTheme == "city" ? " selected" : "") + ">city</option>"
    "<option value='nature'" + String(photoTheme == "nature" ? " selected" : "") + ">nature</option>"
    "<option value='grayscale'" + String(photoTheme == "grayscale" ? " selected" : "") + ">grayscale</option>"
    "</select>"
    "<label>Presonalizado:</label>"
    "<input name='theme_custom' maxlength='60' placeholder='ej: cyberpunk,anime,girl'>"
    "<label>Tiempo de cambio de imagen</label>"
    "<select name='img_refresh_min'>"
    "<option value='1'" + String(photoRefreshMinutes == 1 ? " selected" : "") + ">1 minuto</option>"
    "<option value='2'" + String(photoRefreshMinutes == 2 ? " selected" : "") + ">2 minutos</option>"
    "<option value='3'" + String(photoRefreshMinutes == 3 ? " selected" : "") + ">3 minutos</option>"
    "<option value='4'" + String(photoRefreshMinutes == 4 ? " selected" : "") + ">4 minutos</option>"
    "<option value='5'" + String(photoRefreshMinutes == 5 ? " selected" : "") + ">5 minutos</option>"
    "<option value='6'" + String(photoRefreshMinutes == 6 ? " selected" : "") + ">6 minutos</option>"
    "<option value='7'" + String(photoRefreshMinutes == 7 ? " selected" : "") + ">7 minutos</option>"
    "<option value='8'" + String(photoRefreshMinutes == 8 ? " selected" : "") + ">8 minutos</option>"
    "<option value='9'" + String(photoRefreshMinutes == 9 ? " selected" : "") + ">9 minutos</option>"
    "<option value='10'" + String(photoRefreshMinutes == 10 ? " selected" : "") + ">10 minutos</option>"
    "</select>"
    "<div class='row'>"
    "<button class='btn' type='submit'>Guardar</button>"
    "</div>"
    "<p class='small'>Al guardar se actualiza el tema de la pantalla Imagen.</p>"
    "</form></div></div>"
    "<div class='w'><div class='card' id='cardUpload'>"
    "<h1>Subir imagen</h1>"
    "<p class='small'>Formato soportado: JPG/JPEG (baseline). Se ajusta automatico a 400x272 antes de subir.</p>"
    "<div class='row'>"
    "<input type='file' id='imgFile' accept='.jpg,.jpeg,image/jpeg' style='max-width:320px'>"
    "<button class='btn' type='button' id='imgUploadBtn' onclick='startUpload()'>Subir imagen fija</button>"
    "</div>"
    "<div class='prog mt16'><span id='imgUpBar'></span></div>"
    "<p class='small' id='imgUpStatus'>Estado: " + htmlEscape(photoUploadStatus) + (photoUseUploaded ? " (activa)" : "") + "</p>"
    "</div></div>"
    "<script>"
    "function syncBatteryBadge(d){"
    "const b=document.getElementById('webBatt');if(!b||!d)return;"
    "const s=parseInt(d.batterySegs||0,10);const segs=b.querySelectorAll('.wbseg');"
    "for(let i=0;i<segs.length;i++){if(i<s)segs[i].classList.add('on');else segs[i].classList.remove('on');}"
    "b.classList.remove('low','mid','ok');if(s<=1)b.classList.add('low');else if(s<=3)b.classList.add('mid');else b.classList.add('ok');"
    "if(d.batteryCharging)b.classList.add('charging');else b.classList.remove('charging');"
    "}"
    "function pollBattery(){fetch('/panelstate').then(r=>r.json()).then(syncBatteryBadge).catch(()=>{});}"
    "setInterval(pollBattery,2000);pollBattery();"
    "var c=document.querySelector('input[name=\"theme_custom\"]');"
    "if(c){c.addEventListener('input',function(){c.value=c.value.toLowerCase().trim().replace(/\\s*,\\s*/g,',');});}"
    "var ts=document.getElementById('imgTypeSel');var co=document.getElementById('cardOnline');var cu=document.getElementById('cardUpload');"
    "var f=document.getElementById('imgFile');var b=document.getElementById('imgUploadBtn');var s=document.getElementById('imgUpStatus');var pb=document.getElementById('imgUpBar');"
    "function setSt(t){if(s)s.textContent='Estado: '+t;}"
    "function setP(v){if(!pb)return;var n=v;if(n<0)n=0;if(n>100)n=100;pb.style.width=n+'%';}"
    "function applyType(){if(!ts||!co||!cu)return;var v=ts.value||'online';co.style.display=(v==='online')?'block':'none';cu.style.display=(v==='upload')?'block':'none';}"
    "if(ts){ts.addEventListener('change',applyType);}applyType();"
    "function canvasToJpegBlob(cnv,cb){if(cnv.toBlob){cnv.toBlob(function(blob){cb(blob||null);},'image/jpeg',0.88);return;}"
    "try{var data=cnv.toDataURL('image/jpeg',0.88);var b64=data.split(',')[1]||'';var bin=atob(b64);var len=bin.length;var arr=new Uint8Array(len);for(var i=0;i<len;i++)arr[i]=bin.charCodeAt(i);cb(new Blob([arr],{type:'image/jpeg'}));}catch(e){cb(null);}"
    "}"
    "function uploadBlob(blob){var fd=new FormData();fd.append('photo',blob,'photo.jpg');var x=new XMLHttpRequest();x.open('POST','/image-upload',true);"
    "setP(35);"
    "x.upload.onprogress=function(e){if(e&&e.lengthComputable){var p=Math.round((e.loaded*100)/e.total);if(p<0)p=0;if(p>100)p=100;var v=35+Math.round(p*0.55);if(v>90)v=90;setP(v);setSt('subiendo '+p+'%...');}};"
    "x.onload=function(){if(x.status>=200&&x.status<300){setP(100);setSt('ok');setTimeout(function(){location.reload();},450);}else{setP(0);setSt((x.responseText&&x.responseText.trim())?x.responseText.trim():('HTTP '+x.status));if(b)b.disabled=false;}};"
    "x.onerror=function(){setP(0);setSt('error de red al subir');if(b)b.disabled=false;};x.send(fd);}"
    "window.startUpload=function(){"
    "if(!f||!f.files||f.files.length===0){setSt('seleccione un JPG');return;}"
    "if(b)b.disabled=true;setP(5);"
    "var file=f.files[0];setSt('preparando imagen...');"
    "var img=new Image();img.onload=function(){"
    "setP(15);"
    "var cw=400,ch=272;var cnv=document.createElement('canvas');cnv.width=cw;cnv.height=ch;var ctx=cnv.getContext('2d');"
    "if(!ctx){setP(0);setSt('canvas no disponible');if(b)b.disabled=false;return;}"
    "var srcW=img.width,srcH=img.height;var dstR=cw/ch,srcR=srcW/srcH;var sx=0,sy=0,sw=srcW,sh=srcH;"
    "if(srcR>dstR){sw=srcH*dstR;sx=(srcW-sw)/2;}else if(srcR<dstR){sh=srcW/dstR;sy=(srcH-sh)/2;}"
    "ctx.drawImage(img,sx,sy,sw,sh,0,0,cw,ch);"
    "setP(28);"
    "canvasToJpegBlob(cnv,function(blob){if(!blob){setP(0);setSt('error creando JPEG');if(b)b.disabled=false;return;}setSt('subiendo...');uploadBlob(blob);});"
    "};"
    "img.onerror=function(){setP(0);setSt('no se pudo leer imagen');if(b)b.disabled=false;};"
    "img.src=URL.createObjectURL(file);"
    "};"
    "if(b){b.addEventListener('click',startUpload);}"
    "</script>"
    "</body></html>";
  configServer.send(200, "text/html; charset=utf-8", html);
}

void handleConfigImageSave() {
  if (!webAuthOk()) return;
  String theme = sanitizeThemeInput(configServer.arg("theme"));
  String custom = sanitizeThemeInput(configServer.arg("theme_custom"));
  int refreshMin = configServer.arg("img_refresh_min").toInt();
  if (refreshMin < 1) refreshMin = 1;
  if (refreshMin > 10) refreshMin = 10;
  if (custom.length() > 0) theme = custom;
  if (theme.length() == 0) theme = photoTheme;
  photoTheme = theme;
  photoRefreshMinutes = refreshMin;
  photoUseUploaded = false;
  photoUploadStatus = "Modo online activo";
  saveRuntimeConfig();
  photoRefreshRequested = true;
  configServer.sendHeader("Location", "/image");
  configServer.send(303, "text/plain", "Guardado");
}

void handleConfigImageUploadDone() {
  if (!webAuthOk()) return;
  if (!photoUploadOk) {
    configServer.send(500, "text/plain; charset=utf-8", photoUploadErr.length() ? photoUploadErr : String("Error upload"));
    return;
  }
  String err;
  if (!loadUploadedPhotoFromFs(err)) {
    photoUploadStatus = "Error al cargar: " + err;
    configServer.send(500, "text/plain; charset=utf-8", photoUploadStatus);
    return;
  }
  photoUseUploaded = true;
  photoRefreshRequested = false;
  photoUploadStatus = "Imagen fija activa (400x272)";
  saveRuntimeConfig();
  configServer.send(200, "text/plain; charset=utf-8", "OK");
}

void handleConfigImageUploadStream() {
  if (!webAuthOk()) return;
  HTTPUpload& upload = configServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    photoUploadOk = false;
    photoUploadErr = "";
    photoUploadBytes = 0;
    photoUploadInProgress = true;
    if (!SPIFFS.begin(true)) {
      photoUploadErr = "SPIFFS no disponible";
      return;
    }
    if (SPIFFS.exists(kUploadedPhotoTmpPath)) SPIFFS.remove(kUploadedPhotoTmpPath);
    photoUploadFile = SPIFFS.open(kUploadedPhotoTmpPath, FILE_WRITE);
    if (!photoUploadFile) {
      photoUploadErr = "No se pudo crear archivo temporal";
      return;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!photoUploadFile || photoUploadErr.length() > 0) return;
    photoUploadBytes += upload.currentSize;
    if (photoUploadBytes > kPhotoMaxBytes) {
      photoUploadErr = "Imagen muy grande";
      return;
    }
    size_t w = photoUploadFile.write(upload.buf, upload.currentSize);
    if (w != upload.currentSize) photoUploadErr = "Error escritura";
  } else if (upload.status == UPLOAD_FILE_END) {
    if (photoUploadFile) photoUploadFile.close();
    photoUploadInProgress = false;
    if (photoUploadErr.length() > 0) {
      if (SPIFFS.exists(kUploadedPhotoTmpPath)) SPIFFS.remove(kUploadedPhotoTmpPath);
      photoUploadOk = false;
      return;
    }
    File rf = SPIFFS.open(kUploadedPhotoTmpPath, FILE_READ);
    if (!rf) { photoUploadErr = "No se pudo validar archivo"; photoUploadOk = false; return; }
    size_t n = (size_t)rf.size();
    if (n == 0 || n > kPhotoMaxBytes) { rf.close(); photoUploadErr = "Tamano invalido"; photoUploadOk = false; return; }
    uint8_t* buf = photoAlloc(n);
    if (!buf) { rf.close(); photoUploadErr = "Sin memoria"; photoUploadOk = false; return; }
    size_t r = rf.read(buf, n);
    rf.close();
    if (r != n) { free(buf); photoUploadErr = "Lectura incompleta"; photoUploadOk = false; return; }
    String err;
    bool ok = validateUploadedPhoto(buf, n, err);
    free(buf);
    if (!ok) {
      photoUploadErr = err;
      if (SPIFFS.exists(kUploadedPhotoTmpPath)) SPIFFS.remove(kUploadedPhotoTmpPath);
      photoUploadOk = false;
      return;
    }
    if (SPIFFS.exists(kUploadedPhotoPath)) SPIFFS.remove(kUploadedPhotoPath);
    if (!SPIFFS.rename(kUploadedPhotoTmpPath, kUploadedPhotoPath)) {
      photoUploadErr = "No se pudo guardar imagen";
      photoUploadOk = false;
      return;
    }
    photoUploadOk = true;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (photoUploadFile) photoUploadFile.close();
    photoUploadInProgress = false;
    photoUploadErr = "Carga cancelada";
    if (SPIFFS.exists(kUploadedPhotoTmpPath)) SPIFFS.remove(kUploadedPhotoTmpPath);
    photoUploadOk = false;
  }
}

void handleConfigRadioPage() {
  if (!webAuthOk()) return;
  currentPage = 13;
  String filterCodec = radioFilterCodec;
  filterCodec.toLowerCase();
  if (filterCodec != "mp3" && filterCodec != "aac" && filterCodec != "aacp" && filterCodec != "any") filterCodec = "mp3";
  int filterBitrate = radioFilterBitrateMax;
  if (filterBitrate != 32 && filterBitrate != 48 && filterBitrate != 64 && filterBitrate != 96 && filterBitrate != 128) filterBitrate = 96;
  if (radioStationName.length() == 0) radioStationName = kFixedStationName;
  if (radioStationUrl.length() == 0) radioStationUrl = kFixedStationUrl;
  String html =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>RLCD Radio</title>"
    "<style>"
    ":root{--bg:#0b1119;--panel:#101a27;--accent:#39d98a;--txt:#e7eef7;--muted:#93a4b8;}"
    "body{margin:0;background:radial-gradient(900px 500px at 80% -10%,#1a2e47 0%,var(--bg) 55%);color:var(--txt);font-family:Consolas,Monaco,monospace;}"
    ".w{max-width:920px;margin:24px auto;padding:0 14px;}"
    ".card{background:linear-gradient(180deg,#122034 0%,var(--panel) 100%);border:1px solid #2a3b50;border-radius:12px;padding:16px;box-shadow:0 10px 30px rgba(0,0,0,.35);overflow:hidden}"
    "h1{font-size:22px;margin:0 0 12px 0;color:var(--accent)}"
    ".cardhead{display:flex;justify-content:space-between;align-items:flex-start;gap:10px;margin-bottom:12px}"
    ".cardhead h1{margin:0}"
    ".grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}"
    ".grid>*{min-width:0}"
    "label{display:block;margin:10px 0 6px}"
    "input,select{width:100%;max-width:100%;min-width:0;background:#0b1421;color:var(--txt);border:1px solid #2c425f;border-radius:8px;padding:10px;box-sizing:border-box}"
    "input[type=range]{width:100%;max-width:100%;min-width:0;box-sizing:border-box;padding:0;margin:0;border:0}"
    ".row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}"
    ".row>*{min-width:0}"
    ".btn{background:var(--accent);color:#062013;border:0;padding:10px 14px;border-radius:8px;font-weight:700;cursor:pointer;text-decoration:none;display:inline-block}"
    ".btn2{background:#203246;color:#d8e6f7;border:1px solid #39506a;padding:10px 14px;border-radius:8px;font-weight:700;text-decoration:none;display:inline-block}"
    ".small{font-size:12px;color:var(--muted);overflow-wrap:anywhere;word-break:break-word}"
    ".state{font-size:16px;color:var(--accent);font-weight:700}"
    "#stationUrlSelected{display:block;max-width:100%;overflow-wrap:anywhere;word-break:break-word}"
    ".presetgrid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;width:100%}"
    ".presetgrid>*{min-width:0}"
    ".preset{border:1px solid #34506f;border-radius:10px;padding:10px;background:linear-gradient(180deg,#13253a 0%,#0c1726 100%);text-decoration:none;color:var(--txt);display:block;min-width:0;max-width:100%;width:100%;box-sizing:border-box;overflow:hidden;box-shadow:0 4px 10px rgba(0,0,0,.28);transition:transform .12s ease,box-shadow .12s ease,border-color .12s ease}"
    ".preset:hover{border-color:#39d98a;box-shadow:0 8px 18px rgba(0,0,0,.38);transform:translateY(-1px)}"
    ".preset:active{transform:translateY(0);box-shadow:0 3px 8px rgba(0,0,0,.25)}"
    ".pt{font-size:11px;color:#7fa5cc;margin:0 0 6px 0;font-weight:700;letter-spacing:.4px}"
    ".pn{display:block;max-width:100%;font-size:15px;font-weight:700;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
    ".dmode{margin-top:10px;display:flex;align-items:center;gap:2px;font-size:13px;color:#f4d7d7}"
    ".dmode input[type=checkbox]{width:auto;display:inline-block;padding:0;margin:0 4px 0 0}"
    "@media(max-width:860px){.grid{grid-template-columns:1fr}.w{padding:0 10px}.card{padding:12px}.presetgrid{grid-template-columns:repeat(2,minmax(0,1fr))}}"
    "</style>" + webBatteryBadgeCss() + "</head><body><div class='w'><div class='card'>"
    "<div class='cardhead'><h1>Radio Browser</h1>" + webBatteryBadgeHtml() + "</div>"
    "<form method='POST' action='/radio-save'>"
    "<div class='grid'>"
    "<div>"
    "<label>Pais</label>"
    "<select id='country' name='country_code'>"
    "<option value='--'>-</option>"
    "<option value='AR'>Argentina</option>"
    "<option value='AU'>Australia</option>"
    "<option value='BO'>Bolivia</option>"
    "<option value='BR'>Brasil</option>"
    "<option value='CA'>Canada</option>"
    "<option value='CL'>Chile</option>"
    "<option value='CN'>China</option>"
    "<option value='CO'>Colombia</option>"
    "<option value='DE'>Alemania</option>"
    "<option value='ES'>Espana</option>"
    "<option value='FR'>Francia</option>"
    "<option value='GB'>Reino Unido</option>"
    "<option value='IT'>Italia</option>"
    "<option value='JP'>Japon</option>"
    "<option value='KR'>Corea del Sur</option>"
    "<option value='MX'>Mexico</option>"
    "<option value='PE'>Peru</option>"
    "<option value='PY'>Paraguay</option>"
    "<option value='RU'>Rusia</option>"
    "<option value='UY'>Uruguay</option>"
    "<option value='US'>Estados Unidos</option>"
    "<option value='VE'>Venezuela</option>"
    "</select>"
    "<label>Tipo de stream</label>"
    "<select id='codecFilter' name='filter_codec'>"
    "<option value='mp3'" + String(filterCodec == "mp3" ? " selected" : "") + ">MP3 (default)</option>"
    "<option value='aac'" + String(filterCodec == "aac" ? " selected" : "") + ">AAC</option>"
    "<option value='aacp'" + String(filterCodec == "aacp" ? " selected" : "") + ">AAC+</option>"
    "<option value='any'" + String(filterCodec == "any" ? " selected" : "") + ">Cualquiera</option>"
    "</select>"
    "<label>Bitrate maximo</label>"
    "<select id='brFilter' name='filter_bitrate_max'>"
    "<option value='96'" + String(filterBitrate == 96 ? " selected" : "") + ">96 kbps (default)</option>"
    "<option value='128'" + String(filterBitrate == 128 ? " selected" : "") + ">128 kbps</option>"
    "<option value='64'" + String(filterBitrate == 64 ? " selected" : "") + ">64 kbps</option>"
    "<option value='48'" + String(filterBitrate == 48 ? " selected" : "") + ">48 kbps</option>"
    "<option value='32'" + String(filterBitrate == 32 ? " selected" : "") + ">32 kbps</option>"
    "</select>"
    "<label>Emisora (Radio Browser)</label>"
    "<select id='stationList'><option value=''>Cargando...</option></select>"
    "</div>"
    "<div>"
    "<label>URL de stream (manual)</label>"
    "<input type='hidden' id='stationName' name='station_name' maxlength='120' value='" + htmlEscape(radioStationName) + "'>"
    "<input type='hidden' id='stationCodec' name='station_codec' value='" + htmlEscape(radioStationCodec) + "'>"
    "<input id='stationUrl' name='station_url' maxlength='280' value='" + htmlEscape(radioStationUrl) + "'>"
    "<p class='small'>URL emisora seleccionada: <span id='stationUrlSelected'>-</span></p>"
    "<label>Volumen radio: <span id='rvtxt'>" + String(radioVolumePct) + "%</span></label>"
    "<input type='range' min='0' max='100' step='5' id='rvol' name='radio_vol' value='" + String(radioVolumePct) + "'>"
    "</div>"
    "</div>"
    "<div class='row' style='margin-top:14px'>"
    "<button class='btn' type='submit'>Reproducir</button>"
    "<a class='btn2' href='#' id='stopBtn'>Stop</a>"
    "<a class='btn2' href='/'>Volver al panel principal</a>"
    "</div>"
    "<p class='state' id='radioStateLine' style='color:var(--accent) !important'>Estado: " + radioStatus + "</p>"
    "<hr style='border-color:#2a3b50;border-style:solid;border-width:1px 0 0 0;margin:14px 0'>"
    "<label>Guardar preset</label>"
    "<div class='row'>"
    "<input id='presetName' name='preset_name' maxlength='80' placeholder='Nombre preset (editable para URL manual)' style='flex:1;min-width:260px'>"
    "<select id='presetSlot' name='preset_slot' style='width:140px'>"
    "<option value='1'>Preset 1</option><option value='2'>Preset 2</option><option value='3'>Preset 3</option><option value='4'>Preset 4</option><option value='5'>Preset 5</option>"
    "<option value='6'>Preset 6</option><option value='7'>Preset 7</option><option value='8'>Preset 8</option><option value='9'>Preset 9</option><option value='10'>Preset 10</option>"
    "</select>"
    "<button class='btn2' type='submit' formaction='/radio-preset-save' formmethod='POST'>Agregar preset</button>"
    "</div>"
    "<label style='margin-top:12px'>Presets (10)</label>"
    "<div class='presetgrid'>"
    "<a class='preset pclick' data-idx='1' href='/radio-preset-load?idx=1'><p class='pt'>#1</p><div class='pn'>" + htmlEscape(radioPresetName[0].length() ? radioPresetName[0] : String("(vacio)")) + "</div></a>"
    "<a class='preset pclick' data-idx='2' href='/radio-preset-load?idx=2'><p class='pt'>#2</p><div class='pn'>" + htmlEscape(radioPresetName[1].length() ? radioPresetName[1] : String("(vacio)")) + "</div></a>"
    "<a class='preset pclick' data-idx='3' href='/radio-preset-load?idx=3'><p class='pt'>#3</p><div class='pn'>" + htmlEscape(radioPresetName[2].length() ? radioPresetName[2] : String("(vacio)")) + "</div></a>"
    "<a class='preset pclick' data-idx='4' href='/radio-preset-load?idx=4'><p class='pt'>#4</p><div class='pn'>" + htmlEscape(radioPresetName[3].length() ? radioPresetName[3] : String("(vacio)")) + "</div></a>"
    "<a class='preset pclick' data-idx='5' href='/radio-preset-load?idx=5'><p class='pt'>#5</p><div class='pn'>" + htmlEscape(radioPresetName[4].length() ? radioPresetName[4] : String("(vacio)")) + "</div></a>"
    "<a class='preset pclick' data-idx='6' href='/radio-preset-load?idx=6'><p class='pt'>#6</p><div class='pn'>" + htmlEscape(radioPresetName[5].length() ? radioPresetName[5] : String("(vacio)")) + "</div></a>"
    "<a class='preset pclick' data-idx='7' href='/radio-preset-load?idx=7'><p class='pt'>#7</p><div class='pn'>" + htmlEscape(radioPresetName[6].length() ? radioPresetName[6] : String("(vacio)")) + "</div></a>"
    "<a class='preset pclick' data-idx='8' href='/radio-preset-load?idx=8'><p class='pt'>#8</p><div class='pn'>" + htmlEscape(radioPresetName[7].length() ? radioPresetName[7] : String("(vacio)")) + "</div></a>"
    "<a class='preset pclick' data-idx='9' href='/radio-preset-load?idx=9'><p class='pt'>#9</p><div class='pn'>" + htmlEscape(radioPresetName[8].length() ? radioPresetName[8] : String("(vacio)")) + "</div></a>"
    "<a class='preset pclick' data-idx='10' href='/radio-preset-load?idx=10'><p class='pt'>#10</p><div class='pn'>" + htmlEscape(radioPresetName[9].length() ? radioPresetName[9] : String("(vacio)")) + "</div></a>"
    "</div>"
    "<label class='dmode'><input type='checkbox' id='presetDeleteMode'> Eliminar</label>"
    "</form></div></div>"
    "<script>"
    "function syncBatteryBadge(d){"
    "const b=document.getElementById('webBatt');if(!b||!d)return;"
    "const s=parseInt(d.batterySegs||0,10);const segs=b.querySelectorAll('.wbseg');"
    "for(let i=0;i<segs.length;i++){if(i<s)segs[i].classList.add('on');else segs[i].classList.remove('on');}"
    "b.classList.remove('low','mid','ok');if(s<=1)b.classList.add('low');else if(s<=3)b.classList.add('mid');else b.classList.add('ok');"
    "if(d.batteryCharging)b.classList.add('charging');else b.classList.remove('charging');"
    "}"
    "function pollBattery(){fetch('/panelstate').then(r=>r.json()).then(syncBatteryBadge).catch(()=>{});}"
    "setInterval(pollBattery,2000);pollBattery();"
    "const stopBtn=document.getElementById('stopBtn');"
    "const stLine=document.getElementById('radioStateLine');"
    "const rv=document.getElementById('rvol');const rvt=document.getElementById('rvtxt');"
    "const cc=document.getElementById('country');"
    "const cf=document.getElementById('codecFilter');"
    "const bf=document.getElementById('brFilter');"
    "const sl=document.getElementById('stationList');"
    "const sn=document.getElementById('stationName');"
    "const sc=document.getElementById('stationCodec');"
    "const su=document.getElementById('stationUrl');"
    "const sus=document.getElementById('stationUrlSelected');"
    "let currentUrl=(su&&su.value)?su.value.trim():'';"
    "if(sus&&currentUrl)sus.textContent=currentUrl;"
    "const LS_CC='rlcd_radio_country';"
    "const LS_CF='rlcd_radio_codec_filter';"
    "const LS_BF='rlcd_radio_bitrate_filter';"
    "function normVol(v){v=parseInt(v||0,10);if(isNaN(v))v=0;if(v<0)v=0;if(v>100)v=100;return v;}"
    "if(rv&&rvt){rv.addEventListener('input',()=>{rvt.textContent=normVol(rv.value)+'%';fetch('/radio-live?vol='+encodeURIComponent(rv.value)).catch(()=>{});});}"
    "function loadStations(){if(!cc||!sl)return;"
    "if(cc.value==='--'){sl.innerHTML='<option value=\"\">URL manual</option>';return;}"
    "sl.innerHTML='<option value=\"\">Cargando...</option>';"
    "const brMax=(bf&&bf.value)?parseInt(bf.value,10):96;"
    "const csel=(cf&&cf.value)?cf.value:'mp3';"
    "fetch('https://de1.api.radio-browser.info/json/stations/bycountrycodeexact/'+encodeURIComponent(cc.value)+'?hidebroken=true&bitrateMax='+encodeURIComponent(brMax)+'&limit=200&order=votes&reverse=true')"
    ".then(r=>r.json()).then(a=>{sl.innerHTML='';if(!Array.isArray(a)||a.length===0){sl.innerHTML='<option value=\"\">Sin emisoras</option>';return;}"
    "let added=0;let selIdx=-1;"
    "a.forEach(s=>{"
    "const u=(s.url_resolved||s.url||'').trim();"
    "if(!u||!(u.indexOf('http://')===0||u.indexOf('https://')===0))return;"
    "const br=parseInt(s.bitrate||0,10);"
    "if(isNaN(br)||br<=0)return;"
    "if(!isNaN(br)&&!isNaN(brMax)&&br>brMax)return;"
    "const c=(s.codec||'').toLowerCase();"
    "if(csel==='mp3'){if(c!=='mp3')return;}"
    "else if(csel==='aac'){if(c!=='aac'&&c!=='he-aac'&&c!=='heaac')return;}"
    "else if(csel==='aacp'){if(c!=='aac+'&&c!=='aacp'&&c!=='he-aac'&&c!=='heaac')return;}"
    "else {if(c&&c!=='mp3'&&c!=='aac'&&c!=='aac+'&&c!=='aacp'&&c!=='he-aac'&&c!=='heaac')return;}"
    "const o=document.createElement('option');o.value=u;o.textContent=(s.name||'sin nombre')+' ['+(s.codec||'-')+' '+(isNaN(br)?'-':br)+'kbps]';o.dataset.n=s.name||'';o.dataset.c=(s.codec||'').toLowerCase();o.dataset.cc=(s.countrycode||cc.value||'').toUpperCase();"
    "if(currentUrl&&u===currentUrl)selIdx=added;"
    "sl.appendChild(o);added++;"
    "});"
    "if(added===0){sl.innerHTML='<option value=\"\">Sin streams <= '+brMax+'kbps compatibles</option>';return;}"
    "if(selIdx>=0){sl.selectedIndex=selIdx;const cur=sl.options[sl.selectedIndex];if(cur){sn.value=cur.dataset.n||cur.textContent;if(sc)sc.value=(cur.dataset.c||'');su.value=cur.value;currentUrl=cur.value;if(sus)sus.textContent=cur.value;}}"
    "}).catch(()=>{sl.innerHTML='<option value=\"\">Error API</option>';});}"
    "if(cc){const scc=localStorage.getItem(LS_CC);const dcc='" + htmlEscape((radioCountry == "--" || radioCountry.length()==2) ? radioCountry : String("AR")) + "';const hasSrv="+ String(radioStationUrl.length() > 0 ? "true" : "false") + ";cc.value=hasSrv?dcc:((scc&&(scc.length===2||scc==='--'))?scc:dcc);cc.addEventListener('change',()=>{localStorage.setItem(LS_CC,cc.value);loadStations();});}"
    "if(cf){const scf=localStorage.getItem(LS_CF);if(scf)cf.value=scf;cf.addEventListener('change',()=>{localStorage.setItem(LS_CF,cf.value);loadStations();});}"
    "if(bf){const sbf=localStorage.getItem(LS_BF);if(sbf)bf.value=sbf;bf.addEventListener('change',()=>{localStorage.setItem(LS_BF,bf.value);loadStations();});}"
    "const frm=document.querySelector('form[action=\"/radio-save\"]');"
    "if(frm){frm.addEventListener('submit',()=>{if(cc)localStorage.setItem(LS_CC,cc.value||'AR');if(cf)localStorage.setItem(LS_CF,cf.value||'mp3');if(bf)localStorage.setItem(LS_BF,bf.value||'96');});}"
    "loadStations();"
    "if(sl){sl.addEventListener('change',()=>{const o=sl.options[sl.selectedIndex];if(!o)return;sn.value=o.dataset.n||o.textContent;if(sc)sc.value=(o.dataset.c||'');su.value=o.value;if(sus)sus.textContent=o.value;if(cc){const pc=(o.dataset.cc||'').toUpperCase();if(pc.length===2){cc.value=pc;localStorage.setItem(LS_CC,pc);}}});}"
    "if(su){su.addEventListener('input',()=>{const u=(su.value||'').trim();if(sus)sus.textContent=u||'-';if(sn)sn.value=u;if(cc){cc.value='--';localStorage.setItem(LS_CC,'--');}});}"
    "const pn=document.getElementById('presetName');if(pn&&sn&&su){pn.value=(su.value&&sn.value&&su.value.trim()===sn.value.trim())?'':(sn.value||'');}"
    "const ps=document.getElementById('presetSlot');"
    "if(ps){const cards=document.querySelectorAll('.pclick');let nextEmpty='';cards.forEach(c=>{if(nextEmpty)return;const idx=c.getAttribute('data-idx')||'';const n=(c.querySelector('.pn')?.textContent||'').trim().toLowerCase();if(n==='(vacio)')nextEmpty=idx;});if(nextEmpty)ps.value=nextEmpty;}"
    "const delMode=document.getElementById('presetDeleteMode');"
    "document.querySelectorAll('.pclick').forEach(a=>{a.addEventListener('click',function(e){"
    "const idx=this.getAttribute('data-idx')||'0';"
    "if(delMode&&delMode.checked){e.preventDefault();if(confirm('Eliminar preset '+idx+'?')){window.location.href='/radio-preset-delete?idx='+encodeURIComponent(idx)+'&confirm=1';}}"
    "});});"
    "function updateRadioState(){fetch('/radio-state').then(r=>r.json()).then(d=>{"
    "if(stLine)stLine.textContent='Estado: '+(d.status||'-');"
    "if(rv&&rvt){rv.value=String(d.volume||0);rvt.textContent=normVol(rv.value)+'%';}"
    "}).catch(()=>{});}"
    "if(stopBtn){stopBtn.addEventListener('click',function(e){e.preventDefault();fetch('/radio-toggle').then(()=>updateRadioState()).catch(()=>{});});}"
    "updateRadioState();setInterval(updateRadioState,1500);"
    "</script>"
    "</body></html>";
  configServer.send(200, "text/html; charset=utf-8", html);
}

void handleConfigRadioSave() {
  if (!webAuthOk()) return;
  // Ensure clean handover when user presses Reproducir:
  // stop current stream first, then schedule new one.
  if (radioPlaying || radioStartRequested) {
    radioUserStopped = true;
    radioStartRequested = false;
    stopRadioPlayback();
  }

  String cc = configServer.arg("country_code");
  String fCodec = configServer.arg("filter_codec");
  String fBr = configServer.arg("filter_bitrate_max");
  String stName = configServer.arg("station_name");
  String stUrl = configServer.arg("station_url");
  String stCodec = configServer.arg("station_codec");
  cc.trim();
  cc.toUpperCase();
  fCodec.trim();
  fCodec.toLowerCase();
  int fBrMax = fBr.toInt();
  if (cc != "--" && cc.length() != 2) cc = "AR";
  if (fCodec != "mp3" && fCodec != "aac" && fCodec != "aacp" && fCodec != "any") fCodec = "mp3";
  if (fBrMax != 32 && fBrMax != 48 && fBrMax != 64 && fBrMax != 96 && fBrMax != 128) fBrMax = 96;
  stName.trim();
  stUrl.trim();
  stCodec.trim();
  stCodec.toLowerCase();
  if (stName.length() == 0) stName = kFixedStationName;
  if (stUrl.length() == 0) stUrl = kFixedStationUrl;
  if (stCodec.length() == 0) {
    String lurl = stUrl;
    lurl.toLowerCase();
    stCodec = (lurl.indexOf(".aac") >= 0 || lurl.indexOf(".m4a") >= 0) ? "aac" : "mp3";
  }

  int rvol = configServer.arg("radio_vol").toInt();
  rvol = ((rvol + 2) / 5) * 5;
  if (rvol < 0) rvol = 0;
  if (rvol > 100) rvol = 100;

  radioCountry = cc;
  radioFilterCodec = fCodec;
  radioFilterBitrateMax = fBrMax;
  radioStationName = stName;
  radioStationUrl = stUrl;
  radioStationCodec = stCodec;
  radioVolumePct = rvol;
  radioMuted = false;
  saveRuntimeConfig();
  applyRadioVolume();
  radioUserStopped = false;
  radioStartRequested = true;

  configServer.sendHeader("Location", "/radio");
  configServer.send(303, "text/plain", "Guardado");
}

void handleConfigRadioPresetSave() {
  if (!webAuthOk()) return;
  int idx = configServer.arg("preset_slot").toInt();
  if (idx < 1 || idx > RADIO_PRESET_COUNT) idx = 1;
  idx -= 1;

  String name = configServer.arg("preset_name");
  String url = configServer.arg("station_url");
  String codec = configServer.arg("station_codec");
  String cc = configServer.arg("country_code");
  name.trim();
  url.trim();
  codec.trim();
  codec.toLowerCase();
  cc.trim();
  cc.toUpperCase();
  if (codec.length() == 0) codec = "mp3";
  if (cc != "--" && cc.length() != 2) cc = "AR";
  bool isManual = (url == configServer.arg("station_name"));
  if (isManual) cc = "--";
  if (url.length() == 0) {
    configServer.sendHeader("Location", "/radio");
    configServer.send(303, "text/plain", "Sin URL");
    return;
  }
  if (name.length() == 0) name = configServer.arg("station_name");
  name.trim();
  if (name.length() == 0) name = "Preset " + String(idx + 1);

  radioPresetName[idx] = name;
  radioPresetUrl[idx] = url;
  radioPresetCodec[idx] = codec;
  radioPresetCountry[idx] = cc;
  saveRuntimeConfig();

  configServer.sendHeader("Location", "/radio");
  configServer.send(303, "text/plain", "Preset guardado");
}

void handleConfigRadioPresetLoad() {
  if (!webAuthOk()) return;
  int idx = configServer.arg("idx").toInt();
  if (idx < 1 || idx > RADIO_PRESET_COUNT) idx = 1;
  idx -= 1;
  if (radioPresetUrl[idx].length() == 0) {
    configServer.sendHeader("Location", "/radio");
    configServer.send(303, "text/plain", "Preset vacio");
    return;
  }
  radioStationName = radioPresetName[idx];
  radioStationUrl = radioPresetUrl[idx];
  radioStationCodec = radioPresetCodec[idx];
  radioCountry = radioPresetCountry[idx];
  radioCountry.trim();
  radioCountry.toUpperCase();
  if (radioCountry != "--" && radioCountry.length() != 2) radioCountry = "AR";
  if (radioStationCodec.length() == 0) radioStationCodec = "mp3";
  currentPage = 13;  // Keep radio engine active after preset selection from local UI
  radioUserStopped = false;
  radioStartRequested = true;
  saveRuntimeConfig();
  configServer.sendHeader("Location", "/radio");
  configServer.send(303, "text/plain", "Preset cargado");
}

void handleConfigRadioPresetDelete() {
  if (!webAuthOk()) return;
  String confirmArg = configServer.arg("confirm");
  if (confirmArg != "1") {
    configServer.sendHeader("Location", "/radio");
    configServer.send(303, "text/plain", "Cancelado");
    return;
  }
  int idx = configServer.arg("idx").toInt();
  if (idx < 1 || idx > RADIO_PRESET_COUNT) idx = 1;
  idx -= 1;
  radioPresetName[idx] = "";
  radioPresetUrl[idx] = "";
  radioPresetCodec[idx] = "mp3";
  radioPresetCountry[idx] = "AR";
  saveRuntimeConfig();
  configServer.sendHeader("Location", "/radio");
  configServer.send(303, "text/plain", "Preset borrado");
}

void handleConfigRadioToggle() {
  if (!webAuthOk()) return;
  radioUserStopped = true;
  radioStartRequested = false;
  stopRadioPlayback();
  configServer.send(200, "text/plain; charset=utf-8", "stopped");
}

void handleConfigRadioState() {
  if (!webAuthOk()) return;
  String st = radioStatus;
  st.replace("\\", "\\\\");
  st.replace("\"", "\\\"");
  String out = String("{\"playing\":") + (radioPlaying ? "true" : "false") +
               ",\"muted\":false" +
               ",\"volume\":" + String(radioVolumePct) +
               ",\"status\":\"" + st + "\"}";
  configServer.send(200, "application/json; charset=utf-8", out);
}

void handleConfigRadioLive() {
  if (!webAuthOk()) return;
  if (configServer.hasArg("vol")) {
    int rvol = configServer.arg("vol").toInt();
    rvol = ((rvol + 2) / 5) * 5;
    if (rvol < 0) rvol = 0;
    if (rvol > 100) rvol = 100;
    radioVolumePct = rvol;
    applyRadioVolume();
  }
  radioMuted = false;
  configServer.send(200, "text/plain; charset=utf-8", "ok");
}

void handleConfigSave() {
  if (!webAuthOk()) return;
  String theme = sanitizeThemeInput(configServer.arg("theme"));
  String custom = sanitizeThemeInput(configServer.arg("theme_custom"));
  if (custom.length() > 0) theme = custom;
  if (theme.length() == 0) theme = photoTheme;  // keep current if select is empty and no custom value
  photoTheme = theme;

  int vol = configServer.arg("vol").toInt();
  vol = ((vol + 2) / 5) * 5;
  if (vol < 0) vol = 0;
  if (vol > 100) vol = 100;
  audioVolumePct = vol;
  batteryCriticalBeepEnabled = configServer.hasArg("beep_bat");
  issFootprintBeepEnabled = configServer.hasArg("beep_iss");
  displayInvertMode = configServer.hasArg("disp_inv");
  bool newBatterySave = configServer.hasArg("bat_save");
  bool batterySaveChanged = (batterySaveMode != newBatterySave);
  batterySaveMode = newBatterySave;

  applyAudioVolumeToCodec();
  applyPowerProfile();
  saveRuntimeConfig();
  photoRefreshRequested = true;
  if (batterySaveChanged && batterySaveMode) {
    currentPage = 0;
  }

  configServer.sendHeader("Location", "/");
  configServer.send(303, "text/plain", "Guardado");
}

void handleConfigSystemSave() {
  if (!webAuthOk()) return;
  // MQTT removido en edicion YT/GitHub.
  configServer.sendHeader("Location", "/system");
  configServer.send(303, "text/plain", "Guardado");
}

void handleConfigLogout() {
  // Rotate realm so next visit to "/" requests credentials again.
  webAuthRealm = "RLCD-Config-" + String((unsigned long)millis());
  configServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  configServer.sendHeader("Pragma", "no-cache");
  configServer.sendHeader("Expires", "0");
  configServer.send(200, "text/html; charset=utf-8",
                    "<!doctype html><html><body style='font-family:monospace'>"
                    "<h3>Sesion cerrada</h3>"
                    "<p>Redirigiendo al panel...</p>"
                    "<script>setTimeout(function(){location.replace('/');},180);</script>"
                    "</body></html>");
}

void handleConfigBeep() {
  if (!webAuthOk()) return;
  unsigned long now = millis();
  if (now - lastWebVolumeBeepMs < 180UL) {
    configServer.send(200, "text/plain", "skip");
    return;
  }
  int vol = configServer.arg("v").toInt();
  vol = ((vol + 2) / 5) * 5;
  if (vol < 0) vol = 0;
  if (vol > 100) vol = 100;
  audioVolumePct = vol;
  applyAudioVolumeToCodec();
  lastWebVolumeBeepMs = now;
  playToneHz(1200, 70);
  configServer.send(200, "text/plain", "ok");
}

void handleConfigNextPage() {
  if (!webAuthOk()) return;
  currentPage = (currentPage + 1) % totalPages;
  configServer.send(200, "text/plain; charset=utf-8", "ok");
}

void handleConfigPrevPage() {
  if (!webAuthOk()) return;
  currentPage = (currentPage - 1 + totalPages) % totalPages;
  configServer.send(200, "text/plain; charset=utf-8", "ok");
}

void handleConfigSetPage() {
  if (!webAuthOk()) return;
  if (!configServer.hasArg("p")) {
    configServer.send(400, "text/plain; charset=utf-8", "missing p");
    return;
  }
  int p = configServer.arg("p").toInt();
  if (p < 0 || p >= totalPages) {
    configServer.send(400, "text/plain; charset=utf-8", "invalid p");
    return;
  }
  currentPage = p;
  if (currentPage == 2) photoRefreshRequested = true;
  if (currentPage == 1) kumaRefreshRequested = true;
  configServer.send(200, "text/plain; charset=utf-8", "ok");
}

void handleConfigPanelState() {
  if (!webAuthOk()) return;
  int segs = batteryToSegments(batteryVoltage);
  if (segs < 0) segs = 0;
  if (segs > 5) segs = 5;
  String out = "{\"currentPage\":" + String(currentPage) +
               ",\"batteryCharging\":" + String(batteryCharging ? "true" : "false") +
               ",\"batterySegs\":" + String(segs) +
               ",\"displayInvert\":" + String(displayInvertMode ? "true" : "false") +
               ",\"batterySave\":" + String(batterySaveMode ? "true" : "false") +
               ",\"mqttConnected\":" + String(mqttConnected ? "true" : "false") + "}";
  configServer.send(200, "application/json; charset=utf-8", out);
}

void handleConfigReboot() {
  if (!webAuthOk()) return;
  configServer.send(200, "text/plain; charset=utf-8", "reiniciando");
  delay(150);
  ESP.restart();
}

void handleConfigOtaStatus() {
  if (!webAuthOk()) return;
  String st = otaStatus;
  st.replace("\\", "\\\\");
  st.replace("\"", "\\\"");
  String out = String("{\"status\":\"") + st + "\"}";
  configServer.send(200, "application/json; charset=utf-8", out);
}

void handleConfigOtaUploadDone() {
  if (!webAuthOk()) return;
  bool ok = !Update.hasError();
  if (ok) {
    otaProgressPct = 100;
    otaStatus = "OTA OK. Reiniciando...";
    configServer.send(200, "text/plain; charset=utf-8", "OK");
    delay(250);
    ESP.restart();
  } else {
    otaInProgress = false;
    otaStatus = "OTA error: " + String(Update.errorString());
    configServer.send(500, "text/plain; charset=utf-8", otaStatus);
  }
}

void handleConfigOtaUploadStream() {
  if (!webAuthOk()) return;
  HTTPUpload& upload = configServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    otaInProgress = false;
    otaProgressPct = 0;
    otaUploadBytes = 0;
    otaExpectedBytes = 0;
    String expHdr = configServer.header("X-Firmware-Size");
    if (expHdr.length() > 0) otaExpectedBytes = (size_t)expHdr.toInt();
    otaUploadingFilename = upload.filename;
    int slashPos = otaUploadingFilename.lastIndexOf('/');
    if (slashPos >= 0) otaUploadingFilename = otaUploadingFilename.substring(slashPos + 1);
    int bslashPos = otaUploadingFilename.lastIndexOf('\\');
    if (bslashPos >= 0) otaUploadingFilename = otaUploadingFilename.substring(bslashPos + 1);
    otaUploadingFilename.trim();
    if (otaUploadingFilename.length() == 0) otaUploadingFilename = "firmware.bin";
    otaStatus = "OTA iniciando...";
    // Persist currently visible page so it is restored after reboot.
    saveRuntimeConfig();
    // Capture current page and draw OTA floating dialog immediately.
    draw();
    drawOtaProgressOverlay();
    delay(180);
    otaInProgress = true;
    if (radioPlaying || radioStartRequested) {
      radioUserStopped = true;
      radioStartRequested = false;
      stopRadioPlayback();
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      otaStatus = "OTA error begin: " + String(Update.errorString());
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    otaUploadBytes = upload.totalSize;
    if (otaExpectedBytes > 0) {
      otaProgressPct = (int)((otaUploadBytes * 100UL) / otaExpectedBytes);
      if (otaProgressPct < 0) otaProgressPct = 0;
      if (otaProgressPct > 99) otaProgressPct = 99; // keep 100 for validated end
    }
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      otaInProgress = false;
      otaStatus = "OTA error write: " + String(Update.errorString());
      Update.printError(Serial);
    } else {
      otaStatus = "OTA subiendo... " + String(upload.totalSize / 1024) + " KB";
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      otaProgressPct = 100;
      otaStatus = "OTA validada (" + String(upload.totalSize / 1024) + " KB)";
      currentFirmwareName = otaUploadingFilename;
      if (preferences.begin("catdisplay", false)) {
        preferences.putString("fw_name", currentFirmwareName);
        preferences.end();
      }
    } else {
      otaInProgress = false;
      otaStatus = "OTA error end: " + String(Update.errorString());
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaInProgress = false;
    otaStatus = "OTA abortada";
  }
}

void startConfigServer() {
  const char* headers[] = { "X-Firmware-Size" };
  configServer.collectHeaders(headers, 1);
  configServer.on("/", HTTP_GET, handleConfigRoot);
  configServer.on("/system", HTTP_GET, handleConfigSystemPage);
  configServer.on("/save", HTTP_POST, handleConfigSave);
  configServer.on("/system-save", HTTP_POST, handleConfigSystemSave);
  configServer.on("/image", HTTP_GET, handleConfigImagePage);
  configServer.on("/image-save", HTTP_POST, handleConfigImageSave);
  configServer.on("/image-upload", HTTP_POST, handleConfigImageUploadDone, handleConfigImageUploadStream);
  configServer.on("/image-upload-disable", HTTP_POST, []() {
    if (!webAuthOk()) return;
    photoUseUploaded = false;
    photoUploadStatus = "Modo online activo";
    saveRuntimeConfig();
    photoRefreshRequested = true;
    configServer.send(200, "text/plain; charset=utf-8", "OK");
  });
  configServer.on("/radio", HTTP_GET, handleConfigRadioPage);
  configServer.on("/radio-save", HTTP_POST, handleConfigRadioSave);
  configServer.on("/radio-preset-save", HTTP_POST, handleConfigRadioPresetSave);
  configServer.on("/radio-preset-load", HTTP_GET, handleConfigRadioPresetLoad);
  configServer.on("/radio-preset-delete", HTTP_GET, handleConfigRadioPresetDelete);
  configServer.on("/radio-toggle", HTTP_GET, handleConfigRadioToggle);
  configServer.on("/radio-state", HTTP_GET, handleConfigRadioState);
  configServer.on("/radio-live", HTTP_GET, handleConfigRadioLive);
  configServer.on("/beep", HTTP_GET, handleConfigBeep);
  configServer.on("/prevpage", HTTP_GET, handleConfigPrevPage);
  configServer.on("/nextpage", HTTP_GET, handleConfigNextPage);
  configServer.on("/setpage", HTTP_GET, handleConfigSetPage);
  configServer.on("/panelstate", HTTP_GET, handleConfigPanelState);
  configServer.on("/reboot", HTTP_GET, handleConfigReboot);
  configServer.on("/ota-status", HTTP_GET, handleConfigOtaStatus);
  configServer.on("/ota-upload", HTTP_POST, handleConfigOtaUploadDone, handleConfigOtaUploadStream);
  configServer.onNotFound([]() {
    if (!webAuthOk()) return;
    configServer.send(404, "text/plain", "404");
  });
  configServer.begin();
  Serial.print("Config web: http://");
  Serial.println(WiFi.localIP());
}

// ===== WIFI & NTP =====
void connectWiFi() {
  WiFi.mode(WIFI_STA);

  wifiUsedPortal = false;
  wifiPortalSSID = "";
  wifiPortalIP = "";

  Serial.println("WiFi historial begin...");
  if (tryConnectFromWifiHistory()) {
    wifiConnected = true;
    wifiRSSI = WiFi.RSSI();
    if (connectedWifiPassword.length() < 8) connectedWifiPassword = getStoredWifiPasswordForSSID(WiFi.SSID());
    updateWebAdminPasswordFromWiFi();
    Serial.println("WiFi OK (historial)");
    Serial.println(WiFi.localIP());
    return;
  }

  // Try native reconnect first to avoid WiFiManager autoConnect crash on some core versions.
  Serial.println("WiFi native begin...");
  WiFi.begin();
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 12000) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    wifiRSSI = WiFi.RSSI();
    String nssid = WiFi.SSID();
    String npsk = WiFi.psk();
    connectedWifiPassword = npsk;
    if (nssid.length() > 0 && npsk.length() > 0) saveWifiHistoryEntry(nssid, npsk);
    updateWebAdminPasswordFromWiFi();
    Serial.println("WiFi OK (native)");
    Serial.println(WiFi.localIP());
    return;
  }

  WiFiManager wm;
  WiFiManagerParameter tzSelParam("tzsel", "Preset zona (AR/CL/CO/UTC/CET/JST)", "AR", 7);
  WiFiManagerParameter tzParam("tz", "Zona horaria POSIX", configuredPosixTZ, 63);
  WiFiManagerParameter tzLblParam("tzlbl", "Etiqueta zona (pantalla)", configuredTZLabel, 15);
  wm.addParameter(&tzSelParam);
  wm.addParameter(&tzParam);
  wm.addParameter(&tzLblParam);
  wm.setConnectTimeout(20);

  wm.setAPCallback([](WiFiManager* mgr) {
    wifiUsedPortal = true;
    wifiPortalSSID = String(mgr->getConfigPortalSSID());
    wifiPortalIP = WiFi.softAPIP().toString();
    drawWifiStatusScreen(
      "WiFi Modo AP",
      "SSID: " + wifiPortalSSID,
      "IP: " + wifiPortalIP,
      "Abre esta IP para configurar"
    );
  });

  Serial.println("WiFiManager portal...");
  bool ok = wm.startConfigPortal(kConfigApSsid);

  String preset = String(tzSelParam.getValue());
  String newTz = String(tzParam.getValue());
  String newLbl = String(tzLblParam.getValue());
  if (wifiUsedPortal) applyTimeZonePreset(preset, newTz, newLbl);
  newTz.trim();
  newLbl.trim();
  if (newTz.length() == 0) newTz = kDefaultPosixTZ;
  if (newLbl.length() == 0) newLbl = kDefaultTZLabel;
  snprintf(configuredPosixTZ, sizeof(configuredPosixTZ), "%s", newTz.c_str());
  snprintf(configuredTZLabel, sizeof(configuredTZLabel), "%s", newLbl.c_str());
  saveTimeZoneConfig(configuredPosixTZ, configuredTZLabel);
  applyConfiguredTimeZone();

  if (ok && WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    wifiRSSI = WiFi.RSSI();
    String pssid = wm.getWiFiSSID();
    String ppass = wm.getWiFiPass();
    connectedWifiPassword = ppass;
    if (pssid.length() > 0 && ppass.length() > 0) saveWifiHistoryEntry(pssid, ppass);
    updateWebAdminPasswordFromWiFi();
    Serial.println("WiFi OK (portal)");
    Serial.println(WiFi.localIP());
  } else {
    wifiConnected = false;
    Serial.println("WiFi fallo");
  }
}

bool fetchCityFromExternalIP() {
  if (!wifiConnected) return false;

  auto extractNumberByKey = [](const String& src, const char* key) -> float {
    int p = src.indexOf(key);
    if (p < 0) return NAN;
    p += strlen(key);
    while (p < (int)src.length() && (src[p] == ' ' || src[p] == '\t')) p++;
    int e = p;
    while (e < (int)src.length()) {
      char c = src[e];
      if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') e++;
      else break;
    }
    if (e <= p) return NAN;
    String n = src.substring(p, e);
    n.trim();
    return n.toFloat();
  };

  const char* urls[] = {
    "https://ipapi.co/json/",
    "http://ip-api.com/json/?fields=city,lat,lon"
  };

  for (size_t i = 0; i < (sizeof(urls) / sizeof(urls[0])); i++) {
    HTTPClient http;
    http.begin(urls[i]);
    http.setTimeout(10000);
    int code = http.GET();
    if (code != 200) { http.end(); continue; }
    String payload = http.getString();
    http.end();
    if (payload.length() < 20) continue;

    int key = payload.indexOf("\"city\"");
    if (key < 0) continue;
    int colon = payload.indexOf(":", key);
    if (colon < 0) continue;
    int q1 = payload.indexOf("\"", colon + 1);
    if (q1 < 0) continue;
    int q2 = payload.indexOf("\"", q1 + 1);
    if (q2 < 0) continue;

    String city = payload.substring(q1 + 1, q2);
    city.trim();
    if (city.length() == 0 || city.equalsIgnoreCase("null")) continue;
    detectedCity = city;

    float lat = extractNumberByKey(payload, "\"latitude\":");
    if (isnan(lat)) lat = extractNumberByKey(payload, "\"lat\":");
    float lon = extractNumberByKey(payload, "\"longitude\":");
    if (isnan(lon)) lon = extractNumberByKey(payload, "\"lon\":");
    if (!isnan(lat) && !isnan(lon)) {
      detectedLat = lat;
      detectedLon = lon;
      detectedLocationValid = true;
    }
    return true;
  }

  return false;
}

void addIssTrailPoint(float lat, float lon) {
  if (issTrailCount > 0) {
    float dLat = fabsf(issTrailLat[issTrailCount - 1] - lat);
    float dLon = fabsf(issTrailLon[issTrailCount - 1] - lon);
    if (dLon > 180.0f) dLon = 360.0f - dLon;
    if (dLat < 0.05f && dLon < 0.05f) return;
  }

  if (issTrailCount < ISS_TRAIL_MAX) {
    issTrailLat[issTrailCount] = lat;
    issTrailLon[issTrailCount] = lon;
    issTrailCount++;
    return;
  }

  for (int i = 1; i < ISS_TRAIL_MAX; i++) {
    issTrailLat[i - 1] = issTrailLat[i];
    issTrailLon[i - 1] = issTrailLon[i];
  }
  issTrailLat[ISS_TRAIL_MAX - 1] = lat;
  issTrailLon[ISS_TRAIL_MAX - 1] = lon;
}

bool fetchIssData() {
  if (!wifiConnected) {
    issData.valid = false;
    issData.lastError = "Sin WiFi";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  const char* url = "https://api.wheretheiss.at/v1/satellites/25544";
  if (!http.begin(client, url)) {
    issData.valid = false;
    issData.lastError = "Error inicio HTTPS";
    return false;
  }

  http.setTimeout(8000);
  int code = http.GET();
  if (code != 200) {
    http.end();
    issData.valid = false;
    issData.lastError = (code < 0) ? "Error red ISS" : ("HTTP ISS " + String(code));
    return false;
  }

  String payload = http.getString();
  http.end();
  if (payload.length() < 40) {
    issData.valid = false;
    issData.lastError = "Respuesta ISS vacia";
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) {
    issData.valid = false;
    issData.lastError = "JSON ISS invalido";
    return false;
  }

  if (!doc["latitude"].is<float>() || !doc["longitude"].is<float>()) {
    issData.valid = false;
    issData.lastError = "ISS sin coordenadas";
    return false;
  }

  issData.latitude = doc["latitude"].as<float>();
  issData.longitude = doc["longitude"].as<float>();
  issData.altitudeKm = doc["altitude"].is<float>() ? doc["altitude"].as<float>() : 0.0f;
  issData.velocityKmh = doc["velocity"].is<float>() ? doc["velocity"].as<float>() : 0.0f;
  issData.timestamp = doc["timestamp"].is<unsigned long>() ? doc["timestamp"].as<unsigned long>() : 0UL;
  issData.valid = true;
  issData.lastError = "OK";
  issLastFetch = millis();

  addIssTrailPoint(issData.latitude, issData.longitude);
  return true;
}

void startIssOrbitFetch() {
  if (!wifiConnected) return;
  unsigned long centerTs = issData.valid && issData.timestamp > 0 ? issData.timestamp : (unsigned long)time(nullptr);
  if (centerTs < 100000) return;

  issOrbitValid = false;
  issOrbitCount = 0;
  issOrbitFetchWritten = 0;
  issOrbitFetchIndex = 0;
  issOrbitFetchStartTs = centerTs - ISS_ORBIT_WINDOW_SEC;
  issOrbitFetchInProgress = true;
  issOrbitLastError = "";
  // Let ISS page render first, then begin background chunk fetching.
  issOrbitNextReqAt = millis() + 300;
}

void processIssOrbitFetch() {
  if (!issOrbitFetchInProgress) return;
  if (millis() < issOrbitNextReqAt) return;
  // If user already left ISS page, stop any pending orbit work.
  if (currentPage != 12) {
    issOrbitFetchInProgress = false;
    return;
  }
  // Allow page change to interrupt orbit calculation immediately.
  if (digitalRead(BTN_MIDDLE) == LOW) {
    lastButtonPress = millis();
    currentPage = (currentPage + 1) % totalPages;
    issOrbitFetchInProgress = false;
    return;
  }
  if (!wifiConnected) {
    issOrbitFetchInProgress = false;
    issOrbitLastError = "Sin WiFi";
    return;
  }

  const int totalPts = ISS_ORBIT_MAX;
  int chunk = min(ISS_POSITIONS_MAX_PER_REQ, totalPts - issOrbitFetchIndex);
  if (chunk <= 0) {
    issOrbitFetchInProgress = false;
    issOrbitCount = issOrbitFetchWritten;
    issOrbitValid = (issOrbitCount > 1);
    issOrbitLastFetch = millis();
    return;
  }

  String url = "https://api.wheretheiss.at/v1/satellites/25544/positions?timestamps=";
  for (int j = 0; j < chunk; j++) {
    unsigned long ts = issOrbitFetchStartTs + (unsigned long)(issOrbitFetchIndex + j) * ISS_ORBIT_STEP_SEC;
    if (j > 0) url += ",";
    url += String(ts);
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, url)) {
    issOrbitFetchInProgress = false;
    issOrbitLastError = "Error inicio HTTPS orbita";
    issOrbitLastFetch = millis();
    return;
  }
  // Keep requests short so UI remains responsive while orbit is calculated.
  http.setTimeout(1200);
  int code = http.GET();
  if (code != 200) {
    http.end();
    issOrbitFetchInProgress = false;
    issOrbitLastError = (code < 0) ? "Error red orbita" : ("HTTP orbita " + String(code));
    issOrbitLastFetch = millis();
    return;
  }
  String payload = http.getString();
  http.end();
  if (currentPage != 12 || digitalRead(BTN_MIDDLE) == LOW) {
    if (digitalRead(BTN_MIDDLE) == LOW) {
      lastButtonPress = millis();
      currentPage = (currentPage + 1) % totalPages;
    }
    issOrbitFetchInProgress = false;
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) {
    issOrbitFetchInProgress = false;
    issOrbitLastError = "JSON orbita invalido";
    issOrbitLastFetch = millis();
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull()) {
    issOrbitFetchInProgress = false;
    issOrbitLastError = "Orbita sin datos";
    issOrbitLastFetch = millis();
    return;
  }

  for (JsonVariant v : arr) {
    if (issOrbitFetchWritten >= ISS_ORBIT_MAX) break;
    JsonObject o = v.as<JsonObject>();
    if (!o["latitude"].is<float>() || !o["longitude"].is<float>()) continue;
    issOrbitLat[issOrbitFetchWritten] = o["latitude"].as<float>();
    issOrbitLon[issOrbitFetchWritten] = o["longitude"].as<float>();
    issOrbitTs[issOrbitFetchWritten] = o["timestamp"].is<unsigned long>() ? o["timestamp"].as<unsigned long>() : 0UL;
    issOrbitFetchWritten++;
  }

  issOrbitFetchIndex += chunk;
  if (issOrbitFetchIndex >= totalPts) {
    issOrbitFetchInProgress = false;
    issOrbitCount = issOrbitFetchWritten;
    issOrbitValid = (issOrbitCount > 1);
    issOrbitLastFetch = millis();
    return;
  }

  // Keep within API rate limit (~1 req/s) while remaining responsive.
  issOrbitNextReqAt = millis() + 1100;
}

int kumaFindOrAddMonitor(int id, const String& name) {
  for (int i = 0; i < kumaData.monitorCount; i++) {
    if (kumaData.monitors[i].id == id) {
      if (name.length() > 0) kumaData.monitors[i].name = name;
      return i;
    }
  }
  if (kumaData.monitorCount >= KUMA_MAX_MONITORS) return -1;
  int idx = kumaData.monitorCount++;
  kumaData.monitors[idx].id = id;
  kumaData.monitors[idx].name = name;
  kumaData.monitors[idx].latestStatus = -1;
  kumaData.monitors[idx].latestPingMs = -1;
  kumaData.monitors[idx].latestTime = "";
  kumaData.monitors[idx].hasHeartbeat = false;
  for (int b = 0; b < KUMA_BEANS; b++) kumaData.monitors[idx].beans[b] = 0;
  return idx;
}

bool fetchKumaDashboardData() {
  if (!wifiConnected) return false;

  HTTPClient http;
  String statusUrl = String(kKumaBaseUrl) + "/api/status-page/" + String(kKumaSlug);
  String hbUrl = String(kKumaBaseUrl) + "/api/status-page/heartbeat/" + String(kKumaSlug);

  http.begin(statusUrl);
  http.setTimeout(10000);
  int sc = http.GET();
  if (sc != 200) { http.end(); return false; }
  String statusPayload = http.getString();
  http.end();

  http.begin(hbUrl);
  http.setTimeout(10000);
  int hc = http.GET();
  if (hc != 200) { http.end(); return false; }
  String hbPayload = http.getString();
  http.end();

  DynamicJsonDocument sdoc(65536);
  DynamicJsonDocument hdoc(98304);
  if (deserializeJson(sdoc, statusPayload) != DeserializationError::Ok) return false;
  if (deserializeJson(hdoc, hbPayload) != DeserializationError::Ok) return false;

  kumaData.groupCount = 0;
  kumaData.monitorCount = 0;
  kumaData.valid = false;

  JsonArray groups = sdoc["publicGroupList"].as<JsonArray>();
  if (groups.isNull()) groups = sdoc["groupList"].as<JsonArray>();
  if (groups.isNull()) groups = sdoc["groups"].as<JsonArray>();
  if (groups.isNull()) return false;

  for (JsonVariant gVar : groups) {
    if (kumaData.groupCount >= KUMA_MAX_GROUPS) break;
    JsonObject gObj = gVar.as<JsonObject>();
    KumaGroup& g = kumaData.groups[kumaData.groupCount];
    g.name = String((const char*)(gObj["name"] | "Grupo"));
    g.monitorCount = 0;

    JsonArray mons = gObj["monitorList"].as<JsonArray>();
    if (mons.isNull()) mons = gObj["monitors"].as<JsonArray>();
    if (!mons.isNull()) {
      for (JsonVariant mVar : mons) {
        if (g.monitorCount >= KUMA_MAX_GROUP_MONS) break;
        JsonObject mObj = mVar.as<JsonObject>();
        int id = mObj["id"] | -1;
        const char* name = mObj["name"] | "Monitor";
        if (id < 0) continue;
        int midx = kumaFindOrAddMonitor(id, String(name));
        if (midx < 0) continue;
        g.monitorIdx[g.monitorCount++] = midx;
      }
    }
    if (g.monitorCount > 0) kumaData.groupCount++;
  }

  JsonObject hbRoot = hdoc.as<JsonObject>();
  JsonObject hbList = hdoc["heartbeatList"].as<JsonObject>();
  for (int i = 0; i < kumaData.monitorCount; i++) {
    KumaMonitor& m = kumaData.monitors[i];
    String key = String(m.id);
    JsonArray arr = hbRoot[key].as<JsonArray>();
    if (arr.isNull()) arr = hbList[key].as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) {
      m.latestStatus = -1;
      m.latestPingMs = -1;
      m.latestTime = "";
      m.hasHeartbeat = false;
      for (int b = 0; b < KUMA_BEANS; b++) m.beans[b] = 0;
      continue;
    }

    m.hasHeartbeat = true;
    for (int b = 0; b < KUMA_BEANS; b++) m.beans[b] = 0;
    int arrSize = (int)arr.size();
    int count = min(arrSize, KUMA_BEANS);
    for (int j = 0; j < count; j++) {
      int src = arrSize - 1 - j;               // newest at end
      JsonObject hb = arr[src].as<JsonObject>();
      int st = hb["status"] | -1;
      int target = KUMA_BEANS - 1 - j;         // newest at right ("ahora")
      m.beans[target] = (st == 1) ? 1 : 0;
      if (j == 0) {
        m.latestStatus = st;
        m.latestPingMs = hb["ping"] | (hb["pingMs"] | -1);
        const char* t = hb["time"] | "";
        m.latestTime = String(t);
      }
    }
  }

  kumaData.valid = (kumaData.groupCount > 0 && kumaData.monitorCount > 0);
  kumaData.lastUpdate = millis();
  kumaLastFetch = millis();
  return kumaData.valid;
}

void syncRTCWithNTP() {
  if (!wifiConnected) return;
  // Use POSIX TZ string â€” ESP32 handles DST transitions automatically.
  // Pass 0,0 for gmtOffset/daylightOffset; the TZ string contains all the rules.
  configTime(0, 0, ntpServer);
  applyConfiguredTimeZone();

  struct tm timeinfo; int retries = 0;
  while (!getLocalTime(&timeinfo) && retries < 10) { delay(500); retries++; }
  if (getLocalTime(&timeinfo)) {
    // Push correct local time (already DST-adjusted by the TZ rule) to RTC
    rtc.setTime(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    rtc.setDate(timeinfo.tm_wday, timeinfo.tm_mday, timeinfo.tm_mon+1, timeinfo.tm_year+1900);
    ntpLastSync = millis();

    // Derive current UTC offset by comparing local epoch to UTC directly.
    // Use gmtime_r to get UTC fields, then compute the difference in minutes
    // by comparing hours/minutes â€” avoids mktime() double-conversion issue on newlib.
    time_t localEpoch = time(nullptr);
    struct tm utcCheck;
    gmtime_r(&localEpoch, &utcCheck);
    int localMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    int utcMinutes   = utcCheck.tm_hour  * 60 + utcCheck.tm_min;
    int diffMins     = localMinutes - utcMinutes;
    // Handle day boundary wrap
    if (diffMins >  720) diffMins -= 1440;
    if (diffMins < -720) diffMins += 1440;
    gmtOffset_sec = (long)(diffMins * 60);

    Serial.print("RTC synced! UTC offset: ");
    Serial.print(gmtOffset_sec / 3600);
    Serial.print("h, DST: ");
    Serial.println(timeinfo.tm_isdst ? "SI" : "NO");
  } else Serial.println("NTP fallo");
}

void handleButtons() {
  unsigned long now = millis();
  static int lastLeftState = HIGH;
  static int lastMiddleState = HIGH;
  static unsigned long lastLeftEdgeMs = 0;
  static unsigned long lastMiddleEdgeMs = 0;

  int leftState = digitalRead(BTN_LEFT);
  int middleState = digitalRead(BTN_MIDDLE);

  // GPIO18 (KEY) is the primary page-switch button on this board.
  if (middleState != lastMiddleState && (now - lastMiddleEdgeMs) >= debounceDelay) {
    lastMiddleEdgeMs = now;
    lastMiddleState = middleState;
    if (middleState == LOW) {
      lastButtonPress = now;
      if (batterySaveMode) {
        batterySaveMode = false;
        applyPowerProfile();
        saveRuntimeConfig();
        return;
      }
      currentPage = (currentPage + 1) % totalPages;
    }
  }

  // Keep GPIO0 as secondary action: manual weather refresh.
  if (leftState != lastLeftState && (now - lastLeftEdgeMs) >= debounceDelay) {
    lastLeftEdgeMs = now;
    lastLeftState = leftState;
    if (leftState == LOW) {
      lastButtonPress = now;
      weatherRefreshRequested = true;
    }
  }
}

bool isWeatherPage(int page) {
  return page >= 4 && page <= 7;
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200); delay(200);
  Serial.println("\n=== ESP32-S3 RLCD ===");
  loadRuntimeConfig();
  applyPowerProfile();
  mqttEnabled = false;
  mqttConnected = false;
  mqttStatusText = "No disponible";

  pinMode(BTN_LEFT,   INPUT_PULLUP);
  pinMode(BTN_MIDDLE, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);

  Wire.begin(13, 14);
  initBootAudio();
  // Radio engine now uses ESP8266Audio and creates I2S output on demand.
  radioEngineReady = true;
  applyRadioVolume();

  RlcdPort.RLCD_Init();
  rtc.begin();
  loadTimeZoneConfig();

  // ===== BOOT SCREEN =====
  canvas.fillScreen(0);
  canvas.drawRect(0, 0, W, H, 1);
  canvas.drawRect(1, 1, W-2, H-2, 1);
  canvas.setTextColor(1);

  // Build combined "Sydney AEDT" string â€” NTP not synced yet at this point,
  // so show the configured POSIX TZ standard name as a placeholder
  // getTZLabel() will return correct DST-aware label once NTP syncs
  char locTzBuf[32];
  snprintf(locTzBuf, sizeof(locTzBuf), "%s %s", getDisplayLocation().c_str(), getTZLabel());

  printCentered(&FONT_MEDIUM, 36,  "ESP32-S3 RLCD");
  printCentered(&FONT_MEDIUM, 58,  "Panel del Clima");
  printCentered(&FONT_SMALL,  78,  "by John Willie Gee");
  printCentered(&FONT_MEDIUM, 108, locTzBuf);

  pushCanvasToRLCD(false);
  delay(600);

  // ===== BOOT STATUS LINES =====
  int bootLine = 0;
  bool bootAllOk = true;
  auto drawBoot = [&](const char* msg, int state) {
    int ly = 136 + bootLine * 24;
    canvas.fillRect(14, ly-14, 372, 18, 0);
    canvas.setFont(&FONT_SMALL); canvas.setTextColor(1);
    canvas.setCursor(16, ly); canvas.print(msg);
    if      (state ==  1) { canvas.setCursor(358, ly); canvas.print("OK"); }
    else if (state == -1) { canvas.setCursor(358, ly); canvas.print("--"); }
    pushCanvasToRLCD(false);
    if (state == 1) bootBeepOk();
    else if (state == -1) {
      bootAllOk = false;
      bootBeepFail();
    }
    bootLine++;
  };

  weatherData.valid = false; weatherData.lastUpdate = 0;
  astroData.valid   = false; hourlyData.valid        = false;

  drawBoot("Conectando WiFi...", 0); connectWiFi(); bootLine--;
  drawBoot(wifiConnected ? "WiFi conectado" : "WiFi fallo", wifiConnected ? 1 : -1);

  if (wifiConnected) {
    startConfigServer();
    showConnectedNoDataScreen();
    delay(1600);

    drawBoot("Sincronizando RTC...", 0); syncRTCWithNTP(); bootLine--;
    drawBoot("RTC sincronizado", 1);
    drawBoot("Detectando ciudad...", 0); cityLastAttempt = millis(); bool cityOk = fetchCityFromExternalIP(); bootLine--;
    drawBoot(cityOk ? "Ciudad detectada" : "Ciudad no disponible", cityOk ? 1 : -1);
    drawBoot("Descargando clima...", 0); fetchWeatherData(); bootLine--;
    drawBoot(weatherData.valid ? "Clima cargado" : "Clima fallo", weatherData.valid ? 1 : -1);
    drawBoot("Descargando KUMA...", 0); fetchKumaDashboardData(); bootLine--;
    drawBoot(kumaData.valid ? "KUMA cargado" : "KUMA no disponible", kumaData.valid ? 1 : -1);
    drawBoot("Consultando ISS...", 0); bool issOk = fetchIssData(); bootLine--;
    drawBoot(issOk ? "ISS cargada" : "ISS no disponible", issOk ? 1 : -1);
  }

  if (shtc3_read(temperature, humidity)) { sensorReadCount++; }
  else { sensorFailCount++; }
  batteryVoltage = readBatteryVoltage();

  if (bootAllOk) bootR2D2AllOk();

  delay(1800);

  history.initialized  = false;
  history.currentIndex = 0;
  history.sampleCount  = 0;
  history.lastLogTime  = millis();
  for (int i = 0; i < HISTORY_SIZE; i++) {
    history.tempHistory[i]     = temperature;
    history.humidityHistory[i] = humidity;
  }

  Serial.println("Listo!");
  delay(500);
}

// ===== LOOP =====
void loop() {
  if (otaInProgress) {
    // Freeze normal app activity during OTA for maximum stability.
    if (radioPlaying || radioStartRequested) {
      radioUserStopped = true;
      radioStartRequested = false;
      stopRadioPlayback();
      restoreBeepAudioEngine();
    }
    if (wifiConnected) configServer.handleClient();
    drawOtaProgressScreen();
    delay(20);
    return;
  }

  handleButtons();
  if (batterySaveMode && currentPage != 0) currentPage = 0;
  bool pageJustChanged = false;
  if (currentPage != previousPage) {
    // Recalculate orbit only when entering ISS page.
    if (currentPage == 12) {
      startIssOrbitFetch();
      issRefreshRequested = true;
    }
    if (currentPage == 2) photoRefreshRequested = true;
    if (currentPage == 1) kumaRefreshRequested = true;
    if (isWeatherPage(currentPage)) weatherRefreshRequested = true;
    if (previousPage == 12 && currentPage != 12) issOrbitFetchInProgress = false;
    if (previousPage == 13 && currentPage != 13) {
      // Leaving radio page: stop streaming completely and restore beep audio engine.
      radioUserStopped = true;
      radioStartRequested = false;
      stopRadioPlayback();
      restoreBeepAudioEngine();
    }
    previousPage = currentPage;
    pageJustChanged = true;
  }
  // Read all RTC values in one pass to minimise I2C transactions
  hour24    = rtc.getHour();
  minuteVal = rtc.getMinute();
  // secondVal needed for dashboard (page 0) and analogue clock (page 1) second hands
  if (currentPage == 0 || currentPage == 1 || currentPage == 2) secondVal = rtc.getSecond();

  // Always render immediately after page switch; network updates run in next loops.
  if (pageJustChanged) {
    draw();
    delay(16);
    return;
  }

  if (sensorUpdateCounter == 0) {
    batteryVoltage = readBatteryVoltage();
    updateBatteryChargingState(batteryVoltage, millis());
    if (shtc3_read(temperature, humidity)) {
      sensorReadCount++;
      Serial.printf("T=%.1fC RH=%.1f%% V=%.2f", temperature, humidity, batteryVoltage);
    } else { sensorFailCount++; Serial.print("Fallo lectura sensor"); }
    if (wifiConnected) { wifiRSSI = WiFi.RSSI(); Serial.printf(" WiFi=%d", wifiRSSI); }
    Serial.println();
  }
  if (++sensorUpdateCounter >= 600) sensorUpdateCounter = 0;

  unsigned long now = millis();
  if (now - history.lastLogTime >= 900000UL) {
    history.tempHistory[history.currentIndex]     = temperature;
    history.humidityHistory[history.currentIndex] = humidity;
    history.currentIndex = (history.currentIndex + 1) % HISTORY_SIZE;
    history.lastLogTime  = now;
    if (history.sampleCount < HISTORY_SIZE) history.sampleCount++;
    history.initialized = (history.sampleCount >= HISTORY_SIZE);
  }

  bool weatherPageVisible = (!batterySaveMode && isWeatherPage(currentPage));
  bool kumaPageVisible = (!batterySaveMode && currentPage == 1);
  bool photoPageVisible = (!batterySaveMode && currentPage == 2);
  bool issPageVisible = (!batterySaveMode && currentPage == 12);
  bool radioPageVisible = (!batterySaveMode && currentPage == 13);

  // Safety: outside Radio screen, keep stream engine fully stopped so system beeps always work.
  if (!radioPageVisible && (radioPlaying || radioStartRequested)) {
    radioUserStopped = true;
    radioStartRequested = false;
    stopRadioPlayback();
    restoreBeepAudioEngine();
  }

  if (wifiConnected && weatherPageVisible &&
      (weatherRefreshRequested || weatherData.lastUpdate == 0 || (now - weatherData.lastUpdate) > 1800000UL)) {
    fetchWeatherData();
    weatherRefreshRequested = false;
  }

  if (wifiConnected && kumaPageVisible &&
      (kumaRefreshRequested || kumaLastFetch == 0 || (now - kumaLastFetch) > 60000UL)) {
    fetchKumaDashboardData();
    kumaRefreshRequested = false;
  }

  if (wifiConnected && photoPageVisible &&
      !photoUseUploaded &&
      (photoRefreshRequested || photoLastFetch == 0 || (now - photoLastFetch) > ((unsigned long)photoRefreshMinutes * 60000UL) ||
       (!photoValid && (now - photoLastFetch) > 15000UL))) {
    fetchPhotoBackground();
    photoRefreshRequested = false;
  }

  if (wifiConnected && issPageVisible &&
      (issRefreshRequested || issLastFetch == 0 || (now - issLastFetch) > 5000UL)) {
    bool issOkNow = fetchIssData();
    if (issOkNow) maybeIssPassBeep(now);
    issRefreshRequested = false;
  }

  if (wifiConnected && issPageVisible) {
    if (!issOrbitFetchInProgress && (issOrbitLastFetch == 0 || (now - issOrbitLastFetch) > 600000UL)) {
      startIssOrbitFetch();
    }
    processIssOrbitFetch();
  }

  // Start radio only from deferred request to avoid blocking HTTP handlers.
  if (radioPageVisible && radioStartRequested && !radioUserStopped && !radioPlaying && !radioMuted && wifiConnected && radioStationUrl.length() > 0) {
    if (now - radioLastAutoStartAttemptMs > 1500UL) {
      radioLastAutoStartAttemptMs = now;
      radioFallbackTried = false;
      radioStatus = "Conectando...";
      String preWhy;
      if (preflightRadioUrl(radioStationUrl, preWhy)) {
        startRadioPlayback();
      } else {
        radioStatus = "Stream invalido: " + preWhy;
        radioUserStopped = true;
        radioStartRequested = false;
      }
      radioStartRequested = false;
    }
  }
  if (radioPlaying && radioStatus == "Conectando...") {
    unsigned long now2 = millis();
    if (now2 - radioConnectStartMs > 15000UL || now2 - radioLastEventMs > 15000UL) {
      stopRadioPlayback();
      radioStatus = "Timeout de conexion";
      radioUserStopped = true;
      radioStartRequested = false;
    }
  }

  if (wifiConnected && (weatherPageVisible || issPageVisible) &&
      detectedCity.length() == 0 && (cityLastAttempt == 0 || (now - cityLastAttempt) > 600000UL)) {
    cityLastAttempt = now;
    fetchCityFromExternalIP();
  }

  // Re-sync RTC with NTP every 24 hours
  unsigned long ntpInterval = batterySaveMode ? 3600000UL : 86400000UL;
  if (wifiConnected && (ntpLastSync == 0 || (now - ntpLastSync) > ntpInterval))
    syncRTCWithNTP();

  if (wifiConnected) configServer.handleClient();
  maybeBatteryCriticalBeep(now);

  // On radio page prioritize streaming loop over display refresh to avoid starving decoder/network.
  if (radioPageVisible) {
    unsigned long now3 = millis();
    // While playing, redraw less often to leave more CPU/network budget to the decoder.
    unsigned long redrawMs = (radioStatus == "Reproduciendo") ? 1200UL : 500UL;
    if (now3 - radioLastScreenDrawMs > redrawMs || radioStatus == "Conectando..." || radioStatus == "Timeout de conexion") {
      draw();
      radioLastScreenDrawMs = now3;
    }
    serviceRadioPlayback();
    delay(1);
    return;
  }

  draw();
  delay(16);
}

