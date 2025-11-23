/*
  Arduino Nano Multi-Game Console (Final Polish)
  Hardware: Arduino Nano, 2x Joystick Modules, SSD1306 OLED (I2C)
  
  Games:
  1. Pong (1P vs AI)
  2. Pong (1P vs 2P)
  3. Snake
  4. Space Dodge

  Fixes Applied:
  - Fixed "Menu Loop" (Wait for button release on exit)
  - Fixed Menu Scrolling (Requires stick centering)
  - Fixed Pong Ball Speed logic
  - Fixed AI Jitter
*/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- HARDWARE CONFIGURATION ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 
#define SCREEN_ADDRESS 0x3C // Try 0x3D if screen stays black

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- PIN DEFINITIONS ---
// Player 1
#define P1_VRX A0
#define P1_VRY A1
#define P1_SW  2

// Player 2
#define P2_VRX A2
#define P2_VRY A3
#define P2_SW  3

// Game States
enum GameState {
  MENU,
  PONG_1P,
  PONG_2P,
  SNAKE,
  DODGE,
  GAME_OVER
};

GameState currentState = MENU;
uint8_t selectedGame = 0; // 0:Pong1P, 1:Pong2P, 2:Snake, 3:Dodge

// --- INPUT VARIABLES ---
int p1X = 0, p1Y = 0;
int p2X = 0, p2Y = 0;
uint8_t p1Sw = 1, p2Sw = 1;
uint8_t lastP1Sw = 1;
unsigned long lastDebounceTime = 0;
const uint8_t debounceDelay = 50;
bool joystickCentered = true; // For menu navigation

// Long Press Logic
unsigned long buttonPressStart = 0;

// --- GAME VARIABLES ---

// PONG
uint8_t paddle1Y = 20;
uint8_t paddle2Y = 20; 
int16_t ballX = 64, ballY = 32; 
int8_t ballDirX = 2, ballDirY = 1;
uint8_t score1 = 0;
uint8_t score2 = 0;
#define PADDLE_HEIGHT 14

// SNAKE
#define MAX_SNAKE_LENGTH 100
uint8_t snakeX[MAX_SNAKE_LENGTH]; 
uint8_t snakeY[MAX_SNAKE_LENGTH];
uint8_t snakeLength = 3;
int8_t snakeDirX = 1;
int8_t snakeDirY = 0;
int8_t lastSnakeDirX = 1;
int8_t lastSnakeDirY = 0;
uint8_t foodX, foodY;
bool newFoodNeeded = true;

// SPACE DODGE
uint8_t shipX = 64;
#define NUM_STARS 5
uint8_t starX[NUM_STARS];
int8_t starY[NUM_STARS]; 
uint16_t dodgeScore = 0; 

void setup() {
  // Input Setup
  pinMode(P1_SW, INPUT_PULLUP);
  pinMode(P2_SW, INPUT_PULLUP);

  // Display Setup
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    for(;;); // Loop forever if display fails
  }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.display();
  
  // Seed random
  randomSeed(analogRead(A7)); 
}

void loop() {
  readInput();
  checkLongPress(); 
  
  display.clearDisplay();

  switch(currentState) {
    case MENU:
      runMenu();
      break;
    case PONG_1P:
      runPong(false); // false = AI Mode
      break;
    case PONG_2P:
      runPong(true);  // true = 2 Player Mode
      break;
    case SNAKE:
      runSnake();
      break;
    case DODGE:
      runDodge();
      break;
    case GAME_OVER:
      runGameOver();
      break;
  }

  display.display();
}

// --- INPUT & SYSTEM ---
void readInput() {
  p1X = analogRead(P1_VRX);
  p1Y = analogRead(P1_VRY);
  p2X = analogRead(P2_VRX);
  p2Y = analogRead(P2_VRY);
  
  // Debounce P1 Switch
  uint8_t reading = digitalRead(P1_SW);
  if (reading != lastP1Sw) lastDebounceTime = millis();
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != p1Sw) p1Sw = reading;
  }
  lastP1Sw = reading;

  p2Sw = digitalRead(P2_SW);
}

bool isP1Pressed() {
  return p1Sw == LOW;
}

void checkLongPress() {
  if (currentState != MENU) {
    // Check P1 or P2 button
    if (digitalRead(P1_SW) == LOW || digitalRead(P2_SW) == LOW) {
      if (buttonPressStart == 0) {
        buttonPressStart = millis();
      } else if (millis() - buttonPressStart > 1000) { // 1 second hold
        
        // --- CRITICAL FIX: Wait for release ---
        display.clearDisplay();
        display.setCursor(10, 30);
        display.print(F("Release Button"));
        display.display();
        
        // Wait until BOTH buttons are released
        while(digitalRead(P1_SW) == LOW || digitalRead(P2_SW) == LOW);
        
        // Reset state
        currentState = MENU;
        buttonPressStart = 0;
        p1Sw = HIGH; // Force software state to released
        delay(200);  // Short debounce
      }
    } else {
      buttonPressStart = 0;
    }
  }
}

// --- MENUS ---
void runMenu() {
  display.setTextSize(2);
  display.setCursor(30, 0);
  display.print(F("MENU")); 
  
  display.setTextSize(1);
  const char* games[] = {"PONG 1P", "PONG 2P", "SNAKE", "DODGE"};
  
  // Joystick Logic: Require return to center (approx 512) to scroll again
  // This prevents super fast scrolling
  if (p1Y > 400 && p1Y < 600) {
    joystickCentered = true;
  }

  if (joystickCentered) {
    if (p1Y < 300) { // Up
      if (selectedGame == 0) selectedGame = 3; else selectedGame--;
      joystickCentered = false;
    } else if (p1Y > 700) { // Down
      if (selectedGame == 3) selectedGame = 0; else selectedGame++;
      joystickCentered = false;
    }
  }

  for(uint8_t i=0; i<4; i++) {
    display.setCursor(10, 20 + (i*10));
    if(i == selectedGame) display.print(F("> "));
    display.print(games[i]);
  }

  if(isP1Pressed()) {
    if(selectedGame == 0) { resetPong(); currentState = PONG_1P; }
    if(selectedGame == 1) { resetPong(); currentState = PONG_2P; }
    if(selectedGame == 2) { resetSnake(); currentState = SNAKE; }
    if(selectedGame == 3) { resetDodge(); currentState = DODGE; }
    
    // Small blocking delay to ensure game loop doesn't read the press immediately
    delay(300); 
  }
}

void runGameOver() {
  display.setTextSize(2);
  display.setCursor(10, 5);
  display.print(F("GAME OVER"));
  
  display.setTextSize(1);
  display.setCursor(10, 30);
  
  if(currentState == PONG_1P || currentState == PONG_2P) { 
    display.print(F("P1: ")); display.print(score1);
    display.print(F("  P2: ")); display.print(score2);
  } else if (selectedGame == 2) { 
    display.print(F("Length: ")); display.print(snakeLength);
  } else { 
    display.print(F("Score: ")); display.print(dodgeScore);
  }

  display.setCursor(10, 50);
  display.print(F("Hold Button to Menu"));
}

// --- GAME 1: PONG ---
void resetPong() {
  ballX = 64; ballY = 32;
  ballDirX = 2; ballDirY = 1;
  score1 = 0; score2 = 0;
  paddle1Y = 20;
  paddle2Y = 20;
}

void runPong(bool twoPlayer) {
  // Player 1 (Left)
  if (p1Y < 300) {
      if (paddle1Y >= 3) paddle1Y -= 3; else paddle1Y = 0;
  }
  if (p1Y > 700) {
      if (paddle1Y <= (SCREEN_HEIGHT - PADDLE_HEIGHT - 3)) paddle1Y += 3; 
      else paddle1Y = SCREEN_HEIGHT - PADDLE_HEIGHT;
  }

  // Player 2 or AI (Right)
  if (twoPlayer) {
    if (p2Y < 300) {
        if (paddle2Y >= 3) paddle2Y -= 3; else paddle2Y = 0;
    }
    if (p2Y > 700) {
        if (paddle2Y <= (SCREEN_HEIGHT - PADDLE_HEIGHT - 3)) paddle2Y += 3;
        else paddle2Y = SCREEN_HEIGHT - PADDLE_HEIGHT;
    }
  } else {
    // AI Controls with Deadzone to stop jitter
    uint8_t centerAI = paddle2Y + (PADDLE_HEIGHT/2);
    if(ballY < centerAI - 2) { // Ball is significantly above paddle center
        if (paddle2Y >= 2) paddle2Y -= 2; else paddle2Y = 0;
    }
    if(ballY > centerAI + 2) { // Ball is significantly below paddle center
        if (paddle2Y <= (SCREEN_HEIGHT - PADDLE_HEIGHT - 2)) paddle2Y += 2;
        else paddle2Y = SCREEN_HEIGHT - PADDLE_HEIGHT;
    }
  }

  // Ball Physics
  ballX += ballDirX;
  ballY += ballDirY;

  // Wall Collisions
  if(ballY <= 0) { ballY = 1; ballDirY *= -1; }
  if(ballY >= SCREEN_HEIGHT) { ballY = SCREEN_HEIGHT - 1; ballDirY *= -1; }

  // Paddle Collisions
  if(ballX <= 4 && ballY >= paddle1Y && ballY <= paddle1Y + PADDLE_HEIGHT) {
    ballDirX = abs(ballDirX); // Force Right
    ballX = 6; 
    // Increase speed: Add 1 to magnitude if < 4
    if(abs(ballDirX) < 4) ballDirX += 1; 
  }
  if(ballX >= SCREEN_WIDTH - 5 && ballY >= paddle2Y && ballY <= paddle2Y + PADDLE_HEIGHT) {
    ballDirX = -abs(ballDirX); // Force Left
    ballX = SCREEN_WIDTH - 6; 
     // Increase speed: Subtract 1 from magnitude (making it more negative)
    if(abs(ballDirX) < 4) ballDirX -= 1;
  }

  // Scoring
  if(ballX < 0) { 
    score2++;
    ballX = 64; ballY = 32; ballDirX = 2; 
    if(score2 >= 5) currentState = GAME_OVER; 
  } 
  if(ballX > SCREEN_WIDTH) { 
    score1++;
    ballX = 64; ballY = 32; ballDirX = -2; 
    if(score1 >= 5) currentState = GAME_OVER;
  }

  // Draw
  display.fillRect(0, paddle1Y, 3, PADDLE_HEIGHT, SSD1306_WHITE); 
  display.fillRect(SCREEN_WIDTH-3, paddle2Y, 3, PADDLE_HEIGHT, SSD1306_WHITE); 
  display.fillCircle(ballX, ballY, 2, SSD1306_WHITE); 
  display.drawFastVLine(64, 0, 64, SSD1306_WHITE); 
}

// --- GAME 2: SNAKE ---
void resetSnake() {
  snakeLength = 3;
  snakeX[0] = 64; snakeY[0] = 32;
  snakeDirX = 1; snakeDirY = 0;
  lastSnakeDirX = 1; lastSnakeDirY = 0;
  newFoodNeeded = true;
}

void runSnake() {
  int8_t nextDirX = snakeDirX;
  int8_t nextDirY = snakeDirY;

  // Controls (P1 Only)
  if (p1Y < 300 && lastSnakeDirY == 0) { nextDirX = 0; nextDirY = -1; }
  else if (p1Y > 700 && lastSnakeDirY == 0) { nextDirX = 0; nextDirY = 1; }
  else if (p1X < 300 && lastSnakeDirX == 0) { nextDirX = -1; nextDirY = 0; }
  else if (p1X > 700 && lastSnakeDirX == 0) { nextDirX = 1; nextDirY = 0; }

  snakeDirX = nextDirX;
  snakeDirY = nextDirY;

  static unsigned long lastSnakeMove = 0;
  if(millis() - lastSnakeMove > 100) { 
    lastSnakeMove = millis();
    lastSnakeDirX = snakeDirX;
    lastSnakeDirY = snakeDirY;

    for (uint8_t i = snakeLength - 1; i > 0; i--) {
      snakeX[i] = snakeX[i - 1];
      snakeY[i] = snakeY[i - 1];
    }
    
    int16_t newHeadX = snakeX[0] + (snakeDirX * 4);
    int16_t newHeadY = snakeY[0] + (snakeDirY * 4);

    if(newHeadX < 0 || newHeadX >= SCREEN_WIDTH || newHeadY < 0 || newHeadY >= SCREEN_HEIGHT) { 
      currentState = GAME_OVER; return;
    }

    snakeX[0] = newHeadX;
    snakeY[0] = newHeadY;

    for(uint8_t i=1; i<snakeLength; i++) {
      if(snakeX[0] == snakeX[i] && snakeY[0] == snakeY[i]) {
          currentState = GAME_OVER;
      }
    }

    if (abs((int)snakeX[0] - (int)foodX) < 4 && abs((int)snakeY[0] - (int)foodY) < 4) {
      snakeLength++;
      if(snakeLength >= MAX_SNAKE_LENGTH) snakeLength = MAX_SNAKE_LENGTH;
      newFoodNeeded = true;
    }
  }

  if(newFoodNeeded) {
    foodX = random(1, (SCREEN_WIDTH/4)-1) * 4;
    foodY = random(1, (SCREEN_HEIGHT/4)-1) * 4;
    newFoodNeeded = false;
  }

  for(uint8_t i=0; i<snakeLength; i++) {
    display.fillRect(snakeX[i], snakeY[i], 3, 3, SSD1306_WHITE);
  }
  display.fillRect(foodX, foodY, 3, 3, SSD1306_WHITE); 
}

// --- GAME 3: SPACE DODGE ---
void resetDodge() {
  shipX = 64;
  dodgeScore = 0;
  for(uint8_t i=0; i<NUM_STARS; i++) {
    starX[i] = random(0, SCREEN_WIDTH);
    starY[i] = random(-64, 0); 
  }
}

void runDodge() {
  if (p1X < 300 && shipX > 0) shipX -= 4;
  if (p1X > 700 && shipX < SCREEN_WIDTH - 8) shipX += 4;

  for(uint8_t i=0; i<NUM_STARS; i++) {
    starY[i] += 2 + (dodgeScore / 150); 

    if(starY[i] > SCREEN_HEIGHT) {
      starY[i] = -10; 
      starX[i] = random(0, SCREEN_WIDTH);
      dodgeScore += 10;
    }

    if(starY[i] >= SCREEN_HEIGHT - 10 && starY[i] < SCREEN_HEIGHT) {
      if(starX[i] >= shipX - 2 && starX[i] <= shipX + 10) {
        currentState = GAME_OVER;
      }
    }
    display.drawPixel(starX[i], starY[i], SSD1306_WHITE);
  }

  display.fillTriangle(shipX, SCREEN_HEIGHT-1, shipX+4, SCREEN_HEIGHT-10, shipX+8, SCREEN_HEIGHT-1, SSD1306_WHITE);
  display.setCursor(0,0);
  display.print(dodgeScore);
}