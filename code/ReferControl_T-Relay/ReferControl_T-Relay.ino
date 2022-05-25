/*
 *    Nelsony.com - Catalina Refrigeration Controller
 *    
 *    Set Board to TTGO T-Relay or TTGO T7 V1.3, set baud rate to 115,200.
 *    
 *    Last update 12/14/2021
*/

#include "Arduino.h"
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ESPmDNS.h>
#include "SSD1306Wire.h"  
#include <EEPROM.h>
#include <ezTime.h>
#include <esp_task_wdt.h>

#include "COMMS.h"
#include <arduino_secrets.h>


#define VERSION_NUM "0.08"

#define REFER_CONTROLLER    1

//60 seconds WDT
#define WDT_TIMEOUT     60

#define FZR_TEMP_SET    -4
#define REF_FAN_SET     35
#define TEMP_DELTA      5

#define I2C_SDA         32      // 21
#define I2C_SCL         33      // 22
#define ONE_WIRE_BUS    27

#define COMPRESSOR      21      // Relay K1
#define FAN             19      // Relay K2
#define RELAY_K3        18
#define RELAY_K4        05
#define RED_LED         25      // Onboard RED LED

#define SEALEVELPRESSURE_HPA (1013.25)

#define UPDATE_TIME     1000   // 1 second
#define UPDATE_BUF_SZ   120
//#define ROW_SPACING     21  //22 if scrolling for Font size 20

#define SCREEN_WIDTH    128 // OLED display width, in pixels
#define SCREEN_HEIGHT   64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library. 
//#define OLED_RESET      4 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS  0x3c ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
#define BME_ADDRESS     0x76
                            
//
// Global variables
//
Adafruit_BME280 bme;
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensors(&oneWire);
DeviceAddress sensor1 = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };

// Initialize the OLED display using Arduino Wire:
SSD1306Wire display(SCREEN_ADDRESS, I2C_SDA, I2C_SCL);   // ADDRESS, SDA, SCL


float   FzrTemp         = -4;
float   RefHumd         = 0;
float   RefTemp         = 0;
float   RefPress        = 0;
float   RefAlt          = 0;
byte    CompOn          = 0;
byte    FanOn           = 0;
signed char fzrTempSet       = FZR_TEMP_SET;
signed char fanTempSet       = REF_FAN_SET;

String  fTempDisplay;
String  rTempDisplay;
String  humidDisplay;

int     totalLength;       //total size of firmware
int     currentLength = 0; //current size of written firmware

char    updateBuf[UPDATE_BUF_SZ];


//~~ Function ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*!
    \brief  Function to find Dallas One-Wire devices
    \param OneWire Object from OneWire library
    \param Sensor store byte array
    \return 0 = Success, -1 if no devices found;
*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
int 
FindOneWireDevices(
    OneWire DSWire,
    DeviceAddress DSSensor)
{
    bool  foundDevice = false;
    byte  i;

    i = 30;
    while (!foundDevice)
    {
        DSWire.reset_search();
        delay(500);
        foundDevice = DSWire.search(DSSensor);
        Serial.print(".");
        if (!--i)
        {
            Serial.println("No One Wire devices found.");
            digitalWrite(RED_LED, LOW);
            return -1;
        }
    }
    if (foundDevice)
    {
        Serial.print(" ROM =");
        for (i = 0; i < 8; i++) 
        {
            Serial.write(' ');
            Serial.print(DSSensor[i], HEX);
        }
        Serial.println(".");
    }
    return 0;
}


//~~ Function ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*!
    \brief  Function to read the attached sensors
    \return 0 = Success;
*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
int
readSensors()
{
    sensors.requestTemperatures(); // Send the command to get temperatures
  
    FzrTemp = sensors.getTempF(sensor1);
    RefTemp = (1.8 * bme.readTemperature() + 32);
    RefHumd = bme.readHumidity();
    RefPress = bme.readPressure() / 100.0F;
    RefAlt = bme.readAltitude(SEALEVELPRESSURE_HPA);

    Serial.print("Ref Temp = ");
    Serial.print(RefTemp);
    Serial.println(" *F");

    Serial.print("Fzr Temp = ");
    Serial.print(FzrTemp);
    Serial.println(" *F");

    Serial.print("Pressure = ");
    Serial.print(RefPress);
    Serial.println(" hPa");

    Serial.print("Approx. Altitude = ");
    Serial.print(RefAlt);
    Serial.println(" m");

    Serial.print("Humidity = ");
    Serial.print(RefHumd);
    Serial.println(" %");

    Serial.println();
    return 0;
}


//~~ Function ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*!
    \brief  Function to check the sensor readings against set thresholds.
    \return 0 = Success;
*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
int
checkReadings()
{
    // Make sure the sensor values are valid
    if ((FzrTemp < 120) && (FzrTemp > -60))
    {
        // If temp is greater than around -0 and compressor is off, turn on compressor
        //if ((FzrTemp > FZR_TEMP_ON) && (CompOn == 0))
        if ((FzrTemp > (fzrTempSet + TEMP_DELTA)) && (CompOn == 0))
        {
            digitalWrite(COMPRESSOR, HIGH);
            CompOn = 1;
        }
        // If temp is less than around -5, turn off compressor
        if ((FzrTemp < fzrTempSet) && (CompOn == 1))
        {
            digitalWrite(COMPRESSOR, LOW);
            CompOn = 0;        
        }
    }
    // Make sure the sensor values are valid
    if ((RefTemp < 120) && (RefTemp > -60))
    {
        // If temp is greater than around 40, turn on fan
        //if ((RefTemp > REF_FAN_ON) && (FanOn == 0))
        if ((RefTemp > (fanTempSet + TEMP_DELTA)) && (FanOn == 0))
        {
            digitalWrite(FAN, HIGH);
            FanOn = 1;
        }
        // If temp is less than around 35, turn off fan
        //if ((RefTemp < REF_FAN_OFF) && (FanOn == 1))
        if ((RefTemp < fanTempSet) && (FanOn == 1))
        {
            digitalWrite(FAN, LOW);
            FanOn = 0;        
        }
    }
    return 0;
}


//~~ Function ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*!
    \brief  Function to setup and initialize the system - Standard Arduino call
*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void 
setup() 
{
    Serial.begin(230400); // 115200);
    
    Serial.println(F(""));
    Serial.printf("Nelsony ESP32 T-Relay - Refrigeration Controller - Version %s\n", VERSION_NUM);

    // Setup the Watchdog timer
    esp_task_wdt_init(WDT_TIMEOUT, true); //enable panic so ESP32 restarts
    esp_task_wdt_add(NULL); //add current thread to WDT watch   
    
    FindOneWireDevices(oneWire, sensor1);
    
    pinMode(COMPRESSOR, OUTPUT);
    digitalWrite(COMPRESSOR, LOW);

    pinMode(FAN, OUTPUT);
    digitalWrite(FAN, LOW);

    pinMode(RED_LED, OUTPUT);
    digitalWrite(RED_LED, HIGH);

    bool wireStatus = Wire.begin(I2C_SDA, I2C_SCL);
    if (!wireStatus)
    {
        Serial.println("Failed to init I2C.");
        display.clear();
        display.drawString( 0, 0, "I2C failed");
        display.drawString( 0, 15, "to init");
        display.display();
        delay(3000);
    }

    // Initialising the UI will init the display too.
    display.init();
    //display.flipScreenVertically();

    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_24);
    display.drawString(64, 0, "Nelsony");
    display.setFont(ArialMT_Plain_16);
    display.drawString(63, 30, "Refer Control");

    // write the buffer to the display
    display.display();    

    bool bme_status = bme.begin(BME_ADDRESS, &Wire);  //address either 0x76 or 0x77
    if (!bme_status) 
    {
        Serial.println("Failed to detect BME280 sensor.");
        display.clear();
        display.drawString( 0, 0, "No valid BME280");
        display.drawString( 0, 15, "found, please");
        display.drawString( 0, 30, "check wiring!");
        display.display();
        delay(3000);
    }
    sensors.begin();

    initializeComms(REF_CNTLR_NAME, REF_CNTLR);
    
    ServerInit();

    delay(2000);
}


//~~ Function ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*!
    \brief  Function main loop thread - Standard Arduino system call
*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void
loop() 
{ 
    char     dispBuf[50]; 

    // Reset the Watchdog timer
    esp_task_wdt_reset();
    
    // Update the sensors and display every second
    readSensors();
    checkReadings();
      
    // clear the display
    display.clear();
    display.setFont(ArialMT_Plain_24);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 0, "FT");
    display.drawString(0, 21, "RT");
    display.drawString(0, 42, "RH");
        
    snprintf((char *)&dispBuf[0], 50, "%2.0f°F", FzrTemp);
    fTempDisplay = String(dispBuf);
    snprintf((char *)&dispBuf[0], 50, "%2.0f°F", RefTemp);
    rTempDisplay = String(dispBuf); 
    snprintf((char *)&dispBuf[0], 50, "%2.0f%%", RefHumd);
    humidDisplay = String(dispBuf); 
    
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(124, 0, fTempDisplay);
    display.drawString(124, 21, rTempDisplay);
    display.drawString(124, 42, humidDisplay);
    // write the buffer to the display
    display.display();

    snprintf((char *)&updateBuf[0],  UPDATE_BUF_SZ,
       "{\"fzt\":%2.1f,\"rft\":%2.1f,\"rfh\":%2.1f,\"co\":%i,\"fo\":%i,\"fzts\":%i,\"rfts\":%i}", 
        FzrTemp, RefTemp, RefHumd, CompOn, 
        FanOn, fzrTempSet, fanTempSet);

    if (tryConnecting)
    {
        connectWifi();
    }
    if (shouldReboot)
    {
        Serial.println("Rebooting...");
        delay(100);
        ESP.restart();
    }
    delay(UPDATE_TIME);
}
