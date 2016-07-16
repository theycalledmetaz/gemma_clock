/* Gemma Clock: The Next Generation
    by Aaron Brodney

    Designed for Adafruit Feather Huzzah (ESP8266) w/DS3231 RTC and
    Adafruit 16x8 LCD matrix displays (ie: part 2035)

    Began July 1 2016
    v1.0.00 completed July 8 2016
    v1.0.1 completed July 9 2016
*/

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include "Sodaq_DS3231.h"  // DS3231 RTC support w/temperature and DateTime
#include "Adafruit_LEDBackpack.h"
#include "Adafruit_GFX.h"
#include <Adafruit_SSD1306.h>
#include <Adafruit_FeatherOLED.h>
#include <EEPROM.h>  // use EEPROM in order to store settings
#include <Adafruit_NeoPixel.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP085_U.h>

// temp sensor
Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085);

// setup display using PIN 12
// functions as status indicator
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(1, 12, NEO_RGB + NEO_KHZ800);

Adafruit_8x16matrix matrix = Adafruit_8x16matrix(); // hour
Adafruit_8x16matrix matrix2 = Adafruit_8x16matrix(); // minute disp


Adafruit_SSD1306 display = Adafruit_SSD1306();  // the OLED display

const char ssid[] = "SSID-Here";
const char pass[] = "passw0rd";

const char *versionStr = "1.0.2";
const char *buildDateStr = __DATE__;

const char *ntpServerName = "pool.ntp.org";

const int displayClockSecs = 25;
const int displayTempSecs = 5;

double timezone = -4.0;  // we're defaulting to EDT since that's where it is now

#define TRUE 1
#define FALSE 0

#define DISPLAY_MODE_NORMAL 0
#define DISPLAY_MODE_TEMP 1

#define OLED_MODE_NORMAL 0
#define OLED_MODE_TEMP 1

// button definitions for feather OLED shield
#define BUTTON_A 0
#define BUTTON_B 16
#define BUTTON_C 2
// #define BUTTON_X 14  // big front/top button

#define STATUS_GREEN 0
#define STATUS_RED 1
#define STATUS_ORANGE 2
#define STATUS_PURPLE 3

// is daylight savings time on?
boolean dstOn = 0;

boolean wifiError = FALSE;   // is there a problem with just wifi?
boolean criticalError = FALSE;  // has something like the RTC broken?
boolean tempError = FALSE; // temp sensor

int oledMode = OLED_MODE_NORMAL;

int displayMode = DISPLAY_MODE_NORMAL;

int modeCount = 0;

int dstMode = 0;

long syncFailures = 0;

int wifiCount = 0;

unsigned long lastRefresh = 0;

boolean lockOledDisplay = 0;  // make the message on the screen immutable (for errors and shit)

// unsigned long lastScreenRefresh = 0; // track separately when we last updated the main screens
// the idea is to try to avoid unnecessary i2c traffic

unsigned long lastNtpSync = 0;

unsigned long lastButtonCheck = 0;

unsigned long lastButtonPress = 0;

unsigned long buttonPressTime = 0;

unsigned long longPressAt = 0;

int buttonPreviousState = 0;

unsigned long altDisplayEpoch = 0;

long pollNtpMinutes = 10; // how often (in minutes) to poll NTP

char weekDay[][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
char weekDayFull[][10] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
char monthsFull[][10] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
char months[][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

const unsigned int localPort = 2390;

const int NTP_PACKET_SIZE = 48;

byte packetBuffer[ NTP_PACKET_SIZE];

IPAddress timeServerIP;

WiFiUDP udp;

static const uint8_t
colon_bmp[] = {
  B00000000,
  B00000110,
  B00000110,
  B00000000,
  B00000110,
  B00000110,
  B00000000,
  B00000000
}
,
pacman_bmp[] = {
  B00111100,
  B01111110,
  B11011110,
  B11111100,
  B11111000,
  B11111100,
  B01111110,
  B00111100,
},
heart_bmp[] = {
  B00000000,
  B01100110,
  B11111111,
  B11111111,
  B01111110,
  B00111100,
  B00011000,
  B00000000,
},
slash_bmp[] = {
  B00000001,
  B00000010,
  B00000100,
  B00001000,
  B00010000,
  B00100000,
  B00000000,
  B00000000,
},
solid_bmp[] = {
  B11111111,
  B11111111,
  B11111111,
  B11111111,
  B11111111,
  B11111111,
  B11111111,
  B11111111,
},
degree_bmp[] = {
  B00000111,
  B00000101,
  B00000111,
  B00000000,
  B00000000,
  B00000000,
  B00000000,
  B00000000,
};

static const uint8_t
smile_bmp[] =
{
  B00111100,
  B01000010,
  B10100101,
  B10000001,
  B10100101,
  B10011001,
  B01000010,
  B00111100
},
neutral_bmp[] =
{ B00111100,
  B01000010,
  B10100101,
  B10000001,
  B10111101,
  B10000001,
  B01000010,
  B00111100
},
frown_bmp[] =
{ B00111100,
  B01000010,
  B10100101,
  B10000001,
  B10011001,
  B10100101,
  B01000010,
  B00111100
};

void displaySensorDetails(void)
{
  float temperature = 0.0;
  bmp.getTemperature(&temperature);
  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" C");
}


float to_F(float input)
{
  return ( input * 1.80 ) + 32.0;
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress *address)
{
  Serial.println("sending NTP packet...");

  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);

  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(*address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}


static inline void adjust(void)
{
  DateTime now = rtc.now();  // establish the current time
  uint32_t rtcTime = now.getEpoch();
  uint32_t delta = 0;

  unsigned long mi;
  int cb = 0;

  //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP);

  // send an NTP packet to a time server
  sendNTPpacket(&timeServerIP);

  mi = millis();

  //delay(1000);

  // cb = udp.parsePacket();

  // wait to see if a reply is available
  while (millis() - mi < 1000 && !cb)
  {
    cb = udp.parsePacket();
    yield();
  }

  if (!cb)
  {
    Serial.println("[ERROR]: No packet available.");

    syncFailures++; // increment sync failures so we can keep track of failures

    return;
  }

  //Serial.print("packet received, length=");
  //Serial.println(cb);
  // We've received a packet, read the data from it
  udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

  //the timestamp starts at byte 40 of the received packet and is four bytes,
  // or two words, long. First, esxtract the two words:

  unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
  unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
  // combine the four bytes (two words) into a long integer
  // this is NTP time (seconds since Jan 1 1900):
  unsigned long secsSince1900 = highWord << 16 | lowWord;
  //Serial.print("Seconds since Jan 1 1900 = " );
  //Serial.println(secsSince1900);

  // now convert NTP time into everyday time:
  // Serial.print("Unix UTC time = ");
  // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
  const unsigned long seventyYears = 2208988800UL;
  // subtract seventy years:
  unsigned long epoch = secsSince1900 - seventyYears;
  // print Unix time:
  Serial.println("NTP SERVER UNIX TIME: ");
  Serial.println(epoch);

  // adjust to user timezone
  epoch += timezone * 3600;

  delta = epoch - rtcTime;

  rtc.setEpoch(epoch);

  Serial.println("TIME SET! DELTA: ");
  Serial.println( delta );
  return;
}

void rtcFailure()
{
  display.clearDisplay();

  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("RTC FAIL!");

  display.display();
}

void printDate()
{
  DateTime now = rtc.now();

  Serial.print(now.year(), DEC);
  Serial.print('/');
  Serial.print(now.month(), DEC);
  Serial.print('/');
  Serial.print(now.date(), DEC);
  Serial.print(' ');
  Serial.print(now.hour(), DEC);
  Serial.print(':');
  Serial.print(now.minute(), DEC);
  Serial.print(':');
  Serial.print(now.second(), DEC);
  Serial.println();

  Serial.print("(UNIX TIME) = ");
  Serial.print(now.getEpoch());
  Serial.println();
}


// for hard failures
void statusRed()
{
  pixels.setPixelColor(0, pixels.Color(50, 0, 0));
  pixels.show();
}

// normal running status

void statusGreen()
{
  pixels.setPixelColor(0, pixels.Color(0, 50, 0));
  pixels.show();
}

// error like no wifi connectivity
void statusPurple()
{
  pixels.setPixelColor(0, pixels.Color(50, 0, 50));
  pixels.show();
}

void statusOrange()
{
  pixels.setPixelColor(0, pixels.Color(255, 153, 0));
  pixels.show();
}


// read saved settings from EEPROM such as DST
void readSettings()
{

  // read the DST setting from the first block of EEPROM
  // that is all... for now
  int reading = EEPROM.read(0);

  Serial.println("EEPROM: ");
  Serial.println(reading, DEC);

  if ( reading == 0 )
  {
    timezone = -5.0;
    dstOn = 0;
  } else {
    timezone = -4.0;
    dstOn = 1;
  }

  Serial.println("TIMEZONE SET: ");
  Serial.println(timezone);
}

// 1 for ON (GMT-4) EDT or 0 for OFF (GMT-5) EST
void setDst()
{
  EEPROM.write(0, dstOn);
  EEPROM.commit();
  EEPROM.end();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Gemma Clock TNG");
  Serial.println("By Aaron Brodney");

  Serial.print("VERSION: ");
  Serial.println(versionStr);

  Serial.println("BUILD");
  Serial.println(__DATE__);
  Serial.println(__TIME__);

  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  pinMode(BUTTON_C, INPUT_PULLUP);
  // additional hard button (intended for the front/top of the clock)
  // pinMode(BUTTON_X, INPUT_PULLUP);

  pixels.begin();
  statusRed();  // show status red during boot

  EEPROM.begin(4);  // begin EEPROM access with 4 byte size
  readSettings();

  Wire.begin();
  Wire.setClockStretchLimit(2000);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  display.clearDisplay();
  display.display();

  display.clearDisplay();

  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("CLOCK BOOT");
  display.print("WAIT...");
  display.display();

  matrix.begin(0x71);
  matrix2.begin(0x70);

  matrix.clear();
  matrix2.clear();

  matrix.setBrightness(1);
  matrix2.setBrightness(1);

  matrix2.writeDisplay();
  matrix.writeDisplay();

  if ( ! bmp.begin() )
  {
    Serial.print("BMP180 not detected");
    tempError = TRUE;
  }

  if ( ! rtc.begin() )
  {
    Serial.println("Unable to contact RTC");

    frowneys();

    rtcFailure(); // trigger an OLED display message to indicate RTC failure

    // errorCondition = 1;

    criticalError = TRUE;

    statusRed();

    while (1); // hang here since we're a clock and need an RTC to function

  } else {
    hearts();
  }

  WiFi.begin(ssid, pass);

  while ( WiFi.status() != WL_CONNECTED )
  {
    delay(500);
    Serial.print(".");
    wifiCount++;

    if ( wifiCount > 20 ) // at .5 sec per cyce this is 10 sec
    {
      // proceed with wifi unavailable
      // frowneys();

      // errorCondition = 2;
      // delay(5000);

      wifiError = TRUE;

      break;
    }
  }

  Serial.println("");

  Serial.println("My Wi-Fi IP is: ");
  Serial.println(WiFi.localIP());

  // Start UDP Server to receive the NTP response.
  Serial.println("Starting UDP");
  udp.begin(localPort);

  Serial.println("Syncing NTP");
  adjust();
  Serial.print("Local port: ");
  Serial.println(udp.localPort());
  // boot process is complete.. clear the screens for normal operation

  matrix.clear();
  matrix2.clear();

  matrix.writeDisplay();
  matrix2.writeDisplay();

  // clear and flush the display
  display.clearDisplay();
  display.display();

  // Serial.print("ERROR CONDITION: ");
  // Serial.println( errorCondition );

  updateStatusLed();
}

void updateStatusLed()
{
  if ( checkWifiStatus() )
  {
    statusGreen();
  } else {
    statusPurple();
    wifiError = TRUE;
  }

  if ( tempError == TRUE )  // temp sensor issue
  {
    statusOrange();
  }
}

void pacman()
{
  matrix.setRotation(1);

  matrix.clear();
  matrix.setCursor(0, 0);
  matrix.setTextSize(0);
  matrix.drawBitmap(0, 0, pacman_bmp, 8, 8, LED_ON);
  matrix.drawBitmap(8, 0, pacman_bmp, 8, 8, LED_ON);
  matrix.writeDisplay();
}

void smileys()
{
  matrix.setRotation(1);
  matrix2.setRotation(1);

  matrix.clear();
  matrix.setCursor(0, 0);
  matrix.setTextSize(0);
  matrix.drawBitmap(0, 0, smile_bmp, 8, 8, LED_ON);
  matrix.drawBitmap(8, 0, smile_bmp, 8, 8, LED_ON);

  matrix2.clear();
  matrix2.setCursor(0, 0);
  matrix2.setTextSize(0);
  matrix2.drawBitmap(0, 0, smile_bmp, 8, 8, LED_ON);
  matrix2.drawBitmap(8, 0, smile_bmp, 8, 8, LED_ON);

  matrix.writeDisplay();
  matrix2.writeDisplay();
}

void hearts()
{
  matrix.setRotation(1);
  matrix2.setRotation(1);

  matrix.clear();
  matrix.setCursor(0, 0);
  matrix.setTextSize(0);
  matrix.drawBitmap(4, 0, heart_bmp, 8, 8, LED_ON);

  matrix2.clear();
  matrix2.setCursor(0, 0);
  matrix2.setTextSize(0);
  matrix2.drawBitmap(4, 0, heart_bmp, 8, 8, LED_ON);

  matrix.writeDisplay();
  matrix2.writeDisplay();
}


void frowneys()
{
  matrix.setRotation(1);
  matrix2.setRotation(1);

  matrix.clear();
  matrix.setCursor(0, 0);
  matrix.setTextSize(0);
  matrix.drawBitmap(4, 0, frown_bmp, 8, 8, LED_ON);
  matrix.writeDisplay();

  matrix2.clear();
  matrix2.setCursor(0, 0);
  matrix2.setTextSize(0);
  matrix2.drawBitmap(4, 0, frown_bmp, 8, 8, LED_ON);
  matrix2.writeDisplay();

}

float getTemperature()
{
  float temperature = 0.0;
  bmp.getTemperature(&temperature);
  return temperature;
}

float getRtcTemperature()
{
  return rtc.getTemperature();
}


void updateOled()
{
  if ( ! lockOledDisplay ) // if locked we don't update the screen
  {
    DateTime now = rtc.now();
    // uint32_t ts = now.getEpoch();
    display.clearDisplay();
    
    int dayOfWeekNum = now.dayOfWeek();
    char *dayName = weekDayFull[dayOfWeekNum];

    int day = now.date();
    int month = now.month();
    long year = now.year();

    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);

    display.println(dayName);
    display.print(month);
    display.print("/");
    display.print(day);
    display.print("/");
    display.print(year);
    display.println();

    display.display();
  }
}

void updateScreenTemp()
{
  matrix.setRotation(1);
  matrix.clear();
  matrix2.clear();

  matrix.setCursor(0, 0);
  matrix.setTextSize(0);

  float temperature = getTemperature();
  float rtcTemperature = getRtcTemperature();

  Serial.print("BMP180 TEMP: ");
  Serial.println(temperature);

  Serial.print("DS3231 TEMP: ");
  Serial.println(rtcTemperature);

  //Serial.print("ACTUAL TEMP: ");
  //Serial.println(temperature);

  //Serial.println("TEMP INT: ");
  //Serial.println( round(to_F(temperature)) );

  // displaty rounded temperature on the screen
  matrix.print( round(to_F(temperature)) );

  matrix.drawBitmap(8, 0, degree_bmp, 8, 8, LED_ON);
  matrix.writeDisplay();

  // display centered smiley on 2nd display
  matrix2.drawBitmap(4, 0, smile_bmp, 8, 8, LED_ON);

  matrix2.writeDisplay();

}

void updateScreenTime()
{
  DateTime now = rtc.now();

  matrix.setRotation(1);
  matrix2.setRotation(1);

  matrix.clear();
  matrix2.clear();
  matrix2.setCursor(0, 0);
  matrix.setCursor(0, 0);
  matrix.setTextSize(0);
  matrix2.setTextSize(0);

  int hourVal = now.hour(), minuteVal = now.minute();

  if ( hourVal == 0)
    matrix.print("00");
  if ( hourVal == 1 )
    matrix.print("01");
  if ( hourVal == 2 )
    matrix.print("02");
  if ( hourVal == 3 )
    matrix.print("03");
  if ( hourVal == 4 )
    matrix.print("04");
  if ( hourVal == 5 )
    matrix.print("05");
  if ( hourVal == 6 )
    matrix.print("06");
  if ( hourVal == 7 )
    matrix.print("07");
  if ( hourVal == 8 )
    matrix.print("08");
  if ( hourVal == 9 )
    matrix.print("09");
  if ( hourVal > 9 )
    matrix.print(hourVal);


  if ( minuteVal == 0)
    matrix2.print("00");
  if ( minuteVal == 1 )
    matrix2.print("01");
  if ( minuteVal == 2 )
    matrix2.print("02");
  if ( minuteVal == 3 )
    matrix2.print("03");
  if ( minuteVal == 4 )
    matrix2.print("04");
  if ( minuteVal == 5 )
    matrix2.print("05");
  if ( minuteVal == 6 )
    matrix2.print("06");
  if ( minuteVal == 7 )
    matrix2.print("07");
  if ( minuteVal == 8 )
    matrix2.print("08");
  if ( minuteVal == 9 )
    matrix2.print("09");
  if ( minuteVal > 9 )
    matrix2.print(minuteVal);

  matrix2.writeDisplay();

  matrix.drawBitmap(8, 0, colon_bmp, 8, 8, LED_ON);

  matrix.writeDisplay();
}

boolean isPressed(int whichButton)
{
  if ( ! digitalRead(whichButton) )
    return 1;
  else
    return 0;
}

boolean checkWifiStatus()
{
  if ( WiFi.status() == WL_CONNECTED )
  {
    return 1;
  } else {
    return 0;
  }
}

void loop() {

  if ( millis() - lastNtpSync > (pollNtpMinutes * 60000 ))  // sync with NTP every XX minutes
  {

    printDate();
    Serial.println("Syncing NTP");
    adjust();
    lastNtpSync = millis();

  }

  if ( isPressed(BUTTON_A) )
  {
    displayMode = DISPLAY_MODE_TEMP;
    modeCount = 0;
  }

  if ( isPressed(BUTTON_B) && lastButtonPress == 0 )
  {
    lastButtonPress = millis();
    longPressAt = millis() + 3000;
  }

  if ( isPressed(BUTTON_B) && millis() > longPressAt )
  {
    display.clearDisplay();
    display.display();  // wipe the display before changing message

    display.setCursor(0, 0);
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.println("SOFTWARE");
    display.print(versionStr);
    display.display();

    buttonPreviousState = 1;
    lastButtonPress = 0;
    longPressAt = 0;

    delay(5000);
  }

  if ( isPressed(BUTTON_C) && lastButtonPress == 0 )
  {
    buttonPreviousState = 0;
    lastButtonPress = millis();
    longPressAt = millis() + 5000;
  }

  if ( isPressed(BUTTON_C) && millis() > longPressAt )
  {
    buttonPreviousState = 1;
    lastButtonPress = 0;
    longPressAt = 0;

    statusOrange();   // indicate that we've ack'd the long press

    Serial.println("BUTTON PRESS DETECTED FOR DST TOGGLE");

    delay(200);

    // blink blue 3x to indicate DST change
    for ( int i = 0 ; i < 3 ; i++ ) {
      pixels.setPixelColor(0, pixels.Color(0, 0, 50));
      pixels.show();
      delay(500);

      pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      pixels.show();
      delay(500);
    }

    dstOn = ! dstOn;
    setDst();

    if ( dstOn == 1 )
      timezone = -4.0;
    else
      timezone = -5.0;

    adjust();

    statusGreen();  // indicate status green after we've written setting to EEPROM

    display.clearDisplay();
    display.display();  // wipe the display before changing message

    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.print("DST SAVED!");
    display.display();
    delay(3000);

  }

  // check for button presses.  using millis() as a debounce

  if ( millis() - lastRefresh > 1000 ) {

    // Serial.println("SCREEN REFRESH");

    if ( modeCount == displayClockSecs && displayMode == 0 )
    {
      displayMode = DISPLAY_MODE_TEMP;
      modeCount = 0;
      Serial.println("SWITCHING TO TEMP");
    }

    if ( modeCount == displayTempSecs && displayMode == 1 )
    {
      displayMode = DISPLAY_MODE_NORMAL;
      modeCount = 0;
      Serial.println("SWITCHING TO TIME");
      printDate();
    }

    switch ( displayMode )  // update the screen based on the current mode
    {
      case DISPLAY_MODE_NORMAL:
        updateScreenTime();
        break;
      case DISPLAY_MODE_TEMP:
        updateScreenTemp();
        break;
    }

    updateStatusLed();  // check status condition and ensure the LED is updated

    updateOled(); // update OLED on the same cycle as the main screens

    lastRefresh = millis();

    modeCount++;

  }
}
