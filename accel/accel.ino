/**
 * M5 StickC Plus2 Accelerometer Debug Application
 * 
 * Displays acceleration values (X, Y, Z) and the total acceleration magnitude.
 * Shows a visual representation of the acceleration as bars and a circular gauge.
 * 
 * Hardware: M5 StickC Plus2
 * Libraries: M5StickCPlus2
 */

#include "M5StickCPlus2.h"

// Colors
#define COLOR_BG TFT_BLACK
#define COLOR_TEXT TFT_WHITE
#define COLOR_X TFT_RED
#define COLOR_Y TFT_GREEN
#define COLOR_Z TFT_BLUE
#define COLOR_MAG TFT_YELLOW

// Display settings
#define BAR_WIDTH 40
#define BAR_MAX_HEIGHT 80
#define GAUGE_RADIUS 50

// Global variables for acceleration
float ax = 0, ay = 0, az = 0;

void drawTextWithLabel(int x, int y, const char* label, float value, int color) {
  StickCP2.Display.setTextColor(color);
  StickCP2.Display.setTextSize(1);
  
  // Draw label
  StickCP2.Display.setCursor(x, y);
  StickCP2.Display.print(label);
  
  // Draw value
  StickCP2.Display.setCursor(x + 35, y);
  StickCP2.Display.setTextColor(COLOR_TEXT);
  StickCP2.Display.printf("%6.2f", value);
}

void drawBar(int x, int y, int width, int height, float value, float maxValue, int color) {
  // Normalize value to bar height
  int barHeight = (int)((value / maxValue) * height);
  barHeight = constrain(barHeight, 0, height);
  
  // Draw background bar (dark)
  StickCP2.Display.fillRect(x, y, width, height, 0x222222);
  
  // Draw value bar
  StickCP2.Display.fillRect(x, y + height - barHeight, width, barHeight, color);
  
  // Draw border
  StickCP2.Display.drawRect(x, y, width, height, color);
}

void drawCircularGauge(int centerX, int centerY, int radius, float value, float maxValue, int color) {
  // Draw outer circle
  StickCP2.Display.drawCircle(centerX, centerY, radius, color);
  
  // Calculate angle
  float normalizedValue = constrain(value / maxValue, 0.0, 1.0);
  float angle = -PI/2 + normalizedValue * PI;
  
  // Calculate needle end position
  int needleLength = radius - 5;
  int needleX = centerX + (int)(cos(angle) * needleLength);
  int needleY = centerY + (int)(sin(angle) * needleLength);
  
  // Draw needle
  StickCP2.Display.drawLine(centerX, centerY, needleX, needleY, color);
  
  // Draw center dot
  StickCP2.Display.fillCircle(centerX, centerY, 3, color);
}

// Display settings - adjusted for 135x240 screen
#define BAR_WIDTH 35
#define BAR_MAX_HEIGHT 60
#define GAUGE_RADIUS 35

void drawAccelerationVisualization() {
  int screenWidth = StickCP2.Display.width();
  int screenHeight = StickCP2.Display.height();
  
  // Calculate acceleration magnitude
  float magnitude = sqrt(ax * ax + ay * ay + az * az);
  
  // Title - smaller text
  StickCP2.Display.setTextSize(1);
  StickCP2.Display.setTextColor(COLOR_TEXT);
  StickCP2.Display.setCursor(5, 5);
  StickCP2.Display.print("Accel Debug");
  
  // Subtitle with magnitude
  StickCP2.Display.setCursor(5, 18);
  StickCP2.Display.setTextColor(COLOR_MAG);
  StickCP2.Display.printf("|A| = %.2f", magnitude);
  
  // Draw bar chart for X, Y, Z - compact layout
  int barStartY = 32;
  int barSpacing = 22;
  int barX = 5;
  
  // X acceleration bar
  drawTextWithLabel(barX, barStartY, "X:", ax, COLOR_X);
  drawBar(barX + 55, barStartY - 3, BAR_WIDTH, BAR_MAX_HEIGHT, abs(ax), 20.0, COLOR_X);
  
  // Y acceleration bar
  drawTextWithLabel(barX, barStartY + barSpacing, "Y:", ay, COLOR_Y);
  drawBar(barX + 55, barStartY + barSpacing - 3, BAR_WIDTH, BAR_MAX_HEIGHT, abs(ay), 20.0, COLOR_Y);
  
  // Z acceleration bar
  drawTextWithLabel(barX, barStartY + barSpacing * 2, "Z:", az, COLOR_Z);
  drawBar(barX + 55, barStartY + barSpacing * 2 - 3, BAR_WIDTH, BAR_MAX_HEIGHT, abs(az), 20.0, COLOR_Z);
  
  // Draw circular gauge for magnitude
  int gaugeX = screenWidth - GAUGE_RADIUS - 15;
  int gaugeY = barStartY + BAR_MAX_HEIGHT / 2 + 5;
  
  drawCircularGauge(gaugeX, gaugeY, GAUGE_RADIUS, magnitude, 30.0, COLOR_MAG);
  
  // Draw reference lines for gravity
  StickCP2.Display.setTextColor(TFT_DARKGREY);
  StickCP2.Display.setCursor(5, screenHeight - 10);
  StickCP2.Display.print("1g=9.8");
}

void readAccelerometer() {
  // Update IMU data first
  auto imu_update = StickCP2.Imu.update();
  
  if (imu_update) {
    // Get acceleration data. The data structure is changed and acceleration data is obtained from the gyro data structure.
    auto data = StickCP2.Imu.getImuData();
    ax = data.gyro.x;
    ay = data.gyro.y;
    az = data.gyro.z;
  }
}

void setup() {
  // Initialize M5 StickC Plus2 device
  auto cfg = M5.config();
  StickCP2.begin(cfg);
  
  // Set display rotation
  StickCP2.Display.setRotation(1);
  
  // Initial screen clear
  StickCP2.Display.fillScreen(COLOR_BG);
}

void loop() {
  // Read accelerometer
  readAccelerometer();
  
  // Clear and redraw
  StickCP2.Display.fillScreen(COLOR_BG);
  drawAccelerationVisualization();
  
  // Delay for readable refresh rate
  delay(300);
}
