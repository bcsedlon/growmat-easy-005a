/*
 * apple, temperature sensors, external alarm
 */

const byte KPD_ROWS = 4;
const byte KPD_COLS = 4;
char keys[KPD_ROWS][KPD_COLS] = {
		  {'1','2','3','A'},
		  {'4','5','6','B'},
		  {'7','8','9','C'},
		  {'*','0','#','D'}
};

#define SMSINDEX 1
#define KPD_I2CADDR 0x20

#define TEXT_GROWMATEASY "GROWMAT.CZ"
#define VERSION 1

#include "swRTC.h"
swRTC rtc;

// Menu
#include "OMEEPROM.h"
#include "OMMenuMgr.h"

#define LCD_I2CADDR 0x27	//0x3F
const byte LCD_ROWS = 2;
const byte LCD_COLS = 16;

#define LED_PIN 13

//#define SIM800LTX_PIN 2
//#define SIM800LRX_PIN 3
#define SIM800LRESET_PIN 4

#define ONEWIREBUS_PIN 5

#define SAMPLES 10

#define TEMPHIGHALARM_ADDR 100
#define TEMPLOWALARM_ADDR 104

#define GSMCOUNTRYNUMBER_ADDR 200  //8 bytes
#define GSMNUMBER_ADDR 208  //8 bytes
#define GSMCODE_ADDR 216
#define GSMMODE_ADDR 220

#define MESSAGESOFFSET_ADDR 224
#define MESSAGES_ADDR 228
#define MESSAGESCOUNT 32
#define MESSAGELENGTH 16
//                              "0123456789ABCDEF"
#define MESSAGE_ALARM_POWERON   "dd/mm hh:mm ON x"
#define MESSAGE_ALARM_TEMPHIGH  "dd/mm hh:mm T+ x"
#define MESSAGE_ALARM_TEMPLOW   "dd/mm hh:mm T- x"
#define MESSAGE_ALARM_ON  		'1'
#define MESSAGE_ALARM_OFF 		'0'

#define MESSAGE_TEMPHIGH		"T+"
#define MESSAGE_TEMPLOW 		"T-"
#define MESSAGE_EMPTY			"  "

#define MESSAGE_TEMP 			"T="
#define MESSAGE_GSM  			"G="

#define MESSAGE_CMD_REQUEST  	"#?"
#define MESSAGE_CMD_GSMMODE  	"#M"

#define MESSAGE_CMD_PARREADINT 		"#PRI"
#define MESSAGE_CMD_PARREADFLOAT 	"#PRF"
#define MESSAGE_CMD_PARWRITEINT 	"#PWI"
#define MESSAGE_CMD_PARWRITEFLOAT 	"#PWF"
#define MESSAGE_CMD_PARRELOAD 		"#PLD"

#define GSMCALLDURATION  300000 //30000

#define UISTATE_ALARMLIST 1
#define UISTATE_INFO	2

#define KPD_UP		'A'
#define KPD_DOWN 	'B'
#define KPD_LEFT 	'#'
#define KPD_RIGHT 	'D'
#define KPD_ENTER 	'*'
#define KPD_ESC 	'0'

#include <avr/wdt.h>

//////////////////////////////////
// variables
//////////////////////////////////

unsigned long millisecondsPrev; //milliseconds;
bool secToggle = false;

char gsmStatus = ' ';
uint16_t gsmCode = 9999;
byte gsmMode = 0;
// 0 -> no GSM
// 1 -> receive SMS
// 2 -> call
// 3 -> send SMS

char gsmOperator[32];
char gsmTime[32];
byte gsmSignal = 0;
unsigned long gsmCountryNumber;
unsigned long gsmNumber;

char uiKeyPressed = 0;
int uiState, uiPage;
unsigned long uiKeyTime;

// inputs
float temperature;
float tempHighAlarm, tempLowAlarm;
float tempHysteresis = 0.5;

byte secondsCounter;

bool parseCmd(String &text, const char* cmd, byte &mode, const int address=0) {
	int pos = text.indexOf(cmd);
	if(pos > -1) {
		char ch = text.charAt(pos + strlen(cmd));
		if(ch=='A') mode = 0;
		else if(ch=='0') mode = 1;
		else if(ch=='1') mode = 2;
		if(address)
			OMEEPROM::write(address, mode);
		return true;
	}
	return false;
}

class Alarm {
	unsigned long timeActive;
	unsigned long timeDeactive;

 public:
	unsigned long alarmActiveDelay = 30000;
	unsigned long alarmDeactiveDelay = 30000;
	bool active;
	bool unAck;

	bool activate(bool state) {
		if(state) {
			if(!active) {
				if(!timeActive)
					timeActive = millis();
				if( alarmActiveDelay < (millis() - timeActive)) {
					active = true;
					unAck = true;
					timeActive = 0;
					return true;
				}
			}
		}
		else {
			timeActive = 0;
		}
		return false;
	};

	bool deactivate(bool state) {
		if(state){
			if(active) {
				if(!timeDeactive)
					timeDeactive = millis();
				if( alarmDeactiveDelay < (millis() - timeDeactive)) {
					active = false;
					timeDeactive = 0;
					return true;
				}
			}
		}
		else
			timeDeactive = 0;
		return false;
	}

	void ack() {
		unAck = false;
	}
};

Alarm tempHighAlarm2, tempLowAlarm2;

#include "OneWire.h"
#include "DallasTemperature.h"

OneWire oneWire(ONEWIREBUS_PIN);
DallasTemperature oneWireSensors(&oneWire);

#include <Wire.h>
#include "Sim800l.h"
#include <SoftwareSerial.h> //necessary for the library Sim800l

class Sim800l2 : public Sim800l {
public:
	bool Sim800l2::sendSmsBegin(char* number){
		printSerial("AT+CMGF=1\r"); // set sms to text mode
	    _buffer=_readSerial();
	    printSerial("AT+CMGS=\"");  // command to send sms
	    printSerial(number);
	    printSerial("\"\r");
	    _buffer=_readSerial();
	    return true;
	}

	/*
	template <typename TYPE> void sendSmsText(TYPE value)
	{
		//Serial.print(value);
		printSerial(value);
	}
	*/

	void Sim800l2::sendSmsTextLn(char* s) {
		printSerial(s);
		printSerial('\n');
	}
	void Sim800l2::sendSmsText(char* s) {
		printSerial(s);
	}
	void Sim800l2::sendSmsText(char ch) {
		printSerial(ch);
	}
	void Sim800l2::sendSmsText(float f) {
		printSerial(f);
	}
	void Sim800l2::sendSmsText(int i) {
		printSerial(i);
	}

	bool Sim800l2::sendSmsEnd(){
		printSerial("\r");
		delay(100);
		printSerial((char)26);
		_buffer=_readSerial();
		//expect CMGS:xxx   , where xxx is a number
		if (((_buffer.indexOf("CMGS") ) != -1 ) )
			return true;
		else
			return false;
	}
};

Sim800l2 sim800l;

class GsmManager {

public:
	Sim800l2* sim;
	unsigned long* gsmCountryNumber;
	unsigned long* gsmNumber;
	int* gsmCode;

	char gsmNumberChar[16];

	void gsmNumberCharCalc() {
		for(int i = 0; i < 16; i++)
			gsmNumberChar[i] = 0;

		char *p = gsmNumberChar;
		*p = '+';
		p += 1;
		ultoa(*gsmCountryNumber, p, 10);
		p += 3;
		ultoa(*gsmNumber, p, 10);
		//p += 9;
		//*p = 0;
		//Serial.println(gsmNumberChar);
	}

	unsigned long millisecondsCall;

	GsmManager(Sim800l2* sim_c, int* gsmCode_c, unsigned long* gsmCountryNumber_c, unsigned long* gsmNumber_c) { //char* gsmNumber_c) {
		sim = sim_c;
		gsmCode = gsmCode_c;
		gsmCountryNumber = gsmCountryNumber_c;
		gsmNumber = gsmNumber_c;
		//gsmNumber = gsmNumber_c;
	}

	void call(){
		if(!millisecondsCall && gsmMode > 1) {
			Serial.println(F("CALL"));
			gsmStatus = 'V';
			gsmNumberCharCalc();
			sim->callNumber(gsmNumberChar);
			millisecondsCall = millis(); //milliseconds;
		}
	}

	void updateCall() {
		if(millisecondsCall && (millis() - millisecondsCall > GSMCALLDURATION)){
			Serial.println(F("HANG"));
			sim->hangoffCall();
			millisecondsCall = 0;
			gsmStatus = ' ';
		}
	}

	bool sendSMS(char* text) {
		// TODO: check if SIM is busy
		wdt_reset();
		gsmNumberCharCalc();
		return sim->sendSms(gsmNumberChar, text);
		wdt_reset();
	}

	bool sendInfoSMS() {
		gsmNumberCharCalc();
		sim->sendSmsBegin(gsmNumberChar);
		sim->sendSmsText('\n');

		if(tempHighAlarm2.active) sim->sendSmsTextLn(MESSAGE_TEMPHIGH);
		if(tempLowAlarm2.active) sim->sendSmsTextLn(MESSAGE_TEMPLOW);

		sim->sendSmsText('\n');

		sim->sendSmsText(MESSAGE_TEMP);
		sim->sendSmsText(temperature);
		sim->sendSmsText('\n');
		sim->sendSmsText(MESSAGE_GSM);
		sim->sendSmsText(gsmMode);
		sim->sendSmsText("\n");

		char msg[MESSAGELENGTH + 1];
		for(int i = 0; i < min(MESSAGESCOUNT, 5); i++) {
			sim->sendSmsText("\n");
			readMessage(i, (byte*)msg);
			sim->sendSmsText(msg);
		}

		wdt_reset();
		sim->sendSmsEnd();
		return true;
	}
	/*
	char getStatus() {
		//sim->signalQuality();
		uint8_t iGsmStatus = sim->getCallStatus();
		Serial.println(iGsmStatus);
		if(iGsmStatus ==  3)
			return 'R';
		else if (iGsmStatus ==  4) {
			//sim->hangoffCall();
			//millisecondsCall = 0;
			return 'C';
		}
		return ' ';
		//sim->dateNet();
	}
	*/
	bool proccedSMS() {
		//Serial.println(F("proccedSMS begin"));

		wdt_reset();
		if(sim->getCallStatus())
			return false;

		wdt_reset();
		millisecondsCall = 0;
		gsmStatus = ' ';

		//if(callDuration)
		//if(!gsmMode)
		//	return false;
		//Serial.println('R');

		String text = sim->readSms(SMSINDEX);
		wdt_reset();
		Serial.println(text);

		if(text != "") {
			sim->delAllSms();

			if(gsmMode) {
				int pos;
				pos = text.indexOf("#P");
				if(text.substring(pos+2, pos+6) == String(*gsmCode, DEC)) {
					//Serial.println(F("CODE OK"));
					pos = text.indexOf("#M");
					if(pos > -1) {
						gsmMode = text.charAt(pos + 2) - 48;
						OMEEPROM::write(GSMMODE_ADDR, gsmMode);
					}
					pos = text.indexOf(MESSAGE_CMD_REQUEST);
					if(pos > -1) {
						sendInfoSMS();
					}
				}
			}
		}

		//Serial.println(F("proccedSMS end"));
		return false;
	}

	bool proccedGSM() {
		//Serial.println(F("proccedGSM begin"));
		//Serial.println(sim800l.getReg());

		wdt_reset();
		int gsmDay, gsmMonth, gsmYear, gsmHour, gsmMinute, gsmSecond;
		gsmYear = 0;
		//sim800l.RTCtime(&gsmDay,  &gsmMonth, &gsmYear, &gsmHour, &gsmMinute, &gsmSecond);
		sim800l.RTCtime(&gsmDay,  &gsmMonth, &gsmYear, &gsmHour, &gsmMinute, &gsmSecond).toCharArray(gsmTime, 32);
		//if(gsmYear > 0) {
			rtc.stopRTC(); //stop the RTC
			rtc.setTime(gsmHour,gsmMinute,gsmSecond); //set the time here
			rtc.setDate(gsmDay,gsmMonth,gsmYear); //set the date here
			rtc.startRTC(); //start the RTC
		//}
		wdt_reset();
		sim800l.getOperator().toCharArray(gsmOperator, 32);

		wdt_reset();
		gsmSignal = 0;
		gsmSignal = sim800l.getSignal().toInt();

		wdt_reset();

		//Serial.println(F("proccedGSM end"));
		return false;
	}
} gsmMgr(&sim800l, &gsmCode, &gsmCountryNumber, &gsmNumber);

// LCD i2c
#include "LiquidCrystal_I2C.h"
LiquidCrystal_I2C lcd(LCD_I2CADDR, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);

// Keypad 4x4 i2c
#include "Keypad_I2C.h"
#include "Keypad.h"

class Keypad_I2C2 : public Keypad_I2C {
	unsigned long kTime;

public:
    Keypad_I2C2(char *userKeymap, byte *row, byte *col, byte numRows, byte numCols, byte address, byte width = 1) : Keypad_I2C(userKeymap, row, col, numRows, numCols, address, width) {
    };

    char Keypad_I2C2::getRawKey() {

    	getKeys();

      	if(bitMap[3] == 1) return '*';
        if(bitMap[3] == 2) return '0';
        if(bitMap[3] == 4) return '#';
        if(bitMap[3] == 8) return 'D';

        if(bitMap[2] == 1) return '7';
        if(bitMap[2] == 2) return '8';
        if(bitMap[2] == 4) return '9';
        if(bitMap[2] == 8) return 'C';

        if(bitMap[1] == 1) return '4';
        if(bitMap[1] == 2) return '5';
        if(bitMap[1] == 4) return '6';
        if(bitMap[1] == 8) return 'B';

        if(bitMap[0] == 1) return '1';
        if(bitMap[0] == 2) return '2';
        if(bitMap[0] == 4) return '3';
        if(bitMap[0] == 8) return 'A';

        return NO_KEY;
    };

    char Keypad_I2C2::getKey2() {

    	getKeys();

    	if(bitMap[0] & 1) {
    	    		if(bitMap[0] & 8) {
    	    			lcd.begin(LCD_COLS, LCD_ROWS);
    	    			lcd.print(TEXT_GROWMATEASY);
    	    			delay(1000);
    	    			//1 + A
    	    		}
    	    		if(bitMap[0] & 4) {
    	    			//TODO watch dog test
    	    			lcd.clear();
    	    			lcd.print(F("WATCHDOG TEST"));
    	    			while(true) {};
    	    			//1 + 3
    	    		}
    	}

    	//TODO !!! Dirty trick !!!
    	if(bitMap[1] & 1) {
    	}

    	if(bitMap[0] || bitMap[1] || bitMap[2] || bitMap[3]) {
    		if(!kTime) {
    			kTime = millis();
    		}
    		if((kTime + 500) > millis()){
    			if((kTime + 200) < millis()) {
    				return NO_KEY;
    			}
    		}
    	}
        else
        	kTime = 0;

    	return getRawKey();
    }
};

byte rowPins[KPD_ROWS] = {0, 1, 2, 3}; //connect to the row pinouts of the keypad
byte colPins[KPD_COLS] = {4, 5, 6, 7}; //connect to the column pinouts of the keypad
Keypad_I2C2 kpd( makeKeymap(keys), rowPins, colPins, KPD_ROWS, KPD_COLS, KPD_I2CADDR, PCF8574 );

class OMMenuMgr2 : public OMMenuMgr {

public:

    OMMenuMgr2(const OMMenuItem* c_first, uint8_t c_type, Keypad_I2C2* c_kpd) :OMMenuMgr( c_first, c_type) {
      kpd = c_kpd;
    };

    int OMMenuMgr2::_checkDigital() {
    	char k = kpd->getKey2();

    	if(k == 'A') return BUTTON_INCREASE;
    	if(k == 'B') return BUTTON_DECREASE;
    	if(k == 'D') return BUTTON_FORWARD;
    	if(k == '#') return BUTTON_BACK;
    	if(k == '*') return BUTTON_SELECT;

    	return k;
    	return BUTTON_NONE;
    }

private:

    Keypad_I2C2* kpd;
};

MENU_VALUE tempHighAlarm_value={ TYPE_FLOAT_10, 999,    -999,    MENU_TARGET(&tempHighAlarm), TEMPHIGHALARM_ADDR };
MENU_ITEM tempHighAlarm_item  ={ {"TEPL VYSOKA [C]"},    ITEM_VALUE,  0,        MENU_TARGET(&tempHighAlarm_value) };
MENU_VALUE tempLowAlarm_value ={ TYPE_FLOAT_10, 999,    -999,    MENU_TARGET(&tempLowAlarm), TEMPLOWALARM_ADDR };
MENU_ITEM tempLowAlarm_item   ={ {"TEPL NIZKA  [C]"},    ITEM_VALUE,  0,        MENU_TARGET(&tempLowAlarm_value) };
MENU_LIST const submenu_alarms[] = { &tempHighAlarm_item, &tempLowAlarm_item,};
MENU_ITEM menu_alarms = { {"VYSTR NASTAV->"},  ITEM_MENU,  MENU_SIZE(submenu_alarms),  MENU_TARGET(&submenu_alarms) };

MENU_VALUE gsmMode_value= { TYPE_BYTE, 3, 0,    MENU_TARGET(&gsmMode), GSMMODE_ADDR };
MENU_ITEM gsmMode_item   ={ {"GSM REZIM"},    ITEM_VALUE,  0,        MENU_TARGET(&gsmMode_value) };
MENU_VALUE gsmCode_value= { TYPE_UINT, 9999, 1000,    MENU_TARGET(&gsmCode), GSMCODE_ADDR };
MENU_ITEM gsmCode_item   ={ {"GSM KOD"},    ITEM_VALUE,  0,        MENU_TARGET(&gsmCode_value) };
//MENU_ITEM gsmNumber_item   = { {"GSM NUMBER"},  ITEM_ACTION, 0,        MENU_TARGET(&uiSetGsmNumber) };
MENU_VALUE gsmCountryNumber_value= { TYPE_ULONG, 0, 0,    MENU_TARGET(&gsmCountryNumber), GSMCOUNTRYNUMBER_ADDR };
MENU_ITEM gsmCountryNumber_item   ={ {"GSM PREDVOLBA +"},    ITEM_VALUE,  0,        MENU_TARGET(&gsmCountryNumber_value) };
MENU_VALUE gsmNumber_value= { TYPE_ULONG, 0, 0,    MENU_TARGET(&gsmNumber), GSMNUMBER_ADDR };
MENU_ITEM gsmNumber_item   ={ {"GSM TEL CISLO"},    ITEM_VALUE,  0,        MENU_TARGET(&gsmNumber_value) };

MENU_LIST const submenu_gsm[] = { &gsmMode_item, &gsmCode_item, &gsmCountryNumber_item, &gsmNumber_item};
MENU_ITEM menu_gsm = { {"GSM->"},  ITEM_MENU,  MENU_SIZE(submenu_gsm),  MENU_TARGET(&submenu_gsm) };

MENU_ITEM item_setClock   = { {"TEST VOLANI!"},  ITEM_ACTION, 0,        MENU_TARGET(&uiTestCall) };
//MENU_ITEM item_alarmList   = { {"ALARM LIST->"},  ITEM_ACTION, 0,        MENU_TARGET(&uiAlarmList) };
MENU_ITEM item_reset   = { {"VYCHOZI NASTAV!"},  ITEM_ACTION, 0,        MENU_TARGET(&uiResetAction) };
//MENU_ITEM item_info   = { {"INFO->"},  ITEM_ACTION, 0,        MENU_TARGET(&uiInfo) };

MENU_LIST const root_list[]   = { &menu_alarms, &item_setClock, &menu_gsm, &item_reset};
MENU_ITEM menu_root     = { {"Root"},        ITEM_MENU,   MENU_SIZE(root_list),    MENU_TARGET(&root_list) };

OMMenuMgr2 Menu(&menu_root, MENU_DIGITAL, &kpd);

int saveMessage(char msg[], char status, bool gsm = true) {
	if((gsmNumber != 0) && status == MESSAGE_ALARM_ON && gsm) {
		if(gsmMode == 3)
			gsmMgr.sendInfoSMS();
		else
			gsmMgr.call();
	}

	char* p = msg;
	if(rtc.getDay() < 10)
	   itoa(0, p++, 10);
	itoa(rtc.getDay(), p, 10);
	msg[2]='/';

	p = msg + 3;
	if(rtc.getMonth() < 10)
	   itoa(0, p++, 10);
	itoa(rtc.getMonth(), p, 10);
	msg[5]=' ';
	p = msg + 6;
	if(rtc.getHours() < 10)
	   itoa(0, p++, 10);
	itoa(rtc.getHours(), p, 10);
	msg[8]=':';
	p = msg + 9;
	if(rtc.getMinutes() < 10)
	   itoa(0, p++, 10);
	itoa(rtc.getMinutes(), p, 10);

	msg[11]= ' ';
    msg[15] = status;

    int offset;
    OMEEPROM::read(MESSAGESOFFSET_ADDR, offset);
    if(offset >= MESSAGESCOUNT)
      offset = 0;
    for(int i=0; i < MESSAGELENGTH; i++) {
      OMEEPROM::write(MESSAGES_ADDR + offset * MESSAGELENGTH + i, msg[i]);
    }
    offset = (offset + 1) %  MESSAGESCOUNT;
    OMEEPROM::write(MESSAGESOFFSET_ADDR, offset);

    return offset;
}

void readMessage(int index, byte* msg) {
    int offset;
    OMEEPROM::read(MESSAGESOFFSET_ADDR, offset);
    offset--;
    if(offset >= MESSAGESCOUNT)
      offset = 0;
    offset = (offset + (MESSAGESCOUNT -index )) % MESSAGESCOUNT;
    for(int i=0; i < MESSAGELENGTH; i++) {
      OMEEPROM::read(MESSAGES_ADDR + offset * MESSAGELENGTH + i, *(msg+i));
    }
    *(msg+MESSAGELENGTH) = 0;
}

/*
void serialPrintParInt(int address)
{
	int val;
	OMEEPROM::read(address, val);
	Serial.print(val);
	Serial.println();
	Serial.println();
}

void serialPrintParFloat(int address)
{
	float val;
	OMEEPROM::read(address, val);
	Serial.println(val);
	Serial.println();
	Serial.println();
}
*/

void loadEEPROM() {
    OMEEPROM::read(TEMPHIGHALARM_ADDR, tempHighAlarm);
    OMEEPROM::read(TEMPLOWALARM_ADDR, tempLowAlarm);
    OMEEPROM::read(GSMMODE_ADDR, gsmMode);
    OMEEPROM::read(GSMCODE_ADDR, gsmCode);
    OMEEPROM::read(GSMCOUNTRYNUMBER_ADDR, gsmCountryNumber);
    OMEEPROM::read(GSMNUMBER_ADDR, gsmNumber);
}

void saveDefaultEEPROM() {
	//save defaults
    tempHighAlarm = 100.0;
    tempLowAlarm = 0.0;
    OMEEPROM::write(TEMPHIGHALARM_ADDR, tempHighAlarm);
    OMEEPROM::write(TEMPLOWALARM_ADDR, tempLowAlarm);

    gsmMode = 2;
    OMEEPROM::write(GSMMODE_ADDR, gsmMode);
    //gsmCountryNumber = 420;
    //gsmNumber = 0;
    //OMEEPROM::write(GSMCOUNTRYNUMBER_ADDR, gsmCountryNumber);
    //OMEEPROM::write(GSMNUMBER_ADDR, gsmNumber);
}

//////////////////////////////////
// setup
//////////////////////////////////
void setup() {
	wdt_enable(WDTO_8S);
	wdt_reset();

	pinMode(SIM800LRESET_PIN, OUTPUT);
	sim800l.resetHW();
	wdt_reset();

	pinMode(LED_PIN, OUTPUT);

	Serial.begin(115200);
	while(!Serial);
	Serial.println(TEXT_GROWMATEASY);

	oneWireSensors.begin();
	oneWireSensors.requestTemperatures();
	temperature = oneWireSensors.getTempCByIndex(0);

	wdt_reset();

	Wire.begin( );
	sim800l.begin();
	kpd.begin( makeKeymap(keys) );
	lcd.begin(LCD_COLS, LCD_ROWS);

/*
	sim800l.reset();
	while (strcmp(gsmNumber, "Call READY") > 0) {
		sim800l.readSerial().toCharArray(gsmNumber, 16);
		wdt_reset();
		lcd.setCursor(0, 0);
		lcd.print(gsmNumber);
		uiLcdPrintSpaces8();
		uiLcdPrintSpaces8();
	}
*/

	//wdt_reset();
	//sim800l.reset();
	//wdt_reset();

	lcd.setCursor(0, 0);
	lcd.print(TEXT_GROWMATEASY);
	lcd.setCursor(0, 1);
	lcd.print(F("START GSM "));
	//while(!gsmMgr.proccedGSM()) {
	for(int i = 59; i >= 0; i--) {
		if(i == 45)
			sim800l.enableRTC();
		if(i == 40)
			sim800l.resetHW();
		else
			delay(1000);

		lcd.setCursor(10, 1);
		if(i < 10)
			lcd.print('0');
		lcd.print(i);

		wdt_reset();
	}
	gsmMgr.proccedGSM();

	secondsCounter = 0;

	if( OMEEPROM::saved() )
		loadEEPROM();
	else
		saveDefaultEEPROM();

	//milliseconds = millis();

	saveMessage(MESSAGE_ALARM_POWERON, MESSAGE_ALARM_ON, false);

	uiMain();
	Menu.setDrawHandler(uiDraw);
	Menu.setExitHandler(uiMain);
	Menu.enable(true);
}

double analogRead(int pin, int samples){
  int result = 0;
  for(int i=0; i<samples; i++){
    result += analogRead(pin);
  }
  return (double)(result / samples);
}

//////////////////////////////////
// main loop
//////////////////////////////////
void loop() {
	//Serial.println(millis());



	wdt_reset();

	if(millis() - millisecondsPrev > 1000) {
		millisecondsPrev = millis();
		secondsCounter++;

		wdt_reset();
		if (secondsCounter % 60 == 0 && !Menu.shown()) {
			gsmMgr.proccedGSM();
		}
		wdt_reset();
		if((secondsCounter % 60 == 0) && gsmMode > 0) {
			gsmMgr.proccedSMS();
		}
		wdt_reset();

		if (secondsCounter % 10 == 0) {
			oneWireSensors.requestTemperatures();
			temperature = oneWireSensors.getTempCByIndex(0);
		}

		secToggle ? secToggle = false : secToggle = true;
	}

	if (!Menu.shown()) {
		if(!uiState)
			uiMain();
	}
	char key = kpd.getKey2();
	if(key == '#') {
			uiState = 0;
			uiPage = 0;
			Menu.enable(true);
	}
	else if(!Menu.shown() || !Menu.enable()) {
		if(key == '7') {
			uiAlarmList();
		}
		else if(key == '8') {
			uiInfo();
		}
		else {
			uiScreen();
		}
	}
	Menu.checkInput();
	wdt_reset();

	//////////////////////////////////
	// gsm
	//////////////////////////////////
	gsmMgr.updateCall();
	wdt_reset();

	//////////////////////////////////
	// alarms
	//////////////////////////////////
	if(tempHighAlarm2.activate(temperature > tempHighAlarm))
		saveMessage(MESSAGE_ALARM_TEMPHIGH, MESSAGE_ALARM_ON);
	if(tempHighAlarm2.deactivate(temperature < (tempHighAlarm - tempHysteresis)))
		saveMessage(MESSAGE_ALARM_TEMPHIGH, MESSAGE_ALARM_OFF);

	if(tempLowAlarm2.activate(temperature < tempLowAlarm))
		saveMessage(MESSAGE_ALARM_TEMPLOW, MESSAGE_ALARM_ON);
	if(tempLowAlarm2.deactivate(temperature > (tempLowAlarm + tempHysteresis)))
		saveMessage(MESSAGE_ALARM_TEMPLOW, MESSAGE_ALARM_OFF);

	if(kpd.getRawKey()) {
		tempHighAlarm2.ack();
		tempLowAlarm2.ack();
	}

	//////////////////////////////////
	// communication
	//////////////////////////////////
  	if (Serial.available() > 0) {
  		String text = Serial.readString();
  		int pos;
  		//Serial.println(text);

/*
  		pos = text.indexOf(MESSAGE_CMD_PARREADINT);
  		if (pos >= 0) {
  			serialPrintParInt(text.substring(pos + strlen(MESSAGE_CMD_PARREADINT)).toInt());
  		}
  		pos = text.indexOf(MESSAGE_CMD_PARREADFLOAT);
  		if (pos >= 0) {
  			serialPrintParFloat(text.substring(pos + strlen(MESSAGE_CMD_PARREADFLOAT)).toFloat());
  		}
  		pos = text.indexOf(MESSAGE_CMD_PARWRITEINT);
  		if (pos >= 0) {
  			int address = text.substring(pos + strlen(MESSAGE_CMD_PARWRITEINT)).toInt();
  			//#PWI0125:25
  			int value = text.substring(pos + strlen(MESSAGE_CMD_PARWRITEINT) + 5).toInt();
  			OMEEPROM::write(address, value);
  		}
  		pos = text.indexOf(MESSAGE_CMD_PARWRITEFLOAT);
  		if (pos >= 0) {
  			int address = text.substring(pos + strlen(MESSAGE_CMD_PARWRITEINT)).toInt();
  			//#PWI0125:25
  			float value = text.substring(pos + strlen(MESSAGE_CMD_PARWRITEINT) + 5).toFloat();
  			OMEEPROM::write(address, value);
  		}
  		pos = text.indexOf(MESSAGE_CMD_PARRELOAD);
  		if (pos >= 0) {
  			loadEEPROM();
  		}
*/
		if (text.indexOf(MESSAGE_CMD_REQUEST)!=-1 ) {
			Serial.println();

  			Serial.print(MESSAGE_TEMP);
  			Serial.println(temperature);
  			Serial.print(MESSAGE_GSM);
  			Serial.print(gsmMode);
  			Serial.print(' ');
  			Serial.print('+');
  			Serial.print(gsmCountryNumber);
  			Serial.print(gsmNumber);
  			Serial.print(' ');
  			Serial.print(gsmCode);

  			Serial.println();
  			Serial.println();

  			Serial.println();
  			char msg[MESSAGELENGTH + 1];
  			for(int i = 0; i< MESSAGESCOUNT; i++) {
  				readMessage(i, (byte*)msg);
  				Serial.println(msg);
  			}

  			Serial.println();
  			Serial.println();
   		}
  	}
}

void uiResetAction() {
	saveDefaultEEPROM();
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print(F("VYCH NASTAV OK"));
	delay(1000);
}

void uiDraw(char* p_text, int p_row, int p_col, int len) {
	lcd.backlight();
	lcd.setCursor(p_col, p_row);
	for( int i = 0; i < len; i++ ) {
		if( p_text[i] < '!' || p_text[i] > '~' )
			lcd.write(' ');
		else
			lcd.write(p_text[i]);
	}
}

void uiAlarmList() {
	lcd.backlight();
	Menu.enable(false);
	uiState = UISTATE_ALARMLIST;
	uiPage=0;
	uiKeyTime =0;
	uiKeyPressed = 0;
	lcd.clear();
}

void uiInfo() {
	lcd.backlight();
	Menu.enable(false);
	uiState = UISTATE_INFO;
	uiPage=0;
	uiKeyTime =0;
	uiKeyPressed = 0;
	lcd.clear();
}

void uiTestCall() {
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print(F("VOLANI..."));
	gsmMgr.call();
	delay(1000);
}

void uiScreen() {
	char key = kpd.getRawKey();
	//first key stroke after delay, then pause and then continuously
	if(key) {
		 if(!uiKeyTime) {
			 uiKeyTime = millis();
		 }
		 if(millis() - uiKeyTime < 120) {
			 key = 0;
		 }
		 else {
			 if(uiKeyPressed) {
				 key = 0;
			 }
			 else {
				 uiKeyPressed = key;
			 }
		 }
		 if(millis() - uiKeyTime < 600) {
			 uiKeyPressed = 0;
		 }
	}
	else {
		uiKeyTime = 0;
		uiKeyPressed = 0;
	}

	if(uiState == UISTATE_INFO) {
		if(key == KPD_UP)
			uiPage--;
		if(key == KPD_DOWN)
			uiPage++;
		uiPage = min(uiPage, 2);
		uiPage = max(uiPage, 0);

		if(uiPage == 0) {
			lcd.setCursor(0, 0);
			lcd.print(gsmOperator);
			uiLcdPrintSpaces8();
			uiLcdPrintSpaces8();
			lcd.setCursor(0, 1);
			lcd.print(gsmOperator + 16);
			uiLcdPrintSpaces8();
			uiLcdPrintSpaces8();
		}

		if(uiPage == 1) {
			lcd.setCursor(0, 0);
			lcd.print(gsmTime);
			uiLcdPrintSpaces8();
			uiLcdPrintSpaces8();
			lcd.setCursor(0, 1);
			lcd.print(gsmTime + 16);
			uiLcdPrintSpaces8();
			uiLcdPrintSpaces8();
		}
	}

	if(uiState == UISTATE_ALARMLIST) {
		if(key == KPD_UP)
			uiPage--;
		if(key == KPD_DOWN)
			uiPage++;

		char msg[MESSAGELENGTH + 1];
		uiPage = min(uiPage, MESSAGESCOUNT - 2);
		uiPage = max(uiPage, 0);
		lcd.setCursor(0, 0);
		readMessage(uiPage, (byte*)msg);
		lcd.print(msg);
		lcd.setCursor(0, 1);
		readMessage(uiPage + 1, (byte*)msg);
		lcd.print(msg);
	}
}

void uiLcdPrintAlarm(bool alarmHigh, bool alarmLow) {
	if(alarmHigh)
		lcd.print('+');
	else if(alarmLow)
		lcd.print('-');
	else
		lcd.print(' ');
}

void uiLcdPrintSpaces8() {
	lcd.print(F("        "));
}

void uiMain() {
	lcd.setCursor(0, 0);
	if(rtc.getHours() < 10)
		lcd.print('0');
	lcd.print(rtc.getHours());
	if(secToggle)
		lcd.print(':');
	else
		lcd.print(' ');
	if(rtc.getMinutes() < 10)
		lcd.print('0');
	lcd.print(rtc.getMinutes());
	lcd.print(' ');

	if(tempHighAlarm2.unAck || tempLowAlarm2.unAck) {
		secToggle ? lcd.backlight() : lcd.noBacklight();
	}
	else {
		lcd.backlight();
	}

	lcd.setCursor(6, 0);
	if(tempHighAlarm2.active) {
		lcd.print(MESSAGE_TEMPHIGH);
	}
	else if(tempLowAlarm2.active) {
		lcd.print(MESSAGE_TEMPLOW);
	}
	else
		lcd.print(MESSAGE_EMPTY);
	lcd.print(' ');
	lcd.print(' ');

	lcd.setCursor(8, 0);
	lcd.print(' ');

	if(abs(temperature) < 100)
		lcd.print(' ');
	if(abs(temperature) < 10)
		lcd.print(' ');
	if(temperature > 0)
		lcd.print('+');
	lcd.print(temperature);

	lcd.setCursor(0, 1);
	if(gsmMgr.sim->na)
		lcd.print('!');
	else
		lcd.print(gsmStatus);
	uiLcdPrintSpaces8();
	lcd.setCursor(2, 1);
	if(gsmSignal * 3 < 10)
		lcd.print(' ');
	lcd.print(gsmSignal * 3);
	lcd.print('%');
	lcd.setCursor(6, 1);
	lcd.print(gsmOperator);

	uiLcdPrintSpaces8();
	uiLcdPrintSpaces8();
}
