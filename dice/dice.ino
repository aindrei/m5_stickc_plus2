/**
 * M5 StickC Plus2 Dice Application
 * 
 * Shows 2 dice on the screen. When the device is shaken,
 * the dice randomly rotate quickly and a new face is 
 * randomly chosen after a couple of seconds.
 * 
 * Hardware: M5 StickC Plus2
 * Libraries: M5Unified, M5GFX
 */

#include <M5Unified.h>
#include <M5GFX.h>
#include "M5StickCPlus2.h"

// Dice configuration
#define DICE_SIZE 100
#define DICE_SPACING 20
#define DICE_PADDING 30
#define ANIMATION_DURATION 500  // ms
#define SETTLE_DELAY 2000       // ms before showing final result

// Colors
#define DICE_BG_COLOR 0xFFFFFF  // White
#define DICE_DOT_COLOR 0x000000  // Black
#define DICE_BORDER_COLOR 0x333333

// Global variables
int dice1 = 1;
int dice2 = 1;
int targetDice1 = 1;
int targetDice2 = 1;

bool isRolling = false;
unsigned long rollStartTime = 0;
unsigned long settleStartTime = 0;

// Shake detection variables
float lastAccel = 0;
float currentAccel = 0;
unsigned long lastShakeTime = 0;
const unsigned long SHAKE_COOLDOWN = 1000;  // ms between shake detections
const float SHAKE_THRESHOLD = 40.0;  // Acceleration threshold for shake detection

// Acceleration data (same as accel.ino)
float ax = 0, ay = 0, az = 0;

// Store dice dot positions for each face (1-6)
// Format: {x, y} normalized positions (-1 to 1)
const float dotPositions[6][7][2] = {
  // Face 1 - center dot
  {{0, 0}},
  // Face 2 - top-left and bottom-right
  {{-0.4, -0.4}, {0.4, 0.4}},
  // Face 3 - three diagonal dots
  {{-0.4, -0.4}, {0, 0}, {0.4, 0.4}},
  // Face 4 - four corner dots
  {{-0.4, -0.4}, {0.4, -0.4}, {-0.4, 0.4}, {0.4, 0.4}},
  // Face 5 - five dots (corners + center)
  {{-0.4, -0.4}, {0.4, -0.4}, {0, 0}, {-0.4, 0.4}, {0.4, 0.4}},
  // Face 6 - six dots (two columns of three)
  {{-0.4, -0.4}, {-0.4, 0}, {-0.4, 0.4}, {0.4, -0.4}, {0.4, 0}, {0.4, 0.4}}
};

void drawDice(int x, int y, int size, int face) {
  // Draw dice background (rounded rectangle)
  int radius = size / 10;
  M5.Lcd.fillRoundRect(x, y, size, size, radius, DICE_BG_COLOR);
  M5.Lcd.drawRoundRect(x, y, size, size, radius, DICE_BORDER_COLOR);
  
  // Draw dots
  int dotCount = face;
  int dotSize = size / 8;
  
  for (int i = 0; i < dotCount; i++) {
    int dotX = x + size / 2 + (int)(dotPositions[face - 1][i][0] * size / 2);
    int dotY = y + size / 2 + (int)(dotPositions[face - 1][i][1] * size / 2);
    
    M5.Lcd.fillCircle(dotX, dotY, dotSize, DICE_DOT_COLOR);
  }
}

void drawAllDice() {
  // Clear screen
  M5.Lcd.fillScreen(TFT_BLACK);

  StickCP2.Speaker.tone(4000, 10);
  
  // Calculate positions to center two dice
  int screenWidth = M5.Lcd.width();
  int screenHeight = M5.Lcd.height();
  
  int totalWidth = DICE_SIZE * 2 + DICE_SPACING;
  int startX = (screenWidth - totalWidth) / 2;
  int startY = (screenHeight - DICE_SIZE) / 2;
  
  // Draw both dice
  drawDice(startX, startY, DICE_SIZE, dice1);
  drawDice(startX + DICE_SIZE + DICE_SPACING, startY, DICE_SIZE, dice2);
}

void startRoll() {
  isRolling = true;
  rollStartTime = millis();
  settleStartTime = millis();
  lastShakeTime = millis();
}

void updateRoll() {
  if (!isRolling) return;
  
  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - rollStartTime;
  unsigned long settleElapsed = currentTime - settleStartTime;
  
  // Animate dice while rolling (first phase)
  if (elapsed < ANIMATION_DURATION) {
    // Random dice faces during animation
    dice1 = random(1, 7);
    dice2 = random(1, 7);
    
    // Redraw with slight delay for animation effect
    delay(50);
    drawAllDice();
  }
  // Settle phase - show random but slower
  else if (settleElapsed < SETTLE_DELAY) {
    // Slow down the animation
    if (random(10) < 5) {  // 50% chance to change each frame
      dice1 = random(1, 7);
      dice2 = random(1, 7);
      drawAllDice();
    }
  }
  // Final result
  else {
    dice1 = targetDice1;
    dice2 = targetDice2;
    isRolling = false;
    drawAllDice();
  }
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

void checkShake() {
  // Read accelerometer data using the same method as accel.ino
  readAccelerometer();
  
  // Calculate total acceleration magnitude
  currentAccel = sqrt(ax * ax + ay * ay + az * az);
  
  // Calculate acceleration change
  float accelChange = abs(currentAccel - lastAccel);
  lastAccel = currentAccel;
  
  unsigned long currentTime = millis();
  
  // Check if shake detected (with cooldown to prevent multiple triggers)
  if (accelChange > SHAKE_THRESHOLD && 
      !isRolling && 
      (currentTime - lastShakeTime) > SHAKE_COOLDOWN) {
    // Generate new random target values
    targetDice1 = random(1, 7);
    targetDice2 = random(1, 7);
    
    startRoll();
    lastShakeTime = currentTime;
  }
}

void setup() {
  // Initialize M5 device
  auto cfg = M5.config();
  M5.begin(cfg);
  
  // Set display rotation (optional - adjust as needed)
  M5.Lcd.setRotation(1);
  
  // Seed random number generator
  randomSeed(analogRead(0));
  
  // Initialize dice with random values
  dice1 = random(1, 7);
  dice2 = random(1, 7);
  targetDice1 = dice1;
  targetDice2 = dice2;
  
  // Initial draw
  drawAllDice();
}

void loop() {
  // Update M5 device
  M5.update();
  
  // Check for button A press to trigger roll
  if (M5.BtnA.wasPressed() && !isRolling) {
    targetDice1 = random(1, 7);
    targetDice2 = random(1, 7);
    startRoll();
  }
  
  // Check for shake detection
  checkShake();
  
  // Update rolling animation
  updateRoll();
  
  // Small delay to prevent overwhelming the system
  delay(10);
}
