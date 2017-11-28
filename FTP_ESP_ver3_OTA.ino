#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

/*
   FTP passive client for IDE v1.0.1 and w5100/w5200
   Posted October 2012 by SurferTim
   Modified 6 June 2015 by SurferTim
*/

#include <UIPEthernet.h>
#include <EEPROM.h>

#define BUSY 5
#define STATUS 2
// this must be unique
byte mac[] = {0x00, 0xAA, 0xBB, 0xCC, 0xDF, 0x02};

//DATA;20170417160810,;PH,7.2,pH,00;COD,20.2,mg/l,01;COLOR,7,Pt/Co,00;FLOW,232.4,m3/h,00;TEMP,32,°C,00;
//SET;0000;STATIONCODE_STATIONNAME;USERNAME;PASSWORD;LINKTOFOLDER
// change to your server
IPAddress *server;
unsigned char serverIp[5];
char ipSet = 0;

EthernetClient client;
EthernetClient dclient;

#define EPROM_MEMORY_SIZE 512

#define BUF_SIZE  250
unsigned char recBuff[BUF_SIZE];
unsigned char receiveIndex = 0;

char dateAndTimeBuff[15];

#define INIT              0
#define WAIT_DATA         1
#define ETH_PROC          2
#define STORE              3
#define SETUP             4
byte mainState = INIT;

#define RECEIVE_DATA    1
#define CHECK_DATA      2
#define PROCESSING      3
#define WAIT            4
byte dataStat = INIT;
byte flagData = 0;
byte flagSetup = 0;

#define RESET   0
#define CONNECT 1
#define LOGIN_SETUP 2
#define WRITE_DATA  3
#define FINISH      4
byte ethState = RESET;
byte attempt = 0;

#define TIME_SIZE 14
char stationName[50];
char userName[30];
char password[30];
char folderLink[50];

char responeBuffer[128];
char responeCount;
//yyyymmddhhmmss


signed long currentTime;
signed long next;

char *tStr;
void countToReset(void);
void BusyOn(void);
void BusyOff(void);
void BlinkLed();
byte FTPEthernetProcess();
void StoreData(void);
char Setup();
void clearArray(char * str, byte count);


const char* ssid = "BKIT ESP8266";
const char* wifi_password = "cselabc5c6";
char isOTAWifiConnected = 0;

void setup()
{
  Serial.begin(9600);
//============OTA=================
WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, wifi_password);
  
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection OTA Wifi Failed!");
    //delay(5000);
    //ESP.restart();
    isOTAWifiConnected = 0;
  }
  else {
    isOTAWifiConnected = 1;
    // Port defaults to 8266
    // ArduinoOTA.setPort(8266);
  
    // Hostname defaults to esp8266-[ChipID]
    // ArduinoOTA.setHostname("myesp8266");
  
    // No authentication by default
    // ArduinoOTA.setPassword((const char *)"cselabc5c6");
  
    ArduinoOTA.onStart([]() {
      //Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
      //Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      /*
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    */
    });
    ArduinoOTA.begin();
    Serial.println("Ready");
    Serial.print("OTA Wifi IP address: ");
    Serial.println(WiFi.localIP());
  }
//============END_OTA=============

  
  pinMode(BUSY, OUTPUT);
  pinMode(STATUS, OUTPUT);
  digitalWrite(STATUS, HIGH);
  BusyOn();
  delay(1000L);
  Serial.begin(9600);
  digitalWrite(STATUS, LOW);

//  Ethernet.begin(mac);
  EEPROM.begin(EPROM_MEMORY_SIZE);
  BusyOff();
  Serial.println("ESP Ethernet version 3.0\r\nFTP-15112017");
  next = millis();
  ESP.wdtEnable(WDTO_8S);
}

void loop()
{
//=============OTA Handle=================
  if (isOTAWifiConnected) {
    ArduinoOTA.handle();
  }
//========================================
  if (((signed long)(millis() - next)) > 0)
  {
    ESP.wdtFeed();
    next = millis() + 50;
    BlinkLed(); 
  }
  switch (mainState)
    {
      case INIT:
        mainState = SETUP;
       //Serial.println("set up");
        break;
      case WAIT_DATA:
        
        if (flagData == 1)
        {
          if (ipSet)
          {
            mainState = ETH_PROC;
            //Serial.println("eth process");
          }
          else
            flagData = 0;
        } else if (flagSetup == 1) {
          flagSetup = 0;
          mainState = STORE;
          //Serial.println("store");
        } else {
          DataProcess();
        }
        break;
      case ETH_PROC:
          BusyOn();
          FTPEthernetProcess();
          flagData = 0;
          BusyOff();
          mainState = WAIT_DATA;
        break;
      case STORE:
        
        {
          unsigned char i;
          EEPROM.write(0,0xFA);
          for (i = 4; i < BUF_SIZE; i++)
          {
            //Serial.print();
            EEPROM.write(i - 3, recBuff[i]);
          }
        }
        EEPROM.commit();
        mainState = SETUP;
        //Serial.println("Set up");
      case SETUP:
        
        if (Setup())
        {
          ipSet = 1;
          Serial.print(serverIp[0]);
          Serial.print(serverIp[1]);
          Serial.print(serverIp[2]);
          Serial.println(serverIp[3]);
          Serial.println(stationName);
          Serial.println(userName);
          Serial.println(password);
          Serial.println(folderLink);
        }
        else{
          ipSet = 0;
          //Serial.println("not set");
        }
          
          mainState = WAIT_DATA;
          //Serial.println("wait data");
        break;
      default:
        mainState = INIT;
    }
}
void DataProcess(void)
{
  unsigned char dataReceive = 0;
  byte countItem = 0;
  switch (dataStat)
  {
    case INIT:
      countItem = 0;
      flagData = 0;
      receiveIndex = 0;
      clearArray(recBuff, BUF_SIZE);
      dataStat = RECEIVE_DATA;
      break;
    case RECEIVE_DATA:
      if (Serial.available() > 0) {
        dataReceive = Serial.read();
        if ((dataReceive != 0x0d && dataReceive != 0x0a) || receiveIndex < 20)
        {
          recBuff[receiveIndex] = dataReceive;
          //Serial.print(recBuff[receiveIndex]);
          receiveIndex = (receiveIndex+1)%BUF_SIZE;
        }
        else if(dataReceive == 0x0a)
        {
          dataStat = CHECK_DATA;
          //Serial.println("check data");
        }
      }
      break;
    case CHECK_DATA:
      if (recBuff[0] == 'S' && recBuff[1] == 'E' && recBuff[2] == 'T')
      {
        flagSetup = 1;
      }
      else if (recBuff[0] == 'D' && recBuff[1] == 'A' && recBuff[2] == 'T')
      {
        flagData = 1;
      }
      else break;
      dataStat = PROCESSING;
      //Serial.println("process data");
    case PROCESSING:
      {
        int i;
        for (i = 0; i < BUF_SIZE ; i++)
        {
          if(flagSetup == 1 && (i == 4||i==5||i==6||i==7))
            continue;
          if (recBuff[i] == ';')
          {
            recBuff[i] = 0;
          }
          else if (recBuff[i] == ',')
            recBuff[i] = '\t';
        }
        recBuff[receiveIndex] = 0;
        recBuff[receiveIndex + 1] = 0;

        for(i = 0;i<14;i++)
        {
          dateAndTimeBuff[i] = recBuff[i+5];
        }
        dateAndTimeBuff[14] = 0;
        dataStat = WAIT;
      }
      break;
    case WAIT:
      if (flagSetup == 0 && flagData == 0)
        dataStat = INIT;
      break;
    default:
      dataStat = INIT;
      break;
  }
}


char Setup() {
  char  sStr;
  unsigned char i = 1;
  unsigned char j = 0;

  unsigned char programed;
  programed = EEPROM.read(0);
  if(programed != 0xFA)
    return 0;
  serverIp[0] = EEPROM.read(i++);
  serverIp[1] = EEPROM.read(i++);
  serverIp[2] = EEPROM.read(i++);
  serverIp[3] = EEPROM.read(i++);
  
  i++;
  if (server != NULL)
    delete server;
  server = new IPAddress(serverIp[0], serverIp[1], serverIp[2], serverIp[3]);
  for (j = 0; stationName[j] = EEPROM.read(i++); j++) {
  }
  for (j = 0; userName[j] = EEPROM.read(i++); j++) {
  }
  for (j = 0; password[j] = EEPROM.read(i++); j++) {
  }
  for (j = 0; folderLink[j] = EEPROM.read(i++); j++) {
  }
  return 1;
}
byte FTPEthernetProcess()
{
  attempt = attempt + 1;
  if (attempt > 3) ethState = RESET;
  switch (ethState) {
    case RESET:
      Serial.println(F("Resetting..."));
      attempt = 0;
      Ethernet.begin(mac);
      PrintInformation();
      ethState = CONNECT;
    case CONNECT:
      Serial.println(F("Connecting..."));
      if (client.connect(*server, 21)) {
        Serial.println(F("Sending..."));
        //ethState = LOGIN_SETUP;
        //break;
      }
      else {
        Serial.println(F("Connect failed"));
        delay(200);
        break;
      }
      if (!checkResponse()) break;
    case LOGIN_SETUP:

      client.print(F("USER "));
      client.println(userName);

      if (!checkResponse()) break;

      client.print(F("PASS "));
      client.println(password);
      if (!checkResponse()) break;

      client.println(F("OPTS utf8 on"));

      if (!checkResponse()) break;

      client.println(F("PWD"));

      if (!checkResponse()) break;

      client.println(F("Type I"));

      if (!checkResponse()) break;

      client.println(F("PASV"));

      if (!checkResponse()) break;

      tStr = strtok(responeBuffer, "(,");
      int array_pasv[6];
      for ( int i = 0; i < 6; i++) {
        tStr = strtok(NULL, "(,");
        array_pasv[i] = atoi(tStr);
        if (tStr == NULL) {
          break;
          //Serial.println(F("Bad PASV Answer"));
        }
      }

      unsigned int hiPort, loPort;
      hiPort = array_pasv[4] << 8;
      loPort = array_pasv[5] & 255;

      //Serial.print(F("Data port: "));
      hiPort = hiPort | loPort;
      //Serial.println(hiPort);

      if (dclient.connect(*server, hiPort)) {
        //Serial.println(F("Data connected "));

      }
      else {
        //Serial.println(F("Data connect failed"));
        client.stop();
        break;
      }
    case WRITE_DATA:
      {
        unsigned char * timePtr, *dataPtr;
        char dataPos = 21;
        client.print(F("STOR "));
        client.print(folderLink);
        client.print(stationName);
        client.print(F("_"));
        client.print(dateAndTimeBuff);
        client.println(F(".txt"));

        if (!checkResponse())
        {
          dclient.stop();
          break;
        }
        recBuff[4] = '\t';
        timePtr = recBuff;
        timePtr += 4;
        dataPtr = recBuff;
        dataPtr = dataPtr + dataPos;
        while (*dataPtr != 0)
        {
          char dataLength;
          dataLength = dataLen(dataPtr);
          dclient.write(dataPtr, dataLength);
          dataPtr = dataPtr + dataLength + 1;
          dclient.write(timePtr, 16);
          dclient.write(dataPtr, 2);
          dclient.write("\r\n", 2);
          dataPtr = dataPtr + 3;
        }
        dclient.stop();
        if (!checkResponse()) break;
        //ethState = FINISH;
    }

    case FINISH:
      client.println(F("QUIT"));
      if (!checkResponse()) break;
      client.stop();
      //delay(50);
      Serial.println(F("SEND COMPLETE"));
      attempt = 0;
      ethState = CONNECT;
      return 1;
    default: ethState = CONNECT;
  }
  return 0;
}
char dataLen(unsigned char* str){
  char i = 0;
  char tabCount = 0;
  while(tabCount < 3)
  {
    if(str[i] == '\t')
      tabCount++;
    i++;
  }
  return i-1;
}
void clearArray(unsigned char * str, byte count) {
  for (byte i = 0; i < count; i++) {
    str[i] = '\0';
  }
}
void arrayCopy(char * str1, char * str2) {
  byte i = 0, j = 0;
  while (str1[i] != NULL)
    i++;
  while (str2[j] != NULL) {
    str1[i] = str2[j];
    i++; j++;
  }
}

byte checkResponse()
{
  byte respCode;
  byte thisByte;
  while (!client.available()) delay(1);
  respCode = client.peek();

  responeCount = 0;

  while (client.available())
  {
    thisByte = client.read();

    if (responeCount < 127)
    {
      responeBuffer[responeCount] = thisByte;
      responeCount++;
      responeBuffer[responeCount] = '\0';
    }
  }

  if (respCode >= '4')
  {
    efail();
    return 0;
  }
  delay(100);
  return 1;
}
void BusyOn(void)
{
  digitalWrite(BUSY, HIGH);
}

void BusyOff(void)
{
  digitalWrite(BUSY, LOW);
}
void BlinkLed(void)
{
  static uint8_t cntOfLed = 0;
  cntOfLed = (cntOfLed + 1) % 20;
  if (cntOfLed < 10)
    digitalWrite(STATUS, LOW);
  else
    digitalWrite(STATUS, HIGH);
}
void efail()
{
  byte thisByte = 0;

  client.println(F("QUIT"));

  while (!client.available()) delay(1);

  while (client.available())
  {
    thisByte = client.read();
    //Serial.write(thisByte);
  }
  client.stop();
  Serial.println(F("Command disconnected"));
}
void PrintInformation(void)
{
  Serial.print(F("localIP: "));
  Serial.println(Ethernet.localIP());
  Serial.print(F("subnetMask: "));
  Serial.println(Ethernet.subnetMask());
  Serial.print(F("gatewayIP: "));
  Serial.println(Ethernet.gatewayIP());
  Serial.print(F("dnsServerIP: "));
  Serial.println(Ethernet.dnsServerIP());
}
