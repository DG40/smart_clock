/*
 *   **********************
 *   ***** LIBRARIES: *****
 *   **********************
 */

#include <SPI.h> // Interaction between the Arduino board and the W5100 Ethernet module occurs via the SPI protocol.
#include "FastLED.h"
#include <Ethernet.h>
#include <Dns.h> // Arduino DNS client for WizNet5100-based Ethernet shield.
#include <TimeLib.h>  
#include <iarduino_RTC.h> // Connecting the library for the RTC clock module.
// iarduino_RTC time(RTC_DS1302,43,39,41); // If using RTC_DS1302. Parameters - module name and connection pins (RST, CLK, DAT).
iarduino_RTC time(RTC_DS3231); // If using RTC_DS3231 (the SDA and SLA pins of the module are connected to the SDA and SCL ports of the Arduino board)


/*
 *   **********************
 *   ***** VARIABLES: *****
 *   **********************
 */

// For buttons:
byte deBounce = 255; // [ms] - time during which a repeated button press is not perceived (to eliminate chatter)
volatile unsigned long BtnMillis; // To eliminate chatter (last click time, ms)
volatile byte Mode = 0; // Режим

/*
  * Modes:
  * 1 - time synchronization with the RTC module;
  * 2 - time synchronization with NTP server;
  * 3 - display of light-dynamic scenarios;
*/

//#define MODULE  "ENC28J60" //Uncomment if your using ENC28J60
#define MODULE "W5500"
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED}; //You can customize your burn-in-address here
unsigned int localPort = 8888;      // local port to listen for UDP packets
IPAddress timeServer(172,21,1,1);
const int NTP_PACKET_SIZE= 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets 
EthernetUDP Udp; // Set UDP instance to let us send and receive packets over UDP

#define LED_TYPE    NEOPIXEL
#define NUM_STRIPS 12 // Указываем количество лент.
#define NUM_LEDS_PER_STRIP 29 // Указываем количество пикселей у каждой ленты.
CRGB leds[NUM_STRIPS][NUM_LEDS_PER_STRIP];

unsigned int PrsIntervalNTP=10*1000; // [sec*1000]=[ms] - after what number of milliseconds to re-request the time from the NTP server.
unsigned int PrsIntervalDraw=10*1000; // [sec*1000]=[ms] - after what number of milliseconds to change the clock drawing mode.
byte Hrs; // Current number of hours.
byte Mins = 3; // Current number of minutes.
byte Scnds; // The current number of seconds.

byte ColorMatrix[NUM_STRIPS][NUM_LEDS_PER_STRIP] = { }; // An array of numbers corresponding to the colors of the LED strip pixels, [rows][columns] in size, initially filled with zeros.
byte j_mins; // Number of burning pixels corresponding to minutes.
byte i_scnds; // The number of the "hand" in which the pixel corresponding to seconds is lit.

byte ShiftUTC = 3; // Offset relative to UTC+0 (UTC+3:00 - Moscow time).
unsigned long previousEpoch; // The time value taken from the server (in milliseconds since 1970) obtained from the last request.
unsigned long currentEpoch; // The time value taken from the server (in milliseconds since 1970) received from the current request.
unsigned long previousMillisDraw; // The number of milliseconds since the last render mode change.
unsigned long previousMillisNTP; // The number of milliseconds since the last time update.
unsigned long previousMillis;
unsigned long currentMillis; // Number of milliseconds elapsed since the program started

bool Init = false; // Has the "BeginEthernet" procedure (Ethernet connection, DNS client and UDP protocol initialization) been carried out in "Mode 2".
bool FlagFirstTry = false; // Whether the first time synchronization with the NTP server has been performed.
bool FlagNTP = false; // Whether the time has been synchronized with the NTP server since the previous synchronization.
bool FlagPC = false; // Previous-current - whether we have updated previous since the previous time synchronization with the NTP server.
bool FlagDraw = false; // To change the clock drawing mode.


 /*
 *    MAIN METHODS:
 */

// ***** CONFIGURE ETHERNET CONNECTION: *****
void BeginEthernet()
{
  if (!Ethernet.begin(mac) ) 
  {
    digitalWrite(5,LOW);
    Serial.println("Failed to configure Ethernet using DHCP");
    Serial.println("Please select other mode (NTP-mode is not available now)");
    while(Mode == 2) { } // Waits for the user to change mode
  }
  if (Mode == 2) // ("if" for correct working after fail connection)
  {
      DNSClient dns; // (set a name for the DNS client)
      dns.begin(Ethernet.dnsServerIP()); // (Ethernet.dnsServerIP() returns an IP address)
      
      Udp.begin(localPort); // UDP protocol initialization for port "localPort"
      Init = true; // Ethernet, DNS and UDP initialization completed successfully.
      
  }
}


// ***** FUNCTION FOR SENDING A REQUEST TO THE NTP SERVER (located at "address") *****
// Returns a packet with data about the current time value:

unsigned long SendNTPpacket(IPAddress& address) //Sending RQ to NTP time server 
{
  memset(packetBuffer, 0, NTP_PACKET_SIZE);    // Set all buffer turn 0 - всё, что есть в буфере, обнуляется.
                                               // memset(string, 'b', N); - заменяет в строке string первые N символов на "b"
                                               // packetBuffer - NTP Message
  /* 
   * Let's learn NTP-data format.
   * We need to set LI (leap indicator) - 2 bits, Version number - 3 bits, Mode - 3 bits. That all is 8 bit
    
   * 1. Leap Indicator (LI): This is a two-bit code warning of an impending leap second to be inserted/deleted in the last minute of the current day, 
        with bit 0 and bit 1, respectively, coded as follows:
            00 no warning
            01 last minute has 61 seconds
            10 last minute has 59 seconds)
            11 alarm condition (clock not synchronized)
       
     So our case of LI is "11" (alarm condition (clock not synchronized) - look below "0b>11<100011")
   
   * 2. Version Number (VN): This is a three-bit integer indicating the NTP version number, currently three (3).
    
     So our case of VN is "100" (look below "0b11>100<011"). It's equal "4"
    
   * 3. Mode: This is a three-bit integer indicating the mode, with values defined as follows:
          0 reserved
          1 symmetric active
          2 symmetric passive
          3 client
          4 server
          5 broadcast
          6 reserved for NTP control message (see Appendix B)
          7 reserved for private use
        
     So our case of Mode is "011" (look below "0b11100>011<"). It's equal "3" and means "client"
   */
   
  packetBuffer[0] = 0b11100011;   // First part of NTP-data: Leap Indicator (2 bits), Version number (3 bits) and Mode (3 bits) = 8 bit = 1 byte
    
  packetBuffer[1] = 0;     // Set Stratum (0-15), or type of clock 
                           // Zero - the highest level timing (the error does not exceed 1 second in 300,000 years)
                           
  packetBuffer[2] = 6;     // Set Polling Interval
                           // This is the polling interval. Signed 8-bit number indicating the maximum interval between consecutive messages in seconds
                           
  packetBuffer[3] = 0xEC;  // Set Peer Clock Precision
                           // This is the accuracy of the local clock, a signed 8-bit number indicating the accuracy of the local clock in seconds

  // Bytes 4 to 11 are skipped, but they are zeros from the execution of the memset function:
  // Set to 8 bytes of zero for Root Delay (4 bytes) & Root Dispersion (4 bytes).
  // Root Delay - total round-trip delay (here it is equal to zero, since we do not specify).
  // Root Dispersion - total variance relative to the reference clock (here it is equal to zero, since we do not specify).

  // Reference Clock Identifier (4 bytes):
  packetBuffer[12]  = 49;  
  packetBuffer[13]  = 0x4E; // = 78
  packetBuffer[14]  = 49; 
  packetBuffer[15]  = 52; 
  // (Reference Clock Identifier is a reference identifier identifying a specific server or reference clock)

  // Bytes from 16 to the end (47) are also skipped, they are left zero from the execution of the memset function:
    // Reference Timestamp = packetBuffer[16-23] = 0
    // Originate Timestamp = packetBuffer[24-31] = 0
    // Receive Timestamp = packetBuffer[32-39] = 0
    // Transmit Timestamp = packetBuffer[40-47] = 0
    // Authenticator (optional) = packetBuffer[48-63] at the beginning we declared a buffer size of 48 bytes.

  
  // All NTP fields have been given values, now we can send a packet requesting a timestamp:            
  Udp.beginPacket(address, 123); // Initializing a data packet to be sent to the server "address" port 123. NTP requests are made on port 123.
  Udp.write(packetBuffer,NTP_PACKET_SIZE); // Write a packet with a time request (to send it to the server)
                                           /* It must be enclosed between "beginPacket" and "endPacket"
                                               Syntax:
                                                   * UDP.write(message);
                                                   * UDP.write(buffer, size);
                                               Parameters:
                                                   * message: the outgoing message (char)
                                                   * buffer: an array to send as a series of bytes (byte or char)
                                                   * size: the length of the buffer
                                               */

  Udp.endPacket(); // Completion of operations with the formation of a data packet and sending it to the "address" server.
}


// ***** GET TIME FROM NTP SERVER: *****

void GetTimeNTP()
{                         
    if (!Init)       // If we have not yet successfully initialized Ethernet, DNS and UDP after changing the mode to Mode 2),
    BeginEthernet(); // then we try to initialize Ethernet, DNS and UDP.
    
    if (Mode == 2) // (crutch for fails in activating Ethernet, DNS and UDP)
    {
        SendNTPpacket(timeServer); // send an NTP packet to a time server (sending a time request to the NTP server located at "address")
        // and it receives a data packet from there (if successful, with time data)
            
        if ( Udp.parsePacket() ) // Checks for a response from the server in the form of a UDP packet (returns the size of the packet in int format)
        {   
           Udp.read(packetBuffer,NTP_PACKET_SIZE);  // read the packet into the buffer
           unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
           unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);  
           unsigned long secsSince1900 = highWord << 16 | lowWord;      
           // now convert NTP time into everyday time:
           // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
           const unsigned long seventyYears = 2208988800UL;     
           currentEpoch = secsSince1900 - seventyYears; 
           
           if (!FlagPC)
           {
              previousEpoch = currentEpoch;
              FlagPC = true; // flag that previousEpoch has been updated
           }
              
           if (currentEpoch != previousEpoch)
           {
               FlagNTP = true; // Done with time request. Its components are assigned to the variables Hrs, Mins and Scnds:
               Hrs = ((currentEpoch  % 86400L) / 3600);  
               // Convert UTC+0 to Moscow time (UTC+3:00):
               if (Hrs + ShiftUTC < 24)
                  Hrs += ShiftUTC;
               else
                  Hrs += ShiftUTC - 24;
               Mins = ((currentEpoch  % 3600) / 60);    
               Scnds = (currentEpoch %60);   
           }
        } 
        else 
        {
           Serial.println("Waiting packets...");
        }
    }
    else
    FlagNTP = true; // (so that when changing the mode after the fails in the initialization of Ethernet, DNS and UDP, we
                  // skipped time request retry blocks in the main program)
                  // (in other words, we pretended that the time was received, and we don't need to be here anymore)
}


// ***** DRAWING THE CLOCK (the first representation of time): *****

void DrawClock1(byte tHrs, byte tMins, byte tScnds)
{ 
      j_mins = floor(tMins*NUM_LEDS_PER_STRIP/60); // Round down (in my case, one more LED lights up in the "beam" for every 2 minutes)
      i_scnds = floor(tScnds*12/60); // Round down (in my case, every 5 seconds, the LED in the next "beam" lights up)
      
      // To organize the clock time (12-hour clock):
      if (tHrs >= 12)
      {
        tHrs -= 12;
      }
  
      // "Colorize" the clock matrix:
      for ( byte i = 0; i < NUM_STRIPS; ++i ) // (loop through 12 lines corresponding to 12 "arrows")
      {
          for ( byte j = 0; j < NUM_LEDS_PER_STRIP; ++j ) // (loop through 29 LEDs in one "arrow")
          {
               if (i < i_scnds)
                    ColorMatrix[ i ][ j_mins ] = 3;
               else
                    ColorMatrix[ i ][ j ] = 0;
                
               if (i < tHrs)
                  ColorMatrix[ i ][ j ] = 2;
                    
               if (i == tHrs)
               {
                  if (j < j_mins)
                      ColorMatrix[ i ][ j ] = 2;
                  else
                      ColorMatrix[ i ][ j ] = 1;
                }
            }
      }
      
      // Top arrow (noon):
      for ( byte j = 0; j < NUM_LEDS_PER_STRIP; ++j )
          ColorMatrix[ 11 ][ j ] = 4;
      ColorMatrix[ 11 ][ j_mins - 1 ] = 3;


      // Draw the clock matrix:
      for(byte x = 0; x < NUM_STRIPS; x++) 
      {
          for(byte i = 0; i < NUM_LEDS_PER_STRIP; i++) 
          {
              switch(ColorMatrix[ x ][ i ])
              {
                    case 0: // not participating
                    {
                        leds[x][i].red   = 150;
                        leds[x][i].green = 150;
                        leds[x][i].blue  = 150;
                    }
                    break;
                        
                    case 1: // Hour
                    {
                        leds[x][i].red   = 150;
                        leds[x][i].green = 150;
                        leds[x][i].blue  = 150;
                    }
                    break;
                    
                    case 2: // Minutes (and elapsed hours)
                        leds[x][i].red   = 20*x;
                        leds[x][i].green = 0;
                        leds[x][i].blue  = 255-20*x;
                    break;
                    
                    case 3: // Seconds
                        leds[x][i] = CRGB::Black;
                    break;

                    case 4: // Top arrow (noon)
                        leds[x][i].red   = 0;
                        leds[x][i].green = 0;
                        leds[x][i].blue  = 255;
                    break;
               }
          }
        
      }
      FastLED.show();

      // Smooth blinking:
      for(byte k = 1; k < 26; k++)
      {    
            for(byte x = 0; x < NUM_STRIPS; x++) 
            {
                
                if (x < i_scnds)  
                {   
                    leds[x][j_mins].red   = 9*k;
                    leds[x][j_mins].green = 9*k;
                    leds[x][j_mins].blue  = 9*k;
                }
            }
                    leds[11][j_mins].red   = 9*k;
                    leds[11][j_mins].green = 9*k;
                    leds[11][j_mins].blue  = 9*k;
                   
            FastLED.show();
            delay(10);
      }
      
      for(byte k = 26; k > 1; k--)
      {    
            for(byte x = 0; x < NUM_STRIPS; x++) 
            {
                
                if (x < i_scnds)  
                {   
                    leds[x][j_mins].red   = 9*k;
                    leds[x][j_mins].green = 9*k;
                    leds[x][j_mins].blue  = 9*k;
                }
            }
                    leds[11][j_mins].red   = 9*k;
                    leds[11][j_mins].green = 9*k;
                    leds[11][j_mins].blue  = 9*k;  
                 
            FastLED.show();
            delay(10);
      }
    
}


// ***** DRAWING THE CLOCK (the second representation of time): *****

void DrawClock2(byte tHrs, byte tMins, byte tScnds)
{ 
      // To organize the clock time (12-hour clock):
      if (tHrs > 12)
      {
        tHrs -= 12;
      }

      for(byte x = 0; x < NUM_STRIPS; x++)
      {    
          for(byte j = 0; j < NUM_LEDS_PER_STRIP; ++j)
          {
               leds[x][j].red   = 150;
               leds[x][j].green = 150;
               leds[x][j].blue  = 150;
          }
      }
      
      j_mins = floor(tMins*12/60);
      i_scnds = floor(tScnds*12/60);
      
      // Minute hand:
      for(byte j = 0; j < floor(NUM_LEDS_PER_STRIP); ++j) 
          {
               leds[j_mins][j].red   = 220;
               leds[j_mins][j].green = 0;
               leds[j_mins][j].blue  = 80;
          }
          
      // Second hand:
      for(byte j = 0; j < floor(NUM_LEDS_PER_STRIP); ++j) 
          {
               leds[i_scnds][j].red   = 170;
               leds[i_scnds][j].green = 0;
               leds[i_scnds][j].blue  = 170;
          }

      // Hour hand:
      for(byte j = 0; j < NUM_LEDS_PER_STRIP/2; ++j) 
          {
               leds[tHrs-1][j].red   = 100;
               leds[tHrs-1][j].green = 0;
               leds[tHrs-1][j].blue  = 255;
          }
       
      FastLED.show();
      delay(10);
}



// ***** Scenario "RAINBOW IN A CIRCLE" (all LEDs in a tape of the same color): *****

void RainbowStrips(int SpeedDelay) 
{
    byte *c;
    uint16_t i, j;
    for(j=0; j<256; j++) // Cycle of all colors on wheel
    { 
        for(int x = 0; x < NUM_STRIPS; x++) 
        {
            c=Wheel(((x * 256 / NUM_LEDS_PER_STRIP) + j) & 255);
            for(int i = 0; i < NUM_LEDS_PER_STRIP; i++)
            {
                setPixel(x, i, *c, *(c+1), *(c+2));
            }
            FastLED.show();
            delay(SpeedDelay);
        }
    }
}



// ***** Scenario "RAINBOW ALONG THE LENGTH OF TAPE": *****
          
void RainbowCycle(int SpeedDelay) 
{
    byte *c;
    int16_t i, j;
    byte sch = 1;
    if (sch < 2)
    {
        for(j=0; j<256; j++) // Cycle of all colors on wheel
        { 
            for(int x = 0; x < NUM_STRIPS; x++) 
            {
                for(int i = 0; i < NUM_LEDS_PER_STRIP; i++)
                {
                    c=Wheel(((i * 256 / NUM_LEDS_PER_STRIP) + j) & 255);
                    setPixel(x, i, *c, *(c+1), *(c+2));
                }
                FastLED.show();
                delay(SpeedDelay);
            }
            sch++;
        }
    }
}

          

 /*
 *    SECONDARY METHODS AND FUNCTIONS:
 */

// ***** BUTTON CLICK HANDLER: *****
void Intrpt1() 
{
  if (millis() - BtnMillis >= deBounce) 
  {
    BtnMillis = millis();
    Mode += 1;
    if (Mode > 3)
      Mode = 1;
    Serial.print("You've selected Mode ");
    Serial.println(Mode);
    digitalWrite(4,LOW);
    digitalWrite(5,LOW);
  }
}


// ***** PRINT CURRENT TIME TO SERIAL MONITOR: *****
void PrintTime(char * title, byte tHrs, byte tMins, byte tScnds) 
{
   Serial.print(title);
   Serial.print(tHrs);
   Serial.print(':');
   Serial.print(tMins);
   Serial.print(':');
   Serial.println(tScnds);
}


// ***** COLOR WHEEL: *****
 
byte * Wheel(byte WheelPos) 
{
    static byte c[3];
    if(WheelPos < 85) 
    {
        c[0]=WheelPos * 3;
        c[1]=255 - WheelPos * 3;
        c[2]=0;
    } 
    else if(WheelPos < 170)
    {
        WheelPos -= 85;
        c[0]=255 - WheelPos * 3;
        c[1]=0;
        c[2]=WheelPos * 3;
    } 
    else 
    {
        WheelPos -= 170;
        c[0]=0;
        c[1]=WheelPos * 3;
        c[2]=255 - WheelPos * 3;
    }
    return c;
}

  
// ***** SET PIXEL COLOR: *****

void setPixel(byte Strip, byte Pixel, byte red, byte green, byte blue) 
{
    leds[Strip][Pixel].red = red;
    leds[Strip][Pixel].green = green;
    leds[Strip][Pixel].blue = blue;
}


/*
 *   **********************
 *   ******* SETUP: *******
 *   **********************
 */

void setup() 
{
  delay(1000);
  Serial.begin(9600);

  // Кнопки:
  attachInterrupt(0, Intrpt1, FALLING); // Pin D1 - Mode 1 (RTC-mode), Mode 2 (Ethernet-mode), Mode 3 (Scenarios).

  // Индикаторы:
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
  digitalWrite(4,LOW);
  digitalWrite(5,LOW);
  
  // RTC-модуль:
  
  time.begin();          // Initialize work with the RTC module
  time.period(10);      // Specify to access the RTC module no more than once every 10 minutes

  // If you want to set a new time in the RTC module manually, uncomment the line below, set the necessary time parameters, program the board,
  // comment out and reprogram the board (so that the time is not reset to these values each time the clock is restarted).
  // time.settime(55,55,15,14,4,20,2);  // sec, min, hour, day, month, year, day of the week

  FastLED.addLeds<LED_TYPE, 30>(leds[0], NUM_LEDS_PER_STRIP); // <LED type, ribbon connection pin, (color order)>(variable ribbon leds, number of LEDs in it)
  FastLED.addLeds<LED_TYPE, 28>(leds[1], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<LED_TYPE, 26>(leds[2], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<LED_TYPE, 24>(leds[3], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<LED_TYPE, 22>(leds[4], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<LED_TYPE, 44>(leds[5], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<LED_TYPE, 42>(leds[6], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<LED_TYPE, 40>(leds[7], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<LED_TYPE, 38>(leds[8], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<LED_TYPE, 36>(leds[9], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<LED_TYPE, 34>(leds[10], NUM_LEDS_PER_STRIP);
  FastLED.addLeds<LED_TYPE, 32>(leds[11], NUM_LEDS_PER_STRIP);

  delay( 3000 ); // (when using the "FastLED" library, it is recommended to make this pause before working with the LED strip)

  for(byte x = 0; x < NUM_STRIPS; x++) 
      {
              for(byte i = 0; i < NUM_LEDS_PER_STRIP; i++) 
              {
                      leds[x][i].red   = 150;
                      leds[x][i].green = 150;
                      leds[x][i].blue  = 150;
              }
      }
  FastLED.show();
  Serial.println("LEDs are clear now. Select mode (button 1: RTC, NTP, Scenarios)");
}  


/*
 *   *********************
 *   ******* LOOP: *******
 *   *********************
 */

void loop()
{
  if (Mode != 2)
  {
      Init = false; // To initialize the Ethernet connection, DNS client and UDP protocol on a new entry into the execution of Mode 2
                     // happened again, instead of spinning there endlessly in case of failure
  }
  currentMillis = millis();
  switch (Mode)
    {
      case 1: // RTC-mode:
      {
          digitalWrite(4,HIGH);
          digitalWrite(5,LOW);
          if ((currentMillis - previousMillis) >= 1000)
          {        
            previousMillis = currentMillis;   
            time.gettime();
            Hrs = time.Hours; // 0..24 (hours = 0..12)
            if (Hrs >= 24)
                Hrs = 0;
            Mins = time.minutes;
            Scnds = time.seconds;
          }
          PrintTime("Time from RTC-module: ", Hrs, Mins, Scnds);
      }
      break;
      case 2: // NTP-mode:
        {
            digitalWrite(4,LOW);
            digitalWrite(5,HIGH);
            while(!FlagFirstTry)
            {
               GetTimeNTP();
               if (FlagNTP)
               {
                  FlagFirstTry = true;
                  FlagNTP = false;
                  FlagPC = false;
                  
                  // If you want to synchronize the time in the RTC module with the time on the NTP server, uncomment the line below,
                  // flash the board, start the time synchronization mode with the NTP server, comment out and re-flash
                  // (so that the time is not reset every time you start the synchronization mode with the NTP server):
                  
                  // time.settime(Scnds,Mins,Hrs,14,4,20,2); // sec, min, hour, day, month, year, day of the week
               }
            }
                  
            if ((currentMillis - previousMillisNTP) >= PrsIntervalNTP)
            {   
               previousMillisNTP = currentMillis;       
               do GetTimeNTP();
               while(!FlagNTP);
               FlagNTP = false;
               FlagPC = false;
            }
              
          if ((currentMillis - previousMillis) >= 1000)
          {
                 previousMillis = currentMillis;
                 Scnds++;
                 
                 if (Scnds>59)
                 {
                     Scnds = 0;
                     Mins++;
                 }
                 if (Mins > 59)
                 {
                     Mins = 0;
                     Hrs++;
                 }
                 if (Hrs >= 24)
                 {
                     Hrs = 0;
                 }  
              }
              PrintTime("Time from NTP-server: ", Hrs, Mins, Scnds);
        }
        break;
        case 3: // SHOW-mode:
        {   
            digitalWrite(4,HIGH);
            digitalWrite(5,HIGH);
            // ...Your drawing scenarios...
            RainbowCycle(1);
        }
        break;
    }

    if (Mode != 0)
    {
        if (Mins<3)
        RainbowStrips(1);
        else
        {
            if ((currentMillis - previousMillisDraw) >= PrsIntervalDraw)
                {
                  previousMillisDraw = currentMillis;
                  FlagDraw = !FlagDraw; 
                }
            if (FlagDraw)
            DrawClock1(Hrs, Mins, Scnds);
            else
            DrawClock2(Hrs, Mins, Scnds);
        }
        Serial.print("You've selected Mode ");
        Serial.println(Mode);
    }
}
