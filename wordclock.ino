#include <WiFiClient.h>
#include <ESP8266WebServer.h> // Web Server service
#include <ESP8266mDNS.h>

#include <EEPROM.h>

#include <Adafruit_NeoPixel.h>
#include <time.h>               //include time.h library
#include <Wire.h>               //include Wire.h library
#include <WiFiUdp.h>            // UDP is needed for NTP server communication
//#include <WebSocketsServer.h> // Web socket server


// Date and time functions using a DS3231 RTC connected via I2C and Wire lib
#include "RTClib.h"

#ifdef __AVR__
#include <avr/power.h>
#endif


const char* WORD_CLOCK_VERSION = "V 1.00";


/******************************************************************************
    DEFAULT values to be used if no valid SSID can be found
 ******************************************************************************/

//Replace XXX with your wifi network data:
const char* defWiFiNetworkName = "XXX";
const char* defWiFiNetworkPwd  = "XXX";


// Display color (RGB)
#define factor    0.3                       // lightning factor   
int defDisplayR       =   110   * factor;    // red color value
int defDisplayG       =   36    * factor;    // green color value
int defDisplayB       =   114   * factor;    // blue color value

int defDisplayDate    = 0;                  // Flag, whether to display date every 30 seconds 0=no, all other values=yes
int defTimeZoneOffset = 2;                  // Offset for GMT

/******************************************************************************
    DEFAULT values to be used if no valid SSID can be found
 ******************************************************************************/

int delayval = 150; // delay in milliseconds

ESP8266WebServer server(80);    // Create a webserver object that listens for HTTP request on port 80
//WebSocketsServer webSocket(81); // create a websocket server on port 81

// WiFi class
//ESP8266WiFiClass wiFi8266;

// I2C adress of the RTC  DS3231 (Chip on ZS-042 Board)
int RTC_I2C_ADDRESS = 0x68;

// Arduino-Pin connected to the NeoPixels
#define PIN 12

// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS  114

// current RGB values for Display
int displayR =  0;
int displayG =  0;
int displayB =  0;

/* Don't hardwire the IP address or we won't get the benefits of the pool.
   Lookup the IP address for the host name instead
*/
IPAddress timeServerIP;      // IP Adress of de.pool.ntp.org time server
const char* ntpServerName = "de.pool.ntp.org";

char wiFiNetworkName[40];       // SSID of WLAN to be used
char wiFiNetworkPwd[40];        // Password to be used

int displayDate = 1;                // Flag, whether to display date every 30 seconds 0=no, all other values=yes
int timeZoneOffset = 2;             // Offset for GMT

const int NTP_PACKET_SIZE = 48;     // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

WiFiUDP udp;                        // A UDP instance to let us send and receive packets over UDP

unsigned int localPort = 2390;      // local port to listen for UDP packets

RTC_DS3231 rtc;                     // rtc communication object

// variables for RTC-module read time
int iYear,
    iMonth,
    iDay,
    iHour,
    iMinute,
    iSecond;

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);


struct parmRec
{
  char wifiSSID[40];  // SSID of current network
  char wifiPWD[40];   // password of current network
  int rgbR;           // LED setting Red
  int rgbG;           // LED setting Green
  int rgbB;           // LED setting Blue
  int displDate;      // flag whether to display date every 30 seconds
  int tzOffset;       // time zone offset
} parameter;


void setup() {

  // Initialize Serial Monitoring
  Serial.begin(115200);
  delay(1000);
  Serial.println("Start Monitor...");

  // Read parameter from EEPROM
  readEPROM();

  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    abort();
  }

  // start RTC Communication via Wire.h library
  Serial.print("Start RTC communication");
  Wire.begin();

  // This initializes the NeoPixel library.
  Serial.println("Start NeoPixel library");
  pixels.begin();

  // Start WiFi
  Serial.println("Connect to WiFi and get NTP data");
  getTimeFromNtpServer();

  // Start web server
  Serial.println("About to start WebServer....");
  startServer();
}


// write current parameter settings to flash
void writeEPROM() {

  parameter.rgbR      = displayR;
  parameter.rgbG      = displayG;
  parameter.rgbB      = displayB;
  parameter.tzOffset  = timeZoneOffset;
  parameter.displDate = displayDate;

  memcpy(&parameter.wifiSSID, &wiFiNetworkName, sizeof(wiFiNetworkName));
  memcpy(&parameter.wifiPWD,  &wiFiNetworkPwd,  sizeof(wiFiNetworkPwd));

  byte* p = (byte*)(void*)&parameter;

  for (int L = 0; L < sizeof(parameter); ++L) {

    byte b = *p++;
    EEPROM.write(L, b);

    Serial.print("Write FLASH Byte ");
    Serial.print(L);
    Serial.print(" = ");
    Serial.println(b);
  }

  EEPROM.commit();
}


// try to read settings from FLASH - initialize if WLAN ID read from flash is invalid
void readEPROM() {

  Serial.print("Copy ");
  Serial.print(sizeof(parameter));
  Serial.println(" bytes from flash memory to EPROM buffer: ");

  // initialize space to read parameter
  EEPROM.begin(sizeof(parameter));
  delay(10);

  byte* p = (byte*)(void*)&parameter;

  for (int L = 0; L < sizeof(parameter); ++L) {
    byte b = EEPROM.read(L);
    *p++ = b;
    Serial.print("Read FLASH Byte ");
    Serial.print(L);
    Serial.print(" = ");
    Serial.println(b);
  }

  // Copy data from parameter to process variables
  displayR       =  parameter.rgbR;
  displayG       =  parameter.rgbG;
  displayB       =  parameter.rgbB;
  displayDate    =  parameter.displDate;
  timeZoneOffset =  parameter.tzOffset;

  memcpy(&wiFiNetworkName, &parameter.wifiSSID, sizeof(wiFiNetworkName));
  memcpy(&wiFiNetworkPwd,  &parameter.wifiPWD,  sizeof(wiFiNetworkPwd));

  // Check whether network exists - otherwise initilaize parameter
  // scan for nearby networks:
  Serial.println("** Scan Networks **");
  Serial.print("Search for ");
  Serial.println(wiFiNetworkName);

  byte numSsid = WiFi.scanNetworks();

  // print the list of networks seen:
  Serial.print("SSID List:");
  Serial.println(numSsid);

  int wifiFound = 0;

  // print the network number and name for each network found:
  for (int thisNet = 0; thisNet < numSsid; thisNet++) {
    Serial.print(thisNet);
    Serial.print(") Network: ");
    Serial.println(WiFi.SSID(thisNet));

    // check for WIFI match
    if (strncmp(WiFi.SSID(thisNet).c_str(), &wiFiNetworkName[0], sizeof(wiFiNetworkName)) == 0) {
      Serial.println("WiFi found in list");
      wifiFound = -1;
    }
  }

  if (wifiFound == 0) {
    Serial.println("Unable to find WiFi - initialize default settings");
    strcpy(&wiFiNetworkName[0], defWiFiNetworkName);
    strcpy(&wiFiNetworkPwd[0],  defWiFiNetworkPwd);
    displayR       =  defDisplayR;
    displayG       =  defDisplayG;
    displayB       =  defDisplayB;
    displayDate    =  defDisplayDate;
    timeZoneOffset =  defTimeZoneOffset;
    writeEPROM();
  }
}


void startServer()
{

  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started");
  }

  Serial.println("Establish FILE NOT FOUND handler");
  server.onNotFound(handleNotFound); // if someone requests any other file or page, go to function 'handleNotFound'
  // and check if the file exists

  Serial.println("Call server.begin");
  server.begin(80); // start the HTTP server

  server.on("/", handleRoot);               // Call the 'handleRoot' function when a client requests URI "/"

  Serial.println("HTTP server started.");
}

void handleRoot() {
  Serial.println("In handle Root...");
  server.send(200, "text/plain", "This is the world best programmers WordClock! :-) ");   // Send HTTP status 200 (Ok) and send some text to the browser/client
}


/*__________________________________________________________SERVER_HANDLERS__________________________________________________________*/
void handleNotFound()
{ // if the requested file or page doesn't exist, return a 404 not found error
  if (!handleFileRead(server.uri()))
  { // check if the file exists in the flash memory (SPIFFS), if so, send it
    server.send(404, "text/plain", "404: File Not Found");
  }
}

bool handleFileRead(String path)
{ // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/"))
    path += "index.html";                               // If a folder is requested, send the index file
  String contentType = getContentType(path);            // Get the MIME type
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path))
  { // If the file exists, either as a compressed archive, or normal
    if (SPIFFS.exists(pathWithGz))                      // If there's a compressed version available
      path += ".gz";                                    // Use the compressed verion
    File file = SPIFFS.open(path, "r");                 // Open the file
    size_t sent = server.streamFile(file, contentType); // Send it to the client
    file.close();                                       // Close the file again
    Serial.println(String("\tSent file: ") + path);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path); // If the file doesn't exist, return false
  return false;
}


String getContentType(String filename)
{
  if (filename.endsWith(".htm"))
    return "text/html";
  else if (filename.endsWith(".html"))
    return "text/html";
  else if (filename.endsWith(".css"))
    return "text/css";
  else if (filename.endsWith(".js"))
    return "application/javascript";
  else if (filename.endsWith(".ico"))
    return "image/x-icon";
  else if (filename.endsWith(".png"))
    return "image/png";
  else if (filename.endsWith(".gif"))
    return "image/gif";
  else if (filename.endsWith(".jpg"))
    return "image/jpeg";
  else if (filename.endsWith(".ico"))
    return "image/x-icon";
  else if (filename.endsWith(".xml"))
    return "text/xml";
  else if (filename.endsWith(".pdf"))
    return "application/x-pdf";
  else if (filename.endsWith(".zip"))
    return "application/x-zip";
  else if (filename.endsWith(".gz"))
    return "application/x-gzip";
  return "text/plain";
}


// Get current date/ time from NTP Server
void getTimeFromNtpServer() {

  // Coonnect to WiFi - if possible
  wifiConnect();

  // Check, whether we are connected to WLAN
  if ((WiFi.status() == WL_CONNECTED)) {

    Serial.print("Get IP address of NTP server ");
    Serial.println(ntpServerName);

    //get a random server from the pool
    WiFi.hostByName(ntpServerName, timeServerIP);

    Serial.print("get IP address ");
    Serial.println(timeServerIP);

    // send NTP request to time NTP server
    sendNTPpacket(timeServerIP);

    // wait to see if a reply is available
    delay(1000);

    for (int i = 1; i < 5; i++) {
      int cb = udp.parsePacket();
      if (!cb) {
        Serial.println("no packet yet");
        delay(200);
      } else {
        Serial.print("packet received, length=");
        Serial.println(cb);
        // We've received a packet, read the data from it
        udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

        //the timestamp starts at byte 40 of the received packet and is four bytes,
        // or two words, long. First, esxtract the two words:

        unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
        unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
        // combine the four bytes (two words) into a long integer
        // this is NTP time (seconds since Jan 1 1900):
        unsigned long secsSince1900 = highWord << 16 | lowWord;
        Serial.print("Seconds since Jan 1 1900 = ");
        Serial.println(secsSince1900);

        // now convert NTP time into everyday time:
        Serial.print("Unix time = ");
        // Unix time starts on Jan 1 1970. In seconds, that's 2.208.988.800:
        const unsigned long seventyYears = 2208988800UL;
        // subtract seventy years:
        unsigned long epoch = secsSince1900 - seventyYears;

        // add one hour for UTC+1
        //epoch = epoch + (3600 * timeZoneOffset);
        epoch = epoch + (3600 * 2); // SUMMERTIME

        // add one second because of time exceeded between request and RTC change
        epoch = epoch + 1;

        // print Unix time:
        Serial.println(epoch);

        // print the hour, minute and second:
        Serial.print("The UTC+1 time is ");       // UTC is the time at Greenwich Meridian (GMT)

        int ho = (epoch  % 86400L) / 3600;
        Serial.print(ho); // print the hour (86400 equals secs per day)

        int mi = (epoch % 3600) / 60;
        Serial.print(':');
        if (mi < 10) {
          // In the first 10 minutes of each hour, we'll want a leading '0'
          Serial.print('0');
        }
        Serial.print(mi); // print the minute (3600 equals secs per minute)

        int sec = epoch % 60;
        Serial.print(':');
        if (sec < 10) {
          // In the first 10 seconds of each minute, we'll want a leading '0'
          Serial.print('0');
        }
        Serial.print(sec); // print the second

        time_t rawtime = epoch;
        struct tm * ti;
        ti = localtime (&rawtime);

        uint16_t ye = ti->tm_year + 1900;
        uint8_t  mo = ti->tm_mon + 1;
        uint8_t  da = ti->tm_mday;

        Serial.print("   ==  ");
        Serial.print(ye);
        Serial.print('-');
        Serial.print(mo);
        Serial.print('-');
        Serial.println(da);

        rtcWriteTime(ye, mo, da, ho, mi, sec);

        break;
      }
    }
  }
}


//Function to write / set the clock
void rtcWriteTime(int jahr, int monat, int tag, int stunde, int minute, int sekunde) {

  Wire.beginTransmission(RTC_I2C_ADDRESS);

  Wire.write(0); //count 0 activates RTC module

  Wire.write(decToBcd(sekunde));

  Wire.write(decToBcd(minute));

  Wire.write(decToBcd(stunde));

  Wire.write(decToBcd(0)); // weekdays not respected

  Wire.write(decToBcd(tag));

  Wire.write(decToBcd(monat));

  Wire.write(decToBcd(jahr - 2000));

  Wire.endTransmission();

}


//converts decimal to binary signs
byte decToBcd(byte val) {

  return ( (val / 10 * 16) + (val % 10) );
}


// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress& address) {
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;     // LI, Version, Mode
  packetBuffer[1] = 0;              // Stratum, or type of clock
  packetBuffer[2] = 6;              // Polling Interval
  packetBuffer[3] = 0xEC;           // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}


// Switch off all LEDs
void dunkel() {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(0, 0, 0)); // Switch off all LEDs
  }
}

// Switch on default Text "ES IST"
void defaultText() {
  pixels.setPixelColor( 99, pixels.Color(displayR, displayG, displayB));  // E
  pixels.setPixelColor(100, pixels.Color(displayR, displayG, displayB));  // S
  pixels.setPixelColor(102, pixels.Color(displayR, displayG, displayB));  // I
  pixels.setPixelColor(103, pixels.Color(displayR, displayG, displayB));  // S
  pixels.setPixelColor(104, pixels.Color(displayR, displayG, displayB));  // T
}


//actual function, which controlls 1/0 of the LED
void setLED(int ledNrFrom, int ledNrTo, int switchOn) {

  if (switchOn) {
    if  (ledNrFrom > ledNrTo) {
      setLED(ledNrTo, ledNrFrom, switchOn); //sets LED numbers in correct order (because of the date programming below)
    } else {
      for (int i = ledNrFrom; i <= ledNrTo; i++) {
        if ((i >= 0) &&
            (i < NUMPIXELS))
          pixels.setPixelColor(i, pixels.Color(displayR, displayG, displayB));
      }
    }
  }
}


// Switch a horizontal sequence of LEDs ON or OFF, depending on boolean value switchOn
void setLEDLine(int xFrom, int xTo, int y, int switchOn) {

  if (xFrom > xTo)
    setLEDLine(xTo, xFrom, y, switchOn);
  else {
    for (int x = xFrom; x <= xTo; x++) {
      setLED(ledXY(x, y), ledXY(x, y), switchOn);
    }
  }
}

// Convert x/y coordinates into LED number
// return -1 for invalid coordinate
int ledXY (int x, int y) {

  // Test for valid coordinates
  // If outside panel retrun -1
  if ((x < 0)  ||
      (x > 10) ||
      (y < 0)  ||
      (y > 9))
    return -1;


  int ledNr = (9 - y) * 11;

  if ((y % 2) == 0)
    ledNr = ledNr + x;
  else
    ledNr = ledNr + 10 - x;

  return ledNr;
}

//sets, where the numbers from 1 to 9 are printed
void  printAt (int ziffer, int x, int y) {

  switch (ziffer) {

    case 0:   //number 0
      setLEDLine(x + 1, x + 3, y + 6, -1); //-1 is true, so switchOn
      for (int yd = 1; yd < 6; yd++) {
        setLED(ledXY(x,   y + yd), ledXY(x,   y + yd), -1);
        setLED(ledXY(x + 4, y + yd), ledXY(x + 4, y + yd), -1);
      }
      setLEDLine(x + 1, x + 3, y, -1);
      break;

    case 1:   //number 1
      setLED(ledXY(x + 3, y + 5), ledXY(x + 3, y + 5), -1);
      setLED(ledXY(x + 2, y + 4), ledXY(x + 2, y + 4), -1);
      for (int yd = 0; yd <= 6; yd++) {
        setLED(ledXY(x + 4,   y + yd), ledXY(x + 4,   y + yd), -1);
      }
      break;

    case 2:   //number 2
      for (int d = 1; d <= 4; d++) {
        setLED(ledXY(x + d,   y + d), ledXY(x + d,   y + d), -1);
      }
      setLEDLine(x, x + 4, y, -1);
      setLED(ledXY(x, y + 5),   ledXY(x, y + 5), -1);
      setLED(ledXY(x + 4, y + 5), ledXY(x + 4, y + 5), -1);
      setLEDLine(x + 1, x + 3, y + 6, -1);
      break;

    case 3:
      for (int yd = 1; yd <= 2; yd++) {
        setLED(ledXY(x + 4, y + yd + 3), ledXY(x + 4, y + yd + 3), -1);
        setLED(ledXY(x + 4, y + yd),   ledXY(x + 4, y + yd), -1);
      }
      for (int yd = 0; yd < 7; yd = yd + 3) {
        setLEDLine(x + 1, x + 3, y + yd, -1);
      }
      setLED(ledXY(x, y + 1), ledXY(x, y + 1), -1);
      setLED(ledXY(x, y + 5), ledXY(x, y + 5), -1);
      break;

    case 4:
      for (int d = 0; d <= 3; d++) {
        setLED(ledXY(x + d,   y + d + 3), ledXY(x + d,   y + d + 3), -1);
      }
      for (int yd = 0; yd <= 3; yd++) {
        setLED(ledXY(x + 3,   y + yd), ledXY(x + 3,   y + yd), -1);
      }
      setLEDLine(x, x + 4, y + 2, -1);
      break;

    case 5:
      setLEDLine(x, x + 4, y + 6, -1);
      setLED(ledXY(x  , y + 5), ledXY(x  , y + 5), -1);
      setLED(ledXY(x  , y + 4), ledXY(x  , y + 4), -1);
      setLEDLine(x, x + 3, y + 3, -1);
      setLED(ledXY(x + 4, y + 2), ledXY(x + 4, y + 2), -1);
      setLED(ledXY(x + 4, y + 1), ledXY(x + 4, y + 1), -1);
      setLEDLine(x, x + 3, y, -1);
      break;
    case 6:
      for (int d = 0; d <= 3; d++) {
        setLED(ledXY(x + d,   y + d + 3), ledXY(x + d,   y + d + 3), -1);
      }
      for (int yd = 0; yd < 4; yd = yd + 3) {
        setLEDLine(x + 1, x + 3, y + yd, -1);
      }
      setLED(ledXY(x, y + 1), ledXY(x, y + 1), -1);
      setLED(ledXY(x, y + 2), ledXY(x, y + 2), -1);
      setLED(ledXY(x + 4, y + 1), ledXY(x + 4, y + 1), -1);
      setLED(ledXY(x + 4, y + 2), ledXY(x + 4, y + 2), -1);
      break;

    case 7:
      for (int yd = 0; yd <= 6; yd++) {
        setLED(ledXY(x + 3,   y + yd), ledXY(x + 3,   y + yd), -1);
      }
      setLEDLine(x + 1, x + 4, y + 3, -1);
      setLEDLine(x, x + 3, y + 6, -1);
      break;

    case 8:
      for (int yd = 1; yd <= 2; yd++) {
        setLED(ledXY(x + 4, y + yd + 3), ledXY(x + 4, y + yd + 3), -1);
        setLED(ledXY(x + 4, y + yd),   ledXY(x + 4, y + yd), -1);
        setLED(ledXY(x, y + yd + 3),   ledXY(x, y + yd + 3), -1);
        setLED(ledXY(x, y + yd),     ledXY(x, y + yd), -1);
      }
      for (int yd = 0; yd < 7; yd = yd + 3) {
        setLEDLine(x + 1, x + 3, y + yd, -1);
      }
      break;

    case 9:
      for (int d = 0; d <= 3; d++) {
        setLED(ledXY(x + d + 1,   y + d),  ledXY(x + d + 1,   y + d), -1);
      }
      for (int yd = 4; yd <= 5; yd++) {
        setLED(ledXY(x, y + yd),     ledXY(x, y + yd), -1);
        setLED(ledXY(x + 4, y + yd),   ledXY(x + 4, y + yd), -1);
      }
      setLEDLine(x + 1, x + 3, y + 6, -1);
      setLEDLine(x + 1, x + 4, y + 3, -1);

      break;
  }

}


//turns on the outer four LEDs (one per minute)
void showMinutes(int minutes) {

  int minMod = (minutes % 5);
  for (int i = 1; i < 5; i++) {

    int ledNr = 0;
    switch (i) {
      case 1: ledNr = 111; break;
      case 2: ledNr = 110; break;
      case 3: ledNr = 113; break;
      case 4: ledNr = 112; break;
    }

    if (minMod < i)
      pixels.setPixelColor(ledNr, pixels.Color(0, 0, 0));
    else
      pixels.setPixelColor(ledNr, pixels.Color(displayR, displayG, displayB));
  }
}


//converts binary signs to decimal (needed for RTC-module)
byte bcdToDec(byte val) {
  return ( (val / 16 * 10) + (val % 16) );
}


// Read current date & time from RTC
void rtcReadTime() {

  DateTime now = rtc.now();

  int oldMinute = iMinute;

  iYear  = (int)(now.year());
  iMonth = (int)(now.month());
  iDay   = (int)(now.day());

  iHour   = (int)(now.hour());
  iMinute = (int)(now.minute());
  iSecond = (int)(now.second());

  // Print out time if minute has changed
  if (iMinute != oldMinute) {
    Serial.print("rtcReadTime() - get time from RTC : ");
    Serial.print(iYear);
    Serial.print("-");
    Serial.print(iMonth);
    Serial.print("-");
    Serial.print(iDay);

    Serial.print(" ");
    Serial.print(iHour);
    Serial.print(":");
    Serial.print(iMinute);
    Serial.print(":");
    Serial.println(iSecond);
  }

  // At 1 minute past 2 and 1 minute past 3 get time from NTP server
  if (  // ((iHour == 2) || (iHour == 3)) &&
    (iMinute == 0) &&
    (iSecond == 0)) {
    getTimeFromNtpServer();
    delay(1000 - delayval);
  } else {
    // Test Code !
    if (((iMinute % 30) == 0) && (iSecond == 05)) getTimeFromNtpServer();
  }

}

void showTime() {

  dunkel();      // switch off all LEDs
  defaultText(); // Switch on ES IST

  // for troubleshooting use only!
  //hour = 17;
  //minute = 52;

  /**
    Serial.print("Show time ");
    Serial.print(iHour);
    Serial.print(":");
    Serial.print(iMinute);
    Serial.print(":");
    Serial.println(iSecond);
  **/

  // divide minute by 5 to get value for display control
  int minDiv = iMinute / 5;

  // Fuenf (Minuten)
  setLED(106, 109, ((minDiv ==  1) ||
                    (minDiv ==  5) ||
                    (minDiv ==  7) ||
                    (minDiv == 11)));

  // Viertel
  setLED(81, 87, ((minDiv == 3) ||
                  (minDiv == 9)));

  // Zehn (Minuten)
  setLED(95, 98, ((minDiv ==  2) ||
                  (minDiv == 10)));

  // Zwanzig
  setLED(88, 94, ((minDiv == 4) ||
                  (minDiv == 8)));

  // Nach
  setLED(66, 69, ((minDiv == 1) ||
                  (minDiv == 2) ||
                  (minDiv == 3) ||
                  (minDiv == 4) ||
                  (minDiv == 7)));

  // Vor
  setLED(74, 76, ((minDiv ==  5) ||
                  (minDiv ==  8) ||
                  (minDiv ==  9) ||
                  (minDiv == 10) ||
                  (minDiv == 11)));

  // Halb
  setLED(55, 58, ((minDiv == 5) ||
                  (minDiv == 6) ||
                  (minDiv == 7)));

  // Eck-LEDs: 1 pro Minute
  showMinutes(iMinute);

  //set hour from 1 to 12 (at noon, or midnight)
  iHour = (iHour % 12);
  if (iHour == 0)
    iHour = 12;

  // at minute 25 hour needs to be counted up:
  // fuenf vor halb 2 = 13:25
  if (iMinute >= 25) {
    if (iHour == 12)
      iHour = 1;
    else
      iHour++;
  }

  // Uhr
  //
  setLED(0, 2, (iMinute < 5));

  // Ein
  setLED(52, 54, (iHour == 1));

  // einS (S in EINS) (just used at point 1 o'clock)
  setLED(51, 54, ((iHour == 1) &&
                  (iMinute > 4)));

  // Zwei
  setLED(44, 47, (iHour == 2));

  // Drei
  setLED(33, 36, (iHour == 3));

  // Vier
  setLED(40, 43, (iHour == 4));

  // Fuenf
  setLED(62,  65, (iHour == 5));

  // Sechs
  setLED(28,  32, (iHour == 6));

  // Sieben
  setLED(11, 16, (iHour == 7));

  // Acht
  setLED(22, 25, (iHour == 8));

  // Neun
  setLED(4, 7, (iHour == 9));

  // Zehn (Stunden)
  setLED(7, 10, (iHour == 10));

  // Elf
  setLED(60, 62, (iHour == 11));

  // Zwoelf
  setLED(17, 21, (iHour == 12));
}


//Implementation of date function
void showDate () {

  for (int x = 11; x > -50; x--) {
    dunkel();
    printAt (iDay / 10,   x,    2);
    printAt (iDay % 10,   x + 6,  2);
    setLED(ledXY(x + 11, 2), ledXY(x + 11, 2), -1); //sets first point
    printAt (iMonth / 10, x + 13, 2);
    printAt (iMonth % 10, x + 19, 2);
    if (iYear < 1000)
      iYear = iYear + 2000;
    setLED(ledXY(x + 24, 2), ledXY(x + 24, 2), -1); //sets second point
    printAt (iYear / 1000, x + 26, 2);
    iYear = iYear % 1000;
    printAt (iYear / 100,  x + 32, 2);
    iYear = iYear % 100;
    printAt (iYear / 10,  x + 38, 2);
    printAt (iYear % 10,  x + 44, 2);
    pixels.show();
    delay (150);// set speed of timeshift
  }
}


//Implementation of date function
void showIP () {

  IPAddress ip = WiFi.localIP();

  Serial.print("Displaying IP address ");
  Serial.println(ip);

  for (int x = 11; x > -75; x--) {
    dunkel();

    int offSet = x;

    // Loop over 4 bytes of IP Address
    for (int idx = 0; idx < 4; idx++) {

      uint8_t octet = ip[idx];

      printAt (octet / 100, offSet, 2);
      octet = octet % 100;
      offSet = offSet + 6;
      printAt (octet / 10, offSet, 2);
      offSet = offSet + 6;
      printAt (octet % 10, offSet, 2);
      offSet = offSet + 6;

      // add delimiter between octets
      if (idx < 3) {
        setLED(ledXY(offSet, 2), ledXY(offSet, 2), -1);  //sets point
        offSet++;
      }
    }

    pixels.show();
    delay (100);    // set speed of timeshift
  }
}


// Connect to WiFi Network
void wifiConnect() {

  Serial.println("Inside wifiConnect()");

  // Check, whether we are already connected....
  if ((WiFi.status() != WL_CONNECTED)) {

    Serial.print("Connecting to WiFi Network ");
    Serial.println(wiFiNetworkName);

    WiFi.mode(WIFI_STA);
    WiFi.begin(wiFiNetworkName, wiFiNetworkPwd);

    int i = 1;
    int count = 50;   // Maximum wait 25 seconds
    while ((WiFi.status() != WL_CONNECTED) && (count > 0))
    {
      dunkel();
      setLED(110, 113, (0 == 0));   // Eck LED

      setLED(93, 93, (i == 1));     // W
      setLED(61, 61, (i == 1));     // L
      setLED(37, 37, (i == 1));     // A
      setLED(4, 4, (i == 1));       // N

      setLED(70, 73, (i == 0));     // FUNK

      pixels.show();

      i = 1 - i;
      delay(750);
      Serial.print(".");

      count--;
    }

    Serial.println();

    if ((WiFi.status() == WL_CONNECTED))
      showIP();
  }

  if ((WiFi.status() == WL_CONNECTED)) {
    Serial.println();

    Serial.print("Connected, IP address: ");
    Serial.println(WiFi.localIP());

    Serial.println("Starting UDP");
    udp.begin(localPort);
    Serial.print("Local port: ");
    Serial.println(udp.localPort());
  } else {
    Serial.print("Unable to connect to WiFi ");
    Serial.println(wiFiNetworkName);
  }
}


//main function, which decides weather to show date or time
void loop () {

  //Serial.println("Loop...");

  server.handleClient();
  MDNS.update();

  // get time from RTC
  rtcReadTime();

  int sec = ((iHour * 60) + iMinute) * 60 + iSecond;
  if ((sec % 30) == 0) {    // first value: how often; second value: time delay
    if (displayDate != 0)
      showDate();
    showTime();  //set this for not showing the date
  } else {
    showTime();
  }

  pixels.show(); // This sends the updated pixel color to the hardware.

  ESP.wdtFeed();  // Reset watchdog timer

  delay(delayval); // Delay for a period of time (in milliseconds, should be 1 second).
}
