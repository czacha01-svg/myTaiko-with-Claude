#include <EEPROM.h>
#include <NintendoSwitchControlLibrary.h>
#include <Keyboard.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <PCF8574.h>

// OLED setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// PCF8574 port expander
PCF8574 expander(0x20);


bool editMode = false;

// Rotary encoder pins
const int ENC_CLK = 16;
const int ENC_DT  = 14;
const int ENC_SW  = 15;

// Joystick pins
const int JOY_UP    = 1;
const int JOY_DOWN  = 0;
const int JOY_LEFT  = 5;
const int JOY_RIGHT = 4;

// Direct buttons on Pro Micro
const int BTN_CAPTURE = 6;
const int BTN_HOME    = 7;
const int BTN_MINUS   = 8;
const int BTN_L       = 9;

// Expander buttons
// P0=B, P1=A, P2=Y, P3=X, P4=Plus, P5=R

// EEPROM addresses
const int ADDR_A0   = 0;
const int ADDR_A1   = 2;
const int ADDR_A2   = 4;
const int ADDR_A3   = 6;
const int ADDR_HOLD = 8;
const int ADDR_MODE = 10;

// Drum parameters
int trigger_threshold_A0 = 25; // right ka — raise to avoid LKa crosstalk
int trigger_threshold_A1 = 45;  // right don
int trigger_threshold_A2 = 45;  // left don  
int trigger_threshold_A3 = 25;  // left ka — slightly lower than RKa
int hold_time = 60;
int mode = 1;

// Drum variables
const int windows_size = 25;
const int cd_length = 5;
const int buffer_size = cd_length * 4;
int windowsA0[windows_size];
int windowsA1[windows_size];
int windowsA2[windows_size];
int windowsA3[windows_size];
int first_index = 0;
int second_index = 0;
int press_time[4] = {0, 0, 0, 0};
bool button_status[4] = {0, 0, 0, 0};

// Menu
const int MENU_ITEMS = 6;
const char* menuLabels[] = {
  "RKa Threshold",
  "RDon Threshold",
  "LDon Threshold",
  "LKa Threshold",
  "Hold Time(ms)",
  "Mode"
};
int menuIndex = 0;
bool inMenu = false;
int* menuValues[] = {
  &trigger_threshold_A0,
  &trigger_threshold_A1,
  &trigger_threshold_A2,
  &trigger_threshold_A3,
  &hold_time,
  &mode
};
const int menuMin[]  = {5,  5,  5,  5,  30, 1};
const int menuMax[]  = {200,200,200,200,200,2};
const int menuStep[] = {5,  5,  5,  5,  5,  1};

// State tracking
int lastCLK;
unsigned long buttonHoldStart = 0;
bool buttonHeld = false;
bool lastButtonState = HIGH;
bool expanderState[8] = {};
bool directBtnState[4] = {};

void saveToEEPROM() {
  EEPROM.put(ADDR_A0,   trigger_threshold_A0);
  EEPROM.put(ADDR_A1,   trigger_threshold_A1);
  EEPROM.put(ADDR_A2,   trigger_threshold_A2);
  EEPROM.put(ADDR_A3,   trigger_threshold_A3);
  EEPROM.put(ADDR_HOLD, hold_time);
  EEPROM.put(ADDR_MODE, mode);
}

void loadFromEEPROM() {
  int val;
  EEPROM.get(ADDR_A0, val);
  if (val >= menuMin[0] && val <= menuMax[0]) trigger_threshold_A0 = val;
  EEPROM.get(ADDR_A1, val);
  if (val >= menuMin[1] && val <= menuMax[1]) trigger_threshold_A1 = val;
  EEPROM.get(ADDR_A2, val);
  if (val >= menuMin[2] && val <= menuMax[2]) trigger_threshold_A2 = val;
  EEPROM.get(ADDR_A3, val);
  if (val >= menuMin[3] && val <= menuMax[3]) trigger_threshold_A3 = val;
  EEPROM.get(ADDR_HOLD, val);
  if (val >= menuMin[4] && val <= menuMax[4]) hold_time = val;
  EEPROM.get(ADDR_MODE, val);
  if (val >= 1 && val <= 2) mode = val;
}

void updateDisplay() {
  if (!inMenu) {
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    return;
  }
  
  display.ssd1306_command(SSD1306_DISPLAYON);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(menuLabels[menuIndex]);
  display.setCursor(0, 20);
  display.setTextSize(2);
  if (menuIndex == 5) {
    display.print(mode == 1 ? "Switch" : "PC");
  } else {
    display.print(*menuValues[menuIndex]);
  }
  display.setTextSize(1);
  display.setCursor(0, 48);
  display.print("Click:next Hold:save");
  display.display();
}

void handleEncoder() {
  static unsigned long lastEncoderTime = 0;
  int currentCLK = digitalRead(ENC_CLK);
  
  if (currentCLK != lastCLK && currentCLK == LOW) {
  if (millis() - lastEncoderTime > 10) {
    if (inMenu) {
      if (editMode && menuIndex < 5) {
        // Change value
        if (digitalRead(ENC_DT) != currentCLK) {
          *menuValues[menuIndex] = min(*menuValues[menuIndex] + menuStep[menuIndex], menuMax[menuIndex]);
        } else {
          *menuValues[menuIndex] = max(*menuValues[menuIndex] - menuStep[menuIndex], menuMin[menuIndex]);
        }
      } else if (!editMode) {
        // Move cursor
        if (digitalRead(ENC_DT) != currentCLK) {
          menuIndex = (menuIndex + 1) % MENU_ITEMS;
        } else {
          menuIndex = (menuIndex - 1 + MENU_ITEMS) % MENU_ITEMS;
        }
      }
      updateDisplay();
    }
    lastEncoderTime = millis();
  }
}
  lastCLK = currentCLK;

  bool currentButton = digitalRead(ENC_SW);
  if (currentButton == LOW && lastButtonState == HIGH) {
    buttonHoldStart = millis();
  }
  if (currentButton == LOW && !buttonHeld) {
    if (millis() - buttonHoldStart > 1000) {
      buttonHeld = true;
      if (!inMenu) {
        inMenu = true;
        menuIndex = 0;
      } else {
        saveToEEPROM();
        inMenu = false;
      }
      updateDisplay();
    }
  }
  if (currentButton == HIGH && lastButtonState == LOW) {
    if (!buttonHeld && millis() - buttonHoldStart < 1000) {
      if (inMenu) {
        if (menuIndex == 5) {
          // Save and exit
          saveToEEPROM();
          inMenu = false;
          editMode = false;
        } else if (editMode) {
          // Exit edit mode for this item
          editMode = false;
        } else {
          // Enter edit mode OR move cursor
          if (!editMode) {
            editMode = true; // enter edit mode
        }
      }
      updateDisplay();
      }
    }
    buttonHeld = false;
    }
  lastButtonState = currentButton;
}

void handleJoystick() {
  bool up    = (digitalRead(JOY_UP)    == LOW);
  bool down  = (digitalRead(JOY_DOWN)  == LOW);
  bool left  = (digitalRead(JOY_LEFT)  == LOW);
  bool right = (digitalRead(JOY_RIGHT) == LOW);

  if      (up)    SwitchControlLibrary().pressHatButton(Hat::UP);
  else if (down)  SwitchControlLibrary().pressHatButton(Hat::DOWN);
  else if (left)  SwitchControlLibrary().pressHatButton(Hat::LEFT);
  else if (right) SwitchControlLibrary().pressHatButton(Hat::RIGHT);
  else            SwitchControlLibrary().releaseHatButton();

  SwitchControlLibrary().sendReport();
}

void handleDirectButtons() {
  // Capture
  bool capture = (digitalRead(BTN_CAPTURE) == LOW);
  if (capture && !directBtnState[0]) SwitchControlLibrary().pressButton(Button::CAPTURE);
  else if (!capture && directBtnState[0]) SwitchControlLibrary().releaseButton(Button::CAPTURE);
  directBtnState[0] = capture;

  // Home
  bool home = (digitalRead(BTN_HOME) == LOW);
  if (home && !directBtnState[1]) SwitchControlLibrary().pressButton(Button::HOME);
  else if (!home && directBtnState[1]) SwitchControlLibrary().releaseButton(Button::HOME);
  directBtnState[1] = home;

  // Minus
  bool minus = (digitalRead(BTN_MINUS) == LOW);
  if (minus && !directBtnState[2]) SwitchControlLibrary().pressButton(Button::MINUS);
  else if (!minus && directBtnState[2]) SwitchControlLibrary().releaseButton(Button::MINUS);
  directBtnState[2] = minus;

  // L
  bool l = (digitalRead(BTN_L) == LOW);
  if (l && !directBtnState[3]) SwitchControlLibrary().pressButton(Button::L);
  else if (!l && directBtnState[3]) SwitchControlLibrary().releaseButton(Button::L);
  directBtnState[3] = l;

  SwitchControlLibrary().sendReport();
}

void handleExpanderButtons() {
  bool states[8];
  for (int i = 0; i < 8; i++) {
    states[i] = (expander.digitalRead(i) == LOW);
  }

  // P0=B
  if (states[0] && !expanderState[0]) SwitchControlLibrary().pressButton(Button::B);
  else if (!states[0] && expanderState[0]) SwitchControlLibrary().releaseButton(Button::B);

  // P1=A
  if (states[1] && !expanderState[1]) SwitchControlLibrary().pressButton(Button::A);
  else if (!states[1] && expanderState[1]) SwitchControlLibrary().releaseButton(Button::A);

  // P2=Y
  if (states[2] && !expanderState[2]) SwitchControlLibrary().pressButton(Button::Y);
  else if (!states[2] && expanderState[2]) SwitchControlLibrary().releaseButton(Button::Y);

  // P3=X
  if (states[3] && !expanderState[3]) SwitchControlLibrary().pressButton(Button::X);
  else if (!states[3] && expanderState[3]) SwitchControlLibrary().releaseButton(Button::X);

  // P4=Plus
  if (states[4] && !expanderState[4]) SwitchControlLibrary().pressButton(Button::PLUS);
  else if (!states[4] && expanderState[4]) SwitchControlLibrary().releaseButton(Button::PLUS);

  // P5=R
  if (states[5] && !expanderState[5]) SwitchControlLibrary().pressButton(Button::R);
  else if (!states[5] && expanderState[5]) SwitchControlLibrary().releaseButton(Button::R);

  for (int i = 0; i < 8; i++) expanderState[i] = states[i];

  SwitchControlLibrary().sendReport();
}

void analogMonitor() {
  int A0_Value = windowsA0[first_index]-windowsA0[second_index];
  int A1_Value = windowsA1[first_index]-windowsA1[second_index];
  int A2_Value = windowsA2[first_index]-windowsA2[second_index];
  int A3_Value = windowsA3[first_index]-windowsA3[second_index];
  
  if(A0_Value > 5 || A1_Value > 5 || A2_Value > 5 || A3_Value > 5){
    Serial.print("RKa:"); Serial.print(A0_Value);
    Serial.print(" RDon:"); Serial.print(A1_Value);
    Serial.print(" LDon:"); Serial.print(A2_Value);
    Serial.print(" LKa:"); Serial.println(A3_Value);
  }
}
void setup() {
  //Serial.begin(9600);
  if (!display.begin(SSD1306_EXTERNALVCC, 0x3C)) {
    // continue without display
  }
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);

  expander.begin();
  for (int i = 0; i < 8; i++) {
    expander.pinMode(i, INPUT_PULLUP);
  }

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);
  lastCLK = digitalRead(ENC_CLK);

  pinMode(JOY_UP,    INPUT_PULLUP);
  pinMode(JOY_DOWN,  INPUT_PULLUP);
  pinMode(JOY_LEFT,  INPUT_PULLUP);
  pinMode(JOY_RIGHT, INPUT_PULLUP);

  pinMode(BTN_CAPTURE, INPUT_PULLUP);
  pinMode(BTN_HOME,    INPUT_PULLUP);
  pinMode(BTN_MINUS,   INPUT_PULLUP);
  pinMode(BTN_L,       INPUT_PULLUP);

  loadFromEEPROM();
  analogReference(INTERNAL);
  updateDisplay();
}

void loop() {
  handleEncoder();
  handleJoystick();
  handleDirectButtons();
  handleExpanderButtons();

  for (int i = 0; i < 4; i++) {
    if (button_status[i] == 1 && millis() - press_time[i] > hold_time) {
      switch(i) {
        case 0: SwitchControlLibrary().releaseButton(Button::ZR); break;
        case 1: SwitchControlLibrary().releaseButton(Button::RCLICK); break;
        case 2: SwitchControlLibrary().releaseButton(Button::LCLICK); break;
        case 3: SwitchControlLibrary().releaseButton(Button::ZL); break;
      }
      SwitchControlLibrary().sendReport();
      button_status[i] = 0;
    }
  }

  windowsA0[second_index] = analogRead(A0);
  windowsA1[second_index] = analogRead(A1);
  windowsA2[second_index] = analogRead(A2);
  windowsA3[second_index] = analogRead(A3);
  first_index = second_index;
  second_index = (second_index + 1) % windows_size;

  //analogMonitor();
  
  int A0_delta = windowsA0[first_index] - windowsA0[second_index];
  int A1_delta = windowsA1[first_index] - windowsA1[second_index];
  int A2_delta = windowsA2[first_index] - windowsA2[second_index];
  int A3_delta = windowsA3[first_index] - windowsA3[second_index];

  if (A0_delta < 0) A0_delta = 0;
  if (A1_delta < 0) A1_delta = 0;
  if (A2_delta < 0) A2_delta = 0;
  if (A3_delta < 0) A3_delta = 0;
  
  if (A0_delta > trigger_threshold_A0 && button_status[0] == 0) {
    SwitchControlLibrary().pressButton(Button::ZR);
    SwitchControlLibrary().sendReport();
    press_time[0] = millis();
    button_status[0] = 1;
  }
  if (A1_delta > trigger_threshold_A1 && button_status[1] == 0) {
    SwitchControlLibrary().pressButton(Button::RCLICK);
    SwitchControlLibrary().sendReport();
    press_time[1] = millis();
    button_status[1] = 1;
  }
  if (A2_delta > trigger_threshold_A2 && button_status[2] == 0) {
    SwitchControlLibrary().pressButton(Button::LCLICK);
    SwitchControlLibrary().sendReport();
    press_time[2] = millis();
    button_status[2] = 1;
  }
  if (A3_delta > trigger_threshold_A3 && button_status[3] == 0) {
    SwitchControlLibrary().pressButton(Button::ZL);
    SwitchControlLibrary().sendReport();
    press_time[3] = millis();
    button_status[3] = 1;
  }
}