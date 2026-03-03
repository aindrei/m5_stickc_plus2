/*
  M5StickC Plus2 Weather Station (ZIP 91361)
  - Updates every 5 minutes from public APIs (no key):
      1) Zip -> lat/lon:  http://api.zippopotam.us/us/91361
      2) Forecast:        https://api.open-meteo.com/v1/forecast

  Shows:
    - Today + next 3 days
    - Icon (sunny / partly cloudy / cloudy / rainy)
    - Max/Min temp
    - Wind (max wind + direction)

  Libraries to install (Arduino Library Manager):
    - M5StickCPlus2 (brings M5Unified)
    - ArduinoJson

  Notes:
    - Based on M5StickC Plus2 library usage (StickCP2.begin / StickCP2.Display) per M5 docs. :contentReference[oaicite:0]{index=0}
    - Weather codes follow Open-Meteo WMO weather interpretation codes. :contentReference[oaicite:1]{index=1}
*/

#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <time.h>

#define DEBUG_SERIAL 1
#if DEBUG_SERIAL
  #define DBG(x)   Serial.print(x)
  #define DBGLN(x) Serial.println(x)
#else
  #define DBG(x)
  #define DBGLN(x)
#endif

// ====== WIFI ======
const char* WIFI_SSID = "decoaxax_Guest";
const char* WIFI_PASS = "Dguesttherion12";

// ====== LOCATION ======
static const char* ZIP_CODE = "91361";
static const char* COUNTRY  = "us";

// ====== UPDATE INTERVAL ======
static const uint32_t UPDATE_EVERY_MS = 5UL * 60UL * 1000UL;

// ====== TIME ======
static const char* NTP_1 = "pool.ntp.org";
static const char* NTP_2 = "time.nist.gov";
// America/Los_Angeles (zip 91361); adjust if you move locations.
static const char* TZ_INFO = "PST8PDT,M3.2.0/2,M11.1.0/2";

// ====== UI ======
static const int ROTATION = 0; // portrait
static const int W = 135;
static const int H = 240;

// ====== DATA MODEL ======
struct DayForecast {
  char date[11];          // "YYYY-MM-DD"
  int weatherCode = 0;    // WMO weather code
  float tMaxC = NAN;
  float tMinC = NAN;
  float windMaxKph = NAN;
  float windDirDeg = NAN;
};

struct WeatherState {
  bool ok = false;
  float currentTempC = NAN;
  float currentWindKph = NAN;
  float currentWindDirDeg = NAN;
  int currentWeatherCode = 0;
  DayForecast days[4]; // today + next 3
  uint32_t lastFetchMs = 0;
  char placeName[48] = "91361";
} gWeather;

// --- Weather code to 4 icon buckets ---
// Open-Meteo weather_code uses WMO interpretation codes. :contentReference[oaicite:2]{index=2}
enum IconKind { ICON_SUNNY, ICON_PARTLY, ICON_CLOUDY, ICON_RAINY };

// ====== HELPERS ======
static String httpGET(const String& url) {
  HTTPClient http;
  http.setTimeout(12000);
  http.begin(url);
  int code = http.GET();
  if (code <= 0) {
    http.end();
    return String();
  }
  String payload = http.getString();
  http.end();
  if (code != 200) return String();
  return payload;
}

static bool getLatLonForZip(float &lat, float &lon, char* placeOut, size_t placeOutLen) {
  // Zippopotam: http://api.zippopotam.us/us/91361
  String url = String("http://api.zippopotam.us/") + COUNTRY + "/" + ZIP_CODE;
  DBGLN(url);
  String json = httpGET(url);
  if (!json.length()) return false;
  DBGLN("Response: " + json);

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return false;
  DBGLN("JSON parsed successfully");

  // places[0].latitude / longitude are strings
  /*const char* latStr = doc["places"][0]["latitude"]  | nullptr;
  const char* lonStr = doc["places"][0]["longitude"] | nullptr;
  const char* place  = doc["places"][0]["place name"] | nullptr;
  const char* state  = doc["places"][0]["state abbreviation"] | nullptr;*/
  JsonArray places = doc["places"].as<JsonArray>();
  if (places.isNull() || places.size() == 0) {
    DBGLN("places array missing");
    return false;
  }

  JsonObject p0 = places[0];
  const char* latStr = p0["latitude"].as<const char*>();
  const char* lonStr = p0["longitude"].as<const char*>();
  const char* place  = p0["place name"].as<const char*>();
  const char* state  = p0["state abbreviation"].as<const char*>();

  DBGLN(String("Parsed lat/lon: ") + (latStr ? latStr : "null") + ", " + (lonStr ? lonStr : "null"));

  if (!latStr || !lonStr) return false;
  DBGLN("Converting lat/lon to float...");
  lat = String(latStr).toFloat();
  lon = String(lonStr).toFloat();

  if (placeOut && placeOutLen) {
    if (place && state) {
      snprintf(placeOut, placeOutLen, "%s, %s", place, state);
    } else if (place) {
      snprintf(placeOut, placeOutLen, "%s", place);
    } else {
      snprintf(placeOut, placeOutLen, "%s", ZIP_CODE);
    }
  }
  return true;
}

static bool fetchOpenMeteo(float lat, float lon, WeatherState &out) {
  // Open-Meteo: daily weathercode + temp + wind, plus current (no key).
  // We request windspeed_10m/winddirection_10m for current and daily wind maxima & direction.
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(lat, 6) +
               "&longitude=" + String(lon, 6) +
               "&current=temperature_2m,windspeed_10m,winddirection_10m,weather_code" +
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,windspeed_10m_max,winddirection_10m_dominant" +
               "&temperature_unit=celsius&windspeed_unit=kmh&timezone=auto&forecast_days=4";
  DBGLN("Fetching weather data...");
  DBGLN(url);

  String json = httpGET(url);
  if (!json.length()) return false;
  DBGLN("Weather data fetched, parsing JSON...");

  DynamicJsonDocument doc(24 * 1024);
  DeserializationError err = deserializeJson(doc, json);
  if (err) return false;
  DBGLN("JSON parsed successfully");

  out.ok = true;
  out.currentTempC    = doc["current"]["temperature_2m"] | NAN;
  out.currentWindKph  = doc["current"]["windspeed_10m"] | NAN;
  out.currentWindDirDeg = doc["current"]["winddirection_10m"] | NAN;
  out.currentWeatherCode = doc["current"]["weather_code"] | 0;

  JsonArray times = doc["daily"]["time"].as<JsonArray>();
  JsonArray wc    = doc["daily"]["weather_code"].as<JsonArray>();
  JsonArray tmax  = doc["daily"]["temperature_2m_max"].as<JsonArray>();
  JsonArray tmin  = doc["daily"]["temperature_2m_min"].as<JsonArray>();
  JsonArray wmax  = doc["daily"]["windspeed_10m_max"].as<JsonArray>();
  JsonArray wdir  = doc["daily"]["winddirection_10m_dominant"].as<JsonArray>();

  if (times.size() < 4) return false;
  DBGLN("Parsing daily forecast data...");

  for (int i = 0; i < 4; i++) {
    const char* d = times[i] | "";
    strncpy(out.days[i].date, d, sizeof(out.days[i].date));
    out.days[i].date[10] = 0;

    out.days[i].weatherCode = wc[i] | 0;
    out.days[i].tMaxC = tmax[i] | NAN;
    out.days[i].tMinC = tmin[i] | NAN;
    out.days[i].windMaxKph = wmax[i] | NAN;
    out.days[i].windDirDeg = wdir[i] | NAN;
  }

  out.lastFetchMs = millis();
  return true;
}

// --- Date -> Day-of-week (0=Sun..6=Sat) using Sakamoto algorithm ---
static int dowFromYYYYMMDD(const char* yyyymmdd) {
  int y = atoi(String(yyyymmdd).substring(0, 4).c_str());
  int m = atoi(String(yyyymmdd).substring(5, 7).c_str());
  int d = atoi(String(yyyymmdd).substring(8,10).c_str());
  static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  y -= (m < 3);
  return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

static const char* dowLabel(int dow) {
  static const char* L[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  if (dow < 0 || dow > 6) return "---";
  return L[dow];
}

static IconKind iconFromWmo(int code) {
  if (code == 0) return ICON_SUNNY;
  if (code == 1 || code == 2) return ICON_PARTLY;
  if (code == 3 || code == 45 || code == 48) return ICON_CLOUDY;

  // drizzle / rain / showers / thunder -> rainy
  if ((code >= 51 && code <= 57) || (code >= 61 && code <= 67) || (code >= 80 && code <= 82) || (code >= 95 && code <= 99)) {
    return ICON_RAINY;
  }

  // snow also treat as rainy bucket for this simple UI
  if (code >= 71 && code <= 77) return ICON_RAINY;

  return ICON_CLOUDY;
}

// ====== DRAWING ======
static void drawSun(int cx, int cy, int r) {
  auto &d = StickCP2.Display;
  d.fillCircle(cx, cy, r, TFT_YELLOW);
  for (int i = 0; i < 8; i++) {
    float a = (float)i * 3.1415926f / 4.0f;
    int x1 = cx + (int)((r + 3) * cosf(a));
    int y1 = cy + (int)((r + 3) * sinf(a));
    int x2 = cx + (int)((r + 10) * cosf(a));
    int y2 = cy + (int)((r + 10) * sinf(a));
    d.drawLine(x1, y1, x2, y2, TFT_YELLOW);
  }
}

static void drawCloud(int x, int y, int w, int h, uint16_t color) {
  auto &d = StickCP2.Display;
  int r1 = h/2;
  d.fillCircle(x + r1, y + r1, r1, color);
  d.fillCircle(x + w/2, y + r1 - 2, r1 + 1, color);
  d.fillCircle(x + w - r1, y + r1, r1, color);
  d.fillRoundRect(x, y + r1, w, h - r1, 6, color);
}

static void drawRain(int x, int y, int w, int h) {
  auto &d = StickCP2.Display;
  // cloud
  drawCloud(x, y, w, h, TFT_LIGHTGREY);
  // drops
  int baseY = y + h + 2;
  for (int i = 0; i < 4; i++) {
    int dx = x + 8 + i * (w - 16) / 3;
    d.drawLine(dx, baseY, dx - 2, baseY + 8, TFT_CYAN);
  }
}

static void drawIcon(IconKind k, int x, int y) {
  auto &d = StickCP2.Display;
  // icon box about 36x28
  if (k == ICON_SUNNY) {
    drawSun(x + 18, y + 14, 7);
  } else if (k == ICON_PARTLY) {
    drawSun(x + 14, y + 12, 6);
    drawCloud(x + 10, y + 12, 26, 16, TFT_LIGHTGREY);
  } else if (k == ICON_CLOUDY) {
    drawCloud(x + 8, y + 10, 28, 18, TFT_LIGHTGREY);
  } else {
    drawRain(x + 7, y + 8, 30, 18);
  }
}

static void drawWindArrow(int cx, int cy, float dirDeg, uint16_t color) {
  auto &d = StickCP2.Display;
  float a = (dirDeg - 90.0f) * 3.1415926f / 180.0f; // rotate so 0° points up
  int x2 = cx + (int)(10 * cosf(a));
  int y2 = cy + (int)(10 * sinf(a));
  d.drawLine(cx, cy, x2, y2, color);
  // little arrowhead
  float a1 = a + 2.6f;
  float a2 = a - 2.6f;
  int xh1 = x2 + (int)(4 * cosf(a1));
  int yh1 = y2 + (int)(4 * sinf(a1));
  int xh2 = x2 + (int)(4 * cosf(a2));
  int yh2 = y2 + (int)(4 * sinf(a2));
  d.drawLine(x2, y2, xh1, yh1, color);
  d.drawLine(x2, y2, xh2, yh2, color);
}


static void drawWindGlyph(int x, int y, uint16_t color) {
  // Simple "wind" glyph: three lines
  auto &d = StickCP2.Display;
  d.drawLine(x, y, x + 12, y, color);
  d.drawLine(x + 3, y + 4, x + 15, y + 4, color);
  d.drawLine(x, y + 8, x + 10, y + 8, color);
  // tiny accents
  d.drawPixel(x + 12, y - 1, color);
  d.drawPixel(x + 15, y + 3, color);
}


static void drawCurrentDay(const WeatherState& ws) {
  // Top area styled like the reference (icon + big temp, then hi/lo + wind)
  auto &d = StickCP2.Display;

  const int topH = 116;
  d.fillRect(0, 0, W, topH, TFT_BLACK);

  // Current condition icon (left)
  drawIcon(iconFromWmo(ws.currentWeatherCode), 10, 18);

  // Big temperature (right) - increased size to 6
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setTextSize(5);
  int tempX = 50;
  int tempY = 12;
  d.setCursor(tempX, tempY);
  if (!isnan(ws.currentTempC)) {
    d.printf("%.0f", ws.currentTempC);
  } else {
    d.print("--");
  }
  d.setTextSize(3);
  d.print("o"); // degree symbol using 'o' which is more reliable on bitmap fonts

  // Small line with hi / lo / wind
  const DayForecast& today = ws.days[0];
  int yInfo = 78;
  d.setTextSize(2);

  // High (red)
  d.setTextColor(TFT_RED, TFT_BLACK);
  d.setCursor(8, yInfo);
  if (!isnan(today.tMaxC)) d.printf("%.0f", today.tMaxC);
  else d.print("--o");

  // Divider slash
  d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  d.setCursor(36, yInfo);
  d.print("/");

  // Low (cyan)
  d.setTextColor(TFT_CYAN, TFT_BLACK);
  d.setCursor(50, yInfo);
  if (!isnan(today.tMinC)) d.printf("%.0f", today.tMinC);
  else d.print("--");

  // Wind glyph + speed
  int windX = 82;
  drawWindGlyph(windX, yInfo + 2, TFT_LIGHTGREY);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(windX + 18, yInfo);
  if (!isnan(ws.currentWindKph)) d.printf("%.0f", ws.currentWindKph);
  else d.print("--");

  d.drawFastHLine(0, topH, W, TFT_DARKGREY);
}

static void drawForecastRow(int row, const DayForecast& df) {
  // Row "pill" styled like the reference image
  auto &d = StickCP2.Display;

  const int topH = 116;
  const int rowH = 34;
  const int gap  = 4;

  int y = topH + gap + row * (rowH + gap);

  // Clear band
  d.fillRect(0, y, W, rowH, TFT_BLACK);

  // Rounded card
  int cardX = 8;
  int cardW = W - 16;
  //uint16_t cardBg = 0x2104; // very dark grey
  //d.fillRoundRect(cardX, y, cardW, rowH, 10, cardBg);

  // Day label (uppercase)
  int dow = dowFromYYYYMMDD(df.date);
  const char* label = dowLabel(dow);
  char up[4] = {0};
  up[0] = toupper(label[0]);
  up[1] = toupper(label[1]);
  up[2] = toupper(label[2]);

  d.setTextSize(2);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(cardX, y + 10);
  d.print(up);

  // Icon
  drawIcon(iconFromWmo(df.weatherCode), cardX + 38, y);

  // Temps on right: high then low
  int xHigh = cardX + cardW - 38;
  int xLow  = cardX + cardW - 12;
  int yTemp = y + 10;

  d.setTextColor(TFT_RED, TFT_BLACK);
  d.setCursor(xHigh, yTemp);
  if (!isnan(df.tMaxC)) d.printf("%.0f", df.tMaxC);
  else d.print("--");

  d.setTextColor(TFT_CYAN, TFT_BLACK);
  d.setCursor(xLow, yTemp);
  if (!isnan(df.tMinC)) d.printf("%.0f", df.tMinC);
  else d.print("--");
}

static void drawScreen(const WeatherState& ws) {
  // Top half: current day
  drawCurrentDay(ws);

  // Bottom half: next 3 days (days 1, 2, 3 - skipping today which is day 0)
  for (int i = 1; i < 4; i++) {
    drawForecastRow(i - 1, ws.days[i]);
  }

  // footer status
  /*auto &d = StickCP2.Display;
  d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  d.setCursor(6, 232);
  if (!ws.ok) {
    d.print("No data (check WiFi)");
  } else {
    uint32_t age = (millis() - ws.lastFetchMs) / 1000;
    d.printf("Updated %lus ago", (unsigned long)age);
  }*/
}

// ====== APP LOGIC ======
static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  StickCP2.Display.fillScreen(TFT_BLACK);
  StickCP2.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  StickCP2.Display.setCursor(6, 6);
  StickCP2.Display.print("Connecting WiFi...");

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(200);
    StickCP2.Display.print(".");
  }
}

static void setupTime() {
  configTzTime(TZ_INFO, NTP_1, NTP_2);
}

static bool refreshWeather() {
  DBGLN("Refreshing weather...");
  if (WiFi.status() != WL_CONNECTED) return false;

  float lat = NAN, lon = NAN;
  char place[48] = {0};

  DBGLN("Getting lat/lon for zip...");
  if (!getLatLonForZip(lat, lon, place, sizeof(place))) return false;

  WeatherState ws = gWeather; // copy; keep last values if partial
  strncpy(ws.placeName, place, sizeof(ws.placeName) - 1);
  ws.placeName[sizeof(ws.placeName) - 1] = 0;

  if (!fetchOpenMeteo(lat, lon, ws)) {
    DBGLN("Failed to fetch weather");
    return false;
  }

  gWeather = ws;
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  DBGLN("\nBoot");

  auto cfg = M5.config();
  StickCP2.begin(cfg);
  StickCP2.Display.setRotation(ROTATION);
  StickCP2.Display.setTextFont(1);
  StickCP2.Display.setTextSize(1);
  StickCP2.Display.fillScreen(TFT_BLACK);

  connectWiFi();
  setupTime();

  // First fetch immediately
  bool ok = refreshWeather();
  gWeather.ok = ok;

  drawScreen(gWeather);
}

void loop() {
  StickCP2.update();

  // Button A: force refresh
  if (StickCP2.BtnA.wasPressed()) {
    bool ok = refreshWeather();
    gWeather.ok = ok;
    drawScreen(gWeather);
  }

  // Periodic refresh
  static uint32_t lastUpdate = 0;
  if (millis() - lastUpdate >= UPDATE_EVERY_MS) {
    lastUpdate = millis();
    bool ok = refreshWeather();
    gWeather.ok = ok;
    drawScreen(gWeather);
  }

  // Light UI refresh (age counter)
  static uint32_t lastPaint = 0;
  if (millis() - lastPaint >= 1000) {
    lastPaint = millis();
    drawScreen(gWeather);
  }

  delay(10);
}
