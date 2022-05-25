// Importing necessary libraries
// Requires AsyncTCP and ESPAsyncWebServer libraries that are not in the Arduino Libray Manager
// https://github.com/me-no-dev/AsyncTCP
// https://github.com/me-no-dev/ESPAsyncWebServer

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include "COMMS.h"

// Create the AsyncWebServer object 
AsyncWebServer server(80);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Nelsony Refrigeration Controller</title>
</head>
<body>
  <h1>Nelsony Refrigeration Controller</h1>
  <h2>Refrigeration Controller has an internal error. Please contact support.</h2>
</body>
</html>
)rawliteral";


// Replaces placeholder with button section in your web page
String processor(const String& var)
{
    //Serial.println(var);
    if (var == "FREEZERSETTEMP")
    {
        return String(fzrTempSet);
    }
    else if (var == "REFERSETTEMP")
    {
        return String(fanTempSet);
    }
    else if (var == "WIFISTATE")
    {
        String value = "";
        if (!(userFlags & UF_WIFI_ON))
            value += "disabled";
        return value;
    }
    else if (var == "WIFIBUTTON")
    {
        String buttons = "";
        if (userFlags & UF_WIFI_ON)
        {
            buttons += "checked";
        }
        return buttons;
    }    
    else if (var == "SSID_VALUE")
    {  
        String value = String(ssid);
        return value;
    }
    else if (var == "PSWD_VALUE")
    {  
        String value = String(password);
        return value;
    }
    else if (var == "IPADDRESS")
    {
        return sIPAddr;
    }
    else if (var == "VERSION_NUMBER")
    {  
        String value = String(VERSION_NUM);
        return value;
    }
    return String();
}


void 
ServerInit()
{
    if (!SPIFFS.begin(true))
    {
        Serial.println("An Error has occurred while mounting SPIFFS");
        SPIFFS_ERROR = true;
    }
    server.onNotFound([](AsyncWebServerRequest *request) 
    {
        if (request->method() == HTTP_OPTIONS) 
        {
            Serial.println("Recieved OPTIONS request");
            request->send(200);
        } 
        else 
        {
            Serial.println("Recieved onNotFound request");
            request->send(404);
        }
    });

    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        Serial.println("Recieved default page GET request");
        if (SPIFFS_ERROR)
        {
            request->send_P(200, "text/html", index_html); //, processor);
        }
        else
        {
            request->send(SPIFFS, "/index.html", String(), false, processor);
        }
    });

    // API Read of values
    server.on("/api", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        //Serial.println("Recieved API GET request");
        //Serial.print("X");
        if(request->hasHeader("x-api-key"))
        {
            AsyncWebHeader* h = request->getHeader("x-api-key");
            if (strstr(h->value().c_str(), "69c45815-ca2e-45e5-a2ea-e07aafb37595") != NULL)
            {
                AsyncWebServerResponse *response = request->beginResponse(200, "application/json", (char *)&updateBuf[0]);
                request->send(response);
            }
            else
            {
                Serial.println("Recieved API GET request bad key");
                request->send(401, "text/plain", "Unauthorized");  
            }
        }
        else
        {
            Serial.println("Recieved API GET request No API Key");
            request->send(404, "text/plain", "Not Found");
        }
    });

    // Route for canvas javascript gauge library
    server.on("/gauge.min.js", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        Serial.println("Recieved GET gauge.min.js request");
        request->send(SPIFFS, "/gauge.min.js");
    });

    // Route for style sheet
    server.on("/style.min.css", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        Serial.println("Recieved GET style.min.css request");
        request->send(SPIFFS, "/style.min.css");
    });

    // Route for settings page
    server.on("/settings.html", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        Serial.println("Recieved GET settings.html request");
        request->send(SPIFFS, "/settings.html", String(), false, processor);
        //request->send(SPIFFS, "/settings.html");
    });

    // Route for settings icon
    server.on("/settings-icon.png", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        Serial.println("Recieved GET settings-icon.png request");
        request->send(SPIFFS, "/settings-icon.png");
    });

    // Route for WiFi configuration
    server.on("/set", HTTP_POST, [](AsyncWebServerRequest *request)
    {
        int params = request->params();
        Serial.print("Recieved set POST with params = ");
        Serial.println(params);
        userFlags = 0;
        tryConnecting = true;
        
        for (int i=0; i < params; i++)
        {
            AsyncWebParameter* p = request->getParam(i);
            if (p->isPost())
            {
                Serial.printf("POST set[%s]: %s\n", p->name().c_str(), p->value().c_str());
                if (strstr(p->name().c_str(), "WiFi") != NULL)
                {
                    if (strstr(p->value().c_str(), "on") != NULL)
                    {
                        userFlags |= UF_WIFI_ON;
                    }
                }
                if (strstr(p->name().c_str(), "UpLoad") != NULL)
                {
                    if (strstr(p->value().c_str(), "on") != NULL)
                    {
                        userFlags |= UF_WIFI_ON;
                        userFlags |= UF_UPLOAD_ON;
                    }
                }
                else if (strstr(p->name().c_str(), "SSID") != NULL)
                {
                    strncpy(ssid, p->value().c_str(), 32);
                    Serial.print("SSID set to ");
                    Serial.println(ssid);
                    for (int i= 0; i < SSID_LENGTH; i++)
                    {
                        EEPROM.write(SSID_OFFSET + i, ssid[i]);
                    }
                    userFlags |= UF_WIFI_ON;
                }
                else if (strstr(p->name().c_str(), "PSWD") != NULL) 
                {
                    strncpy(password, p->value().c_str(), 64);
                    Serial.print("Password set to ");
                    Serial.println(password);
                    for (int i= 0; i < PSWD_LENGTH; i++)
                    {
                        EEPROM.write(PSWD_OFFSET + i, password[i]);
                    }
                    userFlags |= UF_WIFI_ON;
                }
            } 
        }        
        EEPROM.write(UFLAGS_OFFSET, userFlags);
        EEPROM.commit();
        //request->send(200, "text/plain", "OK");
       request->send(SPIFFS, "/index.html", String(), false, processor);
    });

    // Route for root / web page
    server.on("/setTemp", HTTP_POST, [](AsyncWebServerRequest *request)
    {
        int params = request->params();
        Serial.print("Recieved setTemp POST with params = ");
        Serial.println(params);
        
        for (int i=0; i < params; i++)
        {
            AsyncWebParameter* p = request->getParam(i);
            if (p->isPost())
            {
                Serial.printf("POST setTemp[%s]: %s\n", p->name().c_str(), p->value().c_str());
                if (strstr(p->name().c_str(), "fzrSetTemp") != NULL)
                {
                    fzrTempSet = p->value().toInt();
                    EEPROM.write(FZRST_OFFSET, fzrTempSet);
                    EEPROM.commit();
                }
                else if (strstr(p->name().c_str(), "refSetTemp") != NULL)
                {
                    fanTempSet = p->value().toInt();
                    EEPROM.write(REFST_OFFSET, fanTempSet);
                    EEPROM.commit();
                }
            }
        }
        request->send(SPIFFS, "/index.html"); //, String(), false; //, processor);
    });

    // Simple Firmware Update Form
    server.on("/fota", HTTP_GET, [](AsyncWebServerRequest *request) 
    {
        Serial.println("Recieved GET fota.html request");
        request->send(SPIFFS, "/fota.html", String(), false, processor);
    });
    
    server.on("/manifest", HTTP_POST, [](AsyncWebServerRequest *request) 
    {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain");
        response->addHeader("Connection", "close");
        request->send(response);
    }
    ,[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) 
    {
        if (strstr(filename.c_str(), ".html") != NULL)
        {
            if (!index) 
            {
                Serial.printf("Manifest Start:%s, %u B\n", filename.c_str(), len);
                // Reset the Watchdog timer
                esp_task_wdt_reset();
            }
            // Reset the Watchdog timer
            esp_task_wdt_reset();
            for (size_t i=0; i<len; i++)
            {
                Serial.write(data[i]);
            }
            if (final) 
            {
                // Reset the Watchdog timer
                esp_task_wdt_reset();
                Serial.printf("Manifest Completed: %u B\n", len);
            }
        }
        else
        {
            Serial.printf("Unknown spiffs file type:%s, %u B\n", filename.c_str(), len);
            request->send(SPIFFS, "/index.html", String(), false, processor);
        }
    });

    
    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) 
    {
        shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot?"OK":"FAIL");
        response->addHeader("Connection", "close");
        request->send(response);
    }
    ,[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) 
    {
        if (strstr(filename.c_str(), ".bin") != NULL)
        {
            if (!index) 
            {
                Serial.printf("Update Start: %s\n", filename.c_str());
                //Update.runAsync(true);
                // Reset the Watchdog timer
                esp_task_wdt_reset();
                if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000))
                {
                    Update.printError(Serial);
                }
            }
            if (!Update.hasError())
            {
                // Reset the Watchdog timer
                esp_task_wdt_reset();
                Serial.print("X");
                if (Update.write(data, len) != len) 
                {
                    Update.printError(Serial);
                }
            }
            if (final) 
            {
                // Reset the Watchdog timer
                esp_task_wdt_reset();
                Serial.println(" DONE");
                shouldReboot = !Update.hasError();
                if (Update.end(true)) 
                {
                    Serial.printf("Update Success: %uB\n", index+len);
                } 
                else 
                {
                    Update.printError(Serial);
                }
            }
        }
        else
        {
            Serial.printf("Unknown update file type:%s, %u B\n", filename.c_str(), len);
            shouldReboot = false;
            if (final) 
            {
                request->send(SPIFFS, "/index.html", String(), false, processor);
            }
        }
    });

    // Start server
    server.begin();
}
