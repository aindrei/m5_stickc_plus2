/*
  M5StickC Plus2 - WiFi + Internet Test (Screen Status)

  What it does:
    1) Connects to WiFi (2.4 GHz required for ESP32)
    2) Shows connection status, IP, RSSI on the screen
    3) Runs a basic internet test:
         - HTTP GET http://example.com/  (good for captive portal detection)
         - DNS test by printing resolved IP (implicit via HTTPClient)
    4) Button A = retry tests
       Button B = toggle details (shows last HTTP code / bytes)

  Libraries:
    - M5StickCPlus2 / M5Unified
    - WiFi (built-in)
    - HTTPClient (built-in)

  Put your SSID/PASS below.
*/

#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ====== EDIT THESE ======
const char* WIFI_SSID = "decoaxax_Guest";
const char* WIFI_PASS = "Dguesttherion12";
// ========================

static const uint32_t WIFI_TIMEOUT_MS = 20000;
static const uint32_t HTTP_TIMEOUT_MS = 12000;

static bool showDetails = false;

struct TestState {
  bool wifiOk = false;
  bool httpOk = false;

  int wifiStatus = WL_DISCONNECTED;
  String ip = "";
  int rssi = 0;

  int httpCode = 0;
  int payloadBytes = 0;
  String error = "";
};

static TestState st;

static void clearScreen() {
  StickCP2.Display.fillScreen(TFT_BLACK);
  StickCP2.Display.setTextFont(1);
  StickCP2.Display.setTextSize(1);
  StickCP2.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

static void drawHeader(const char* title) {
  StickCP2.Display.fillRect(0, 0, 240, 18, TFT_BLACK);
  StickCP2.Display.setCursor(6, 4);
  StickCP2.Display.print(title);
  StickCP2.Display.drawFastHLine(0, 18, 240, TFT_DARKGREY);
}

static void drawLine(int y, const String& s, uint16_t color = TFT_WHITE) {
  StickCP2.Display.setTextColor(color, TFT_BLACK);
  StickCP2.Display.fillRect(0, y, 240, 12, TFT_BLACK);
  StickCP2.Display.setCursor(6, y);
  StickCP2.Display.print(s);
}

static String wifiStatusToString(int s) {
  switch (s) {
    case WL_IDLE_STATUS:      return "IDLE";
    case WL_NO_SSID_AVAIL:    return "NO_SSID";
    case WL_SCAN_COMPLETED:   return "SCAN_DONE";
    case WL_CONNECTED:        return "CONNECTED";
    case WL_CONNECT_FAILED:   return "CONNECT_FAILED";
    case WL_CONNECTION_LOST:  return "CONNECTION_LOST";
    case WL_DISCONNECTED:     return "DISCONNECTED";
    default:                  return String("UNKNOWN(") + s + ")";
  }
}

static void render() {
  clearScreen();
  drawHeader("WiFi / Internet Test");

  drawLine(22, String("SSID: ") + WIFI_SSID, TFT_LIGHTGREY);

  // WiFi status
  uint16_t wcol = st.wifiOk ? TFT_GREEN : TFT_RED;
  drawLine(36, String("WiFi: ") + (st.wifiOk ? "OK" : "FAIL") +
               " (" + wifiStatusToString(st.wifiStatus) + ")", wcol);

  if (st.wifiOk) {
    drawLine(50, String("IP: ") + st.ip, TFT_WHITE);
    drawLine(64, String("RSSI: ") + st.rssi + " dBm", TFT_WHITE);
  } else {
    drawLine(50, "IP: --", TFT_WHITE);
    drawLine(64, "RSSI: --", TFT_WHITE);
  }

  // HTTP test status
  uint16_t hcol = st.httpOk ? TFT_GREEN : TFT_RED;
  drawLine(82, String("HTTP: ") + (st.httpOk ? "OK" : "FAIL"), hcol);

  if (showDetails) {
    drawLine(96, String("Code: ") + st.httpCode +
                 "  Bytes: " + st.payloadBytes, TFT_LIGHTGREY);
    if (st.error.length()) {
      // Truncate to fit
      String e = st.error;
      if (e.length() > 34) e = e.substring(0, 34) + "...";
      drawLine(110, String("Err: ") + e, TFT_LIGHTGREY);
    } else {
      drawLine(110, "Err: (none)", TFT_LIGHTGREY);
    }
  } else {
    drawLine(96, "Press B: details", TFT_LIGHTGREY);
    drawLine(110, "Press A: retry", TFT_LIGHTGREY);
  }

  // Footer
  StickCP2.Display.drawFastHLine(0, 126, 240, TFT_DARKGREY);
  drawLine(128, "A=Retry  B=Details", TFT_LIGHTGREY);
}

static void resetState() {
  st = TestState();
  st.wifiStatus = WiFi.status();
}

static void connectWiFi() {
  st.error = "";
  st.wifiOk = false;

  drawLine(36, "WiFi: connecting...", TFT_YELLOW);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  // Start fresh each run
  WiFi.disconnect(true, true);
  delay(200);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_TIMEOUT_MS) {
    st.wifiStatus = WiFi.status();
    drawLine(36, String("WiFi: ... ") + wifiStatusToString(st.wifiStatus), TFT_YELLOW);
    delay(300);
  }

  st.wifiStatus = WiFi.status();

  if (st.wifiStatus == WL_CONNECTED) {
    st.wifiOk = true;
    st.ip = WiFi.localIP().toString();
    st.rssi = WiFi.RSSI();
  } else {
    st.wifiOk = false;
    st.ip = "";
    st.rssi = 0;
  }
}

static void internetTestHTTP() {
  st.httpOk = false;
  st.httpCode = 0;
  st.payloadBytes = 0;
  st.error = "";

  if (!st.wifiOk) {
    st.error = "WiFi not connected";
    return;
  }

  drawLine(82, "HTTP: GET http://example.com/", TFT_YELLOW);

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);

  // Use HTTP (not HTTPS) to help detect captive portal behavior quickly.
  const char* url = "http://example.com/";
  http.begin(url);

  int code = http.GET();
  st.httpCode = code;

  if (code <= 0) {
    st.error = http.errorToString(code);
    http.end();
    return;
  }

  String payload = http.getString();
  st.payloadBytes = payload.length();

  http.end();

  // Consider "OK" if we got a 200 and some body bytes.
  // Captive portals often return 30x or 200 with weird content, but this is still a signal.
  st.httpOk = (st.httpCode == 200 && st.payloadBytes > 0);
  if (!st.httpOk && st.httpCode != 200) {
    st.error = String("Non-200 response: ") + st.httpCode;
  }
}

static void runTests() {
  resetState();
  render();

  connectWiFi();
  render();

  internetTestHTTP();
  render();
}

void setup() {
  auto cfg = M5.config();
  StickCP2.begin(cfg);
  StickCP2.Display.setRotation(1); // landscape
  clearScreen();

  runTests();
}

void loop() {
  StickCP2.update();

  if (StickCP2.BtnA.wasPressed()) {
    runTests();
  }

  if (StickCP2.BtnB.wasPressed()) {
    showDetails = !showDetails;
    render();
  }

  delay(10);
}
