/*
 *    Nelsony.com - Communication functions
 *    
 *    Last update 1/14/2022
*/

#include "COMMS.h"


// IP And Server variables
char    MACAddr[18];
char    deviceID[20];
byte    devNum;
String  sIPAddr;

bool    gMDNSInit       = false;
bool    gServiceFound   = false;
char    gServerIP[20];
int     gServerPort;

//unsigned long delayTime;
char    URLBuf[128];
char *  pURL = &URLBuf[0];

bool    adHocMode = false;
char    ssid[SSID_LENGTH];
char    password[PSWD_LENGTH];
byte    userFlags       = 0;
bool    apConnected = false;
bool    tryConnecting = true;


// Firmware update variables.
//int             totalLength;       //total size of firmware
//int             currentLength = 0; //current size of written firmware



//~~ Function ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*!
    \brief  Function to initialize all the necessary comms functions
*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
int
initializeComms(
    const char *    devType,
    const char *    devCode)
{
    // Initialize EEPROM library and read our contents (if any)
    EEPROM.begin(EEPROM_SIZE);    
    readEEPROMStore();

    if (!SPIFFS.begin(true))
    {
        Serial.println("An Error has occurred while mounting SPIFFS");
        SPIFFS_ERROR = true;
    }

    WiFi.disconnect(true);
   
    String MAC = WiFi.macAddress();
    MAC.replace(":", ""); 
    MAC.toCharArray(MACAddr, MAC.length() + 1);
    for (int i = 0; i < 6; i++)
    {
        devNum += MACAddr[i];
    }
    devNum &= 0x5F;
    strcpy((char *)&deviceID[0], devCode);
    strcat((char *)&deviceID[0], (char *)&MACAddr[6]); 
    Serial.print("DeviceNumber: ");
    Serial.println(devNum);
    Serial.print("DeviceID: ");
    Serial.println((char *)&deviceID[0]);
    connectWifi();
   
    return 0;
}

//~~ Function ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*!
    \brief  Function to read and initialize if necessary NV params
*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void
readEEPROMStore()
{
    if ((EEPROM.read(VALID_OFFSET) == VALID_SIG0) &&
       (EEPROM.read(VALID_OFFSET + 1) == VALID_SIG1))
    {
        Serial.println("Using EEPROM Values");
        userFlags = EEPROM.read(UFLAGS_OFFSET);
        for (int i= 0; i < SSID_LENGTH; i++)
        {
            ssid[i] = EEPROM.read(SSID_OFFSET + i);
        }
        for (int i= 0; i < PSWD_LENGTH; i++)
        {
            password[i] = EEPROM.read(PSWD_OFFSET + i);
        }
        for (int i= 0; i < IP_ADDR_STR_LEN; i++)
        {
            gServerIP[i] = EEPROM.read(UPLOAD_IP + i);
        }
        gServerPort = EEPROM.read(UPLOAD_PORT);
        gServerPort |= (EEPROM.read(UPLOAD_PORT + 1) << 8);
        
#ifdef REFER_CONTROLLER        
        fzrTempSet = EEPROM.read(FZRST_OFFSET);
        fanTempSet = EEPROM.read(REFST_OFFSET);
#endif // Refer Controller        
    }
    else
    {
        Serial.print("Setting default EEPROM values");
       
        userFlags = 0;
        EEPROM.write(UFLAGS_OFFSET, userFlags);
        for (int i= 0; i < SSID_LENGTH; i++)
        {
            EEPROM.write(SSID_OFFSET + i, 0);
        }
        for (int i= 0; i < PSWD_LENGTH; i++)
        {
            EEPROM.write(PSWD_OFFSET + i, 0);
        }
        for (int i= 0; i < IP_ADDR_STR_LEN; i++)
        {
            EEPROM.write(UPLOAD_IP + i, 0);
        }
        EEPROM.write(UPLOAD_PORT, 0);
        EEPROM.write(UPLOAD_PORT + 1, 0);
#ifdef REFER_CONTROLLER        
        EEPROM.write(FZRST_OFFSET, FZR_TEMP_SET);
        EEPROM.write(FZRST_OFFSET + 1, 0);
        EEPROM.write(REFST_OFFSET, REF_FAN_SET);
        EEPROM.write(REFST_OFFSET + 1, 0);
#endif // Refer Controller        
        EEPROM.write(VALID_OFFSET, VALID_SIG0);
        EEPROM.write(VALID_OFFSET + 1, VALID_SIG1);
        EEPROM.commit();
    }
}


//~~ Function ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*!
    \brief  Function to connect to a Wifi AP or setup Ad-Hoc
    \return false = Not connected
*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
bool
connectWifi()
{
    static int connectAttempts = 0;
    char *  devId = &deviceID[0];
    int     retries = 100;
    bool    tryAdHoc = false;
    bool    retval = false;

    if (userFlags & UF_WIFI_ON)
    {
        if (WiFi.status() != WL_CONNECTED) 
        {
            Serial.print("connectWifi, trying AP Mode: ");
            Serial.println(userFlags);
            WiFi.mode(WIFI_STA);
            delay(500);
            WiFi.disconnect(true);
            delay(500);
            adHocMode = false;
            WiFi.begin(ssid, password);
            while (WiFi.status() != WL_CONNECTED) 
            {
                if (!--retries)
                    break; 
                delay(100);
                Serial.print(".");
                // Reset the Watchdog timer
                esp_task_wdt_reset();
            }
            Serial.println("");
        }
        if (WiFi.status() == WL_CONNECTED)
        {
            if (!apConnected)
            {
                Serial.print("WiFi Connected to SSID: ");
                Serial.println(ssid);
                sIPAddr = WiFi.localIP().toString();
                Serial.print("IP address: ");
                Serial.println(sIPAddr);
                apConnected = true;
                tryConnecting = false;
                setService();
            }
            retval = true;
        }
        else
        {
            // If we did not connect and failed multiple attempts...
            connectAttempts++;
            // and we are trying a new connection
            if ((tryConnecting && (connectAttempts > 3)) || 
                (connectAttempts > 5))
            {
                // Fail and revert to Ad-Hoc
                connectAttempts = 0;
                Serial.println("Reverting to AdHoc mode");
                tryAdHoc = true;        
            }
        }
    }
    else
    {
        tryAdHoc = true;        
    }
    if (tryAdHoc)
    {    
        if (!adHocMode)
        {
            char    localSSID[SSID_LENGTH];
            // Create Wi-Fi ad-hoc network
            WiFi.mode(WIFI_AP);
            //adHocMode = true;
            Serial.print("Setting up AdHoc SSID: ");
#ifdef REFER_CONTROLLER        
            sprintf((char *)&localSSID[0], "NelsonyRefer%d", devNum);
#endif // 
#ifdef ENGINE_MONITOR
            sprintf((char *)&localSSID[0], "NelsonyEngine%d", devNum);
#endif //             
#ifdef WEATHER_DISPLAY
            sprintf((char *)&localSSID[0], "NelsonyWeather%d", devNum);
#endif // Weather Display            
#ifdef WEATHER_SENSOR
            sprintf((char *)&localSSID[0], "NelsonyWthrSense%d", devNum);
#endif // Weather Sensor
#ifdef RC_HUB
            sprintf((char *)&localSSID[0], "NelsonyRCHub%d", devNum);
#endif // Remote Control Hub        

            Serial.println((char *)&localSSID[0]);
            WiFi.softAP((char *)&localSSID[0], devId);
            sIPAddr = WiFi.softAPIP().toString();
            Serial.print("IP address: ");
            Serial.println(sIPAddr);
            apConnected = false;
            setService();
        }
        tryConnecting = false;
    }
    return retval;    
}


//~~ Function ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*!
    \brief  Function to disconnect from a Wifi AP or Ad-Hoc
*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void
disconnectWifi()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print("Disconnecting from WiFi");
        WiFi.disconnect(true);
        delay(500);
        //WiFi.mode(WIFI_AP);
        delay(500);
        while (WiFi.status() == WL_CONNECTED) 
        {
            delay(500);
            Serial.print(".");
        }
        Serial.println("");
    }
    return;
}

//~~ Function ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*!
    \brief  Function to find our data concentrator
    \param  Name of serivce
    \param  Protocol of service, tcp, udp, etc.
    \return false = Not found
*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
bool
setService()
{
    if (!gMDNSInit)
    {
#ifdef REFER_CONTROLLER        
        if (MDNS.begin("Nelsony-ReferControl")) 
#endif // Refer Controller
#ifdef ENGINE_MONITOR
        if (MDNS.begin("Nelsony-EngineMonitor")) 
#endif // Engine Monitor        
#ifdef WEATHER_DISPLAY
        if (MDNS.begin("Nelsony-WeatherDisplay")) 
#endif // Engine Monitor        
#ifdef WEATHER_SENSOR
        if (MDNS.begin("Nelsony-WeatherSensor")) 
#endif // Engine Monitor        
#ifdef RC_HUB
        if (MDNS.begin("Nelsony-RC_Hub")) 
#endif // Remote Control Hub        
        {
            gMDNSInit = true;
            MDNS.addService("http", "tcp", 80);
        }
        else
        {
            Serial.println("mDNS failed to start");
        }
    }
    return true;
}
       
#ifdef WEATHER_SENSOR
//~~ Function ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*!
    \brief  Function to find our data concentrator
    \param  Name of serivce
    \param  Protocol of service, tcp, udp, etc.
    \return false = Not found
*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
bool
findService()
{
    int     nbrServices;
    int     retries = 1;
    bool    updateAddr = false;

    if (!gMDNSInit)
    {
        if (MDNS.begin("Nelsony")) 
        {
            gMDNSInit = true;
            MDNS.addService("EngMon", "tcp", 80);
        }
        else
        {
            Serial.println("mDNS failed to start");
        }
    }

    if (!apConnected)
        return false;
       
    if (!gServiceFound)
    {
        while(retries--)
        {
            // Reset the Watchdog timer
            esp_task_wdt_reset();
            nbrServices = MDNS.queryService("nelsony", "tcp");
            if (nbrServices == 0) 
            {
                Serial.println("No services were found.");
                //gMDNSInit = false;
            } 
            else 
            {
                for (int i = 0; i < nbrServices; i++) 
                {
                    const char * mdnsIPAddr = MDNS.IP(i).toString().c_str();
                    for (int t= 0; t < IP_ADDR_STR_LEN; t++)
                    {
                        if (mdnsIPAddr[t] == 0)
                            break;
                        if (gServerIP[t] != mdnsIPAddr[t])
                        {
                            updateAddr = true;
                            Serial.print("Old IP address: ");
                            Serial.println(gServerIP);
                            break;
                        }
                    }
                    if (gServerPort != MDNS.port(i))
                    {
                        updateAddr = true;
                        Serial.print("Old Port: ");
                        Serial.println(gServerPort);
                    }

                    if (updateAddr)
                    {
                        Serial.print("Hostname: ");
                        Serial.println(MDNS.hostname(i));
                        Serial.print("IP address: ");
                        Serial.println(MDNS.IP(i));
            
                        Serial.print("Port: ");
                        Serial.println(MDNS.port(i));
                        strncpy(gServerIP, MDNS.IP(i).toString().c_str(), 20);
                        gServerPort = MDNS.port(i);
                        for (int i= 0; i < IP_ADDR_STR_LEN; i++)
                        {
                            EEPROM.write(UPLOAD_IP + i, gServerIP[i]);
                        }
                        EEPROM.write(UPLOAD_PORT, (gServerPort & 0x00FF));
                        EEPROM.write(UPLOAD_PORT + 1, ((gServerPort & 0xFF00) >> 8));
                        EEPROM.commit();
                        gServiceFound = true;
                    }
                }
                break;
            }
        }
    }
    return true; //gServiceFound;
}


//~~ Function ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*!
    \brief  Function to send our information packet to the cloud
    \param  Data to send to the cloud
    \return 0 = success, 1 = firmware update available.

    304 Not Modified-
    This is used for caching purposes. It tells the client that the response 
    has not been modified, so the client can continue to use the same cached 
    version of the response.

    426 Upgrade Required - 
    The server refuses to perform the request using the current protocol 
    but might be willing to do so after the client upgrades to a different 
    protocol. The server sends an Upgrade header in a 426 response to 
    indicate the required protocol(s).

*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
int 
sendMessage(
    char * httpData)
{
    int   retCode = -1;    

    HTTPClient http;

    if (apConnected)
    {
        if (findService())
        {
            sprintf(pURL, "http://%s:%i/api/data?devID=%s&version=%s", 
                gServerIP, gServerPort, deviceID, VERSION_NUM);
            Serial.print("Connecting to website: ");
            Serial.println(pURL);
        
            // Reset the Watchdog timer
            esp_task_wdt_reset();
            http.begin(pURL); //, test_root_ca);
            http.setConnectTimeout(50000);
            http.setTimeout(50000);
            // Add the API Key
            http.addHeader("x-api-key", "eC8DP6R8nj6tXY2pUVMQG8CGfdCs2TOD4GslwV2E");
            // Specify content-type header
            http.addHeader("Content-Type", "text/plain");
            // Data to send with HTTP POST
            int httpResponseCode = http.POST(httpData);

            Serial.print("HTTP Response code: ");
            Serial.println(httpResponseCode);
            if ((httpResponseCode > 199) && (httpResponseCode < 204))
            {
                retCode = 0;
            }
            else if (httpResponseCode == -1)
            {
                Serial.printf("[HTTP] POST failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
                gServiceFound = false;
            }
            else if (httpResponseCode == 304)
            {
                Serial.println("Server has new firmware image available");
                retCode = 1; // Indicate there is a new firmware available.
            }
            http.end(); 
        }
    }
    return retCode;
}

#endif // Weather Sensor addition

#if 0 // Commented out
//~~ Function ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*!
    \brief  Function to find our data concentrator
    \param  Name of serivce
    \param  Protocol of service, tcp, udp, etc.
    \return false = Not found
*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
bool
findService(
  const char * service,
  const char * proto)
{
    int   nbrServices;

    if (!gMDNSInit)
    {
        if (MDNS.begin("Nelsony")) 
        {
            gMDNSInit = true;
            MDNS.addService("EngMon", "tcp", 80);
        }
        else
        {
            Serial.println("mDNS failed to start");
        }
    }
       
    if (!gServiceFound)
    {
        // Reset the Watchdog timer
        esp_task_wdt_reset();
        nbrServices = MDNS.queryService(service, proto);
        if (nbrServices == 0) 
        {
            Serial.println("No services were found.");
            gMDNSInit = false;
        } 
        else 
        {
            for (int i = 0; i < nbrServices; i++) 
            {
                Serial.print("Hostname: ");
                Serial.println(MDNS.hostname(i));
                Serial.print("IP address: ");
                Serial.println(MDNS.IP(i));
            
                Serial.print("Port: ");
                Serial.println(MDNS.port(i));
                strncpy(gServerIP, MDNS.IP(i).toString().c_str(), 20);
                gServerPort = MDNS.port(i);
                gServiceFound = true;
            }
        }
    }
    return gServiceFound;
}

//#ifdef REFER_CONTROLLER        
//~~ Function ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*!
    \brief  Function to send our information packet to the cloud
    \param  Data to send to the cloud
    \return 0 = success, 1 = firmware update available.

    304 Not Modified-
    This is used for caching purposes. It tells the client that the response 
    has not been modified, so the client can continue to use the same cached 
    version of the response.

    426 Upgrade Required - 
    The server refuses to perform the request using the current protocol 
    but might be willing to do so after the client upgrades to a different 
    protocol. The server sends an Upgrade header in a 426 response to 
    indicate the required protocol(s).

*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
int 
sendMessage(
    char * httpData)
{
    int   retries = 100;
    int   retCode = 0;    

    if (connectWifi())
    {
        HTTPClient http;

        if (findService("nelsony", "tcp"))
        {
            sprintf(pURL, "http://%s:%i/api/data?devID=%s&version=%s", 
                gServerIP, gServerPort, deviceID, VERSION_NUM);
            Serial.print("Connecting to website: ");
            Serial.println(pURL);
        }
        else
        {
            // Use default AWS Cloud address instead
            sprintf(pURL, "https://0wxz6aevv2.execute-api.us-west-2.amazonaws.com/test/upload?devID=%s&version=%s", 
                deviceID, VERSION_NUM);
            Serial.print("Connecting to website: ");
            Serial.println(pURL);
        }
        // Reset the Watchdog timer
        esp_task_wdt_reset();
        http.begin(pURL); //, test_root_ca);
        http.setConnectTimeout(50000);
        http.setTimeout(50000);
        // Add the API Key
        http.addHeader("x-api-key", "eC8DP6R8nj6tXY2pUVMQG8CGfdCs2TOD4GslwV2E");
        // Specify content-type header
        http.addHeader("Content-Type", "text/plain");
        // Data to send with HTTP POST
        int httpResponseCode = http.POST(httpData);

        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        
        if (httpResponseCode == -1)
        {
            Serial.printf("[HTTP] POST failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
            gServiceFound = false;
        }
        else if (httpResponseCode == 304)
        {
            Serial.println("Server has new firmware image available");
            retCode = 1; // Indicate there is a new firmware available.
        }
        http.end(); 
    }
    return retCode;
}
//#endif // REFER_CONTROLLER        


//~~ Function ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*!
    \brief  Function to check for updated firmware
    \return If firmware updated, function causes a reboot to occur else 0
*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
int
checkFOTA(
    )
{
    int retries = 100;

    // Check for WiFi connection
    if ((WiFi.status() == WL_CONNECTED)) 
    {
        HTTPClient http;

        if (findService("nelsony", "tcp"))
        {
            sprintf(pURL, "http://%s:%i/api/update?devID=%s&version=%s", 
                    gServerIP, gServerPort, deviceID, VERSION_NUM);
            Serial.print("Connecting to website: ");
            Serial.println(pURL);
        }
#ifdef ENGINE_MONITOR
        else
        {
            return 0;
        }
#endif // ENGINE_MONITOR
        
#ifdef REFER_CONTROLLER        
        else
        {
            // Use default AWS Cloud address instead
            sprintf(pURL, "https://0wxz6aevv2.execute-api.us-west-2.amazonaws.com/test/update?devID=%s&version=%s", 
                deviceID, VERSION_NUM);
            Serial.print("Connecting to website: ");
            Serial.println(pURL);
        }
#endif // REFER_CONTROLLER        

        // Reset the Watchdog timer
        esp_task_wdt_reset();
        http.setConnectTimeout(50000);
        http.setTimeout(50000);
        http.begin(pURL); //, test_root_ca);
        // Add the API Key
        http.addHeader("x-api-key", "eC8DP6R8nj6tXY2pUVMQG8CGfdCs2TOD4GslwV2E");
        // Specify content-type header
        http.addHeader("Content-Type", "text/plain");
        // Check for Update with HTTP GET
        int httpResponseCode = http.GET();
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        if (httpResponseCode != 200)
        {
            if (httpResponseCode != 304)
            {
                Serial.printf("[HTTP] POST failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
            }
        }
        else
        {
            totalLength = http.getSize();
            Serial.print("Image size: ");
            Serial.println(totalLength);
            if (totalLength > 0)
            {
                int len = totalLength;
                // this is required to start firmware update process
                Update.begin(UPDATE_SIZE_UNKNOWN);
                // create buffer for read
                uint8_t buff[128] = { 0 };
                // get tcp stream
                WiFiClient * stream = http.getStreamPtr();
                // read all data from server
                Serial.println("Updating firmware...");
                while(http.connected() && 
                    (len > 0 || len == -1)) 
                {
                    // Reset the Watchdog timer
                    esp_task_wdt_reset();
                    // get available data size
                    size_t size = stream->available();
                    if (size) 
                    {
                        // read up to 128 byte
                        int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
                        // pass to function
                        updateFirmware(buff, c);
                        if (len > 0) 
                        {
                            len -= c;
                        }
                    }
                    delay(1);
                }            
            }
        }
        http.end(); 
    }
    Serial.flush();
    return 0;
}


// 
//~~ Function ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/*!
    \brief  Function to update firmware incrementally
        Buffer is declared to be 128 so chunks of 128 bytes
          from firmware is written to device until server closes
    \return nothing;
*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void 
updateFirmware(
    uint8_t *data, 
    size_t len)
{
    Update.write(data, len);
    currentLength += len;
    // Print dots while waiting for update to finish
    Serial.print('.');
    // if current length of written firmware is not equal to total firmware size, repeat
    if(currentLength < totalLength) 
        return;

    Update.end(true);
    Serial.printf("\nUpdate Success, Total Size: %u\nRebooting...\n", currentLength);
    // Restart ESP32 to see changes 
    ESP.restart();
}
#endif // 0
