#ifdef __IN_ECLIPSE__
//This is a automatic generated file
//Please do not modify this file
//If you touch this file your change will be overwritten during the next build
//This file has been generated on 2019-04-05 06:08:20

#include "Arduino.h"
#include "swRTC.h"
#include "OMEEPROM.h"
#include "OMMenuMgr.h"
#include <avr/wdt.h>
#include "OneWire.h"
#include "DallasTemperature.h"
#include <Wire.h>
#include "Sim800l.h"
#include <SoftwareSerial.h>
#include "LiquidCrystal_I2C.h"
#include "Keypad_I2C.h"
#include "Keypad.h"
void readMessage(int index, byte* msg) ;
void loadEEPROM() ;
void saveDefaultEEPROM() ;
void setup() ;
double analogRead(int pin, int samples);
void loop() ;
void uiResetAction() ;
void uiDraw(char* p_text, int p_row, int p_col, int len) ;
void uiAlarmList() ;
void uiInfo() ;
void uiTestCall() ;
void uiScreen() ;
void uiLcdPrintAlarm(bool alarmHigh, bool alarmLow) ;
void uiLcdPrintSpaces8() ;
void uiMain() ;

#include "growmat_easy_005a.ino"


#endif
