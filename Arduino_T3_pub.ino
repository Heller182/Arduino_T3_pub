/*
 * Andreas Hellerschmied, 2020-02-22
 * 
 *  Pin connections:
 *  - RTC module (DS3231):
 *    - SCL => Arduino Uno A5 (I2C SCL)
 *    - SDA => Arduino Uno A4 (I2C SDA)
 *    - VCC => +5V
 *    - GND => GND
 *  - OLED Dsiplay: SH1106, 128X64
*/


#include <RTClib.h>               // https://adafruit.github.io/RTClib/html/index.html
#include <U8g2lib.h>              // https://github.com/olikraus/u8g2/wiki
#include <Wire.h>                 // I2C Bus
#include <OneWire.h>              // One Wire Bus
#include <DallasTemperature.h>    // DS18B20 Temperature Sensor
#include <Adafruit_Sensor.h>      // BME280 
#include <Adafruit_BME280.h>      // BME280
#include <button_handler.h>       // Detect button events



#define BUTTON1_PIN               2  // Button 1: 
                                     //  - change display (short press)
                                     //  - minus or down (short press)
                                     
#define BUTTON2_PIN               3  // Button 2:
                                     //  - confirm (long press)
                                     //  - plus or up (short press)
                                     //  - enter menu (long press)

#define LONGPRESS_LEN_MS         1000   // long button press duration [ms] (default = 1000)

//# define MIN_YEAR                2000  // Min year to be set by clock
//# define MAX_YEAR                2100  // Max year to be set by clock

#define ONE_WIRE_BUS              4    // DS18B20 data wire is connected to input pin D4

// For BME280
#define BME_SCK 13
#define BME_MISO 12
#define BME_MOSI 11
#define BME_CS 10
#define SEALEVELPRESSURE_HPA (1013.25)


/* Defined in button_handler.h => Not necessary here!
#define EV_NONE                  0 // no event
#define EV_SHORTPRESS            1 // button release before "longpress" time
#define EV_LONGPRESS_AUTO        2 // After DEFAULT_LONGPRESS_LEN ms (or the non-dafault time)
#define EV_LONGPRESS             3 // button release after "longpress" time
*/


// Instanciate button objects:
ButtonHandler button1(BUTTON1_PIN);
ButtonHandler button2(BUTTON2_PIN);

// Instanciate clock object:
RTC_DS3231 rtc;

// Constructor for display (U8G2_R0: no rotation, landscape):
//U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE); // 
//U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE); // page buffer mode
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// DS18B20 Temperature Sensor on One Wire Bus
DeviceAddress thermometerAddress;     // custom array type to hold 64 bit device address
OneWire oneWire(ONE_WIRE_BUS);        // create a oneWire instance to communicate with temperature IC
DallasTemperature tempSensor(&oneWire);  // pass the oneWire reference to Dallas Temperature

// Addresses of 2 DS18B20s:
// Sensor 1 : 0x28, 0xAA, 0x31, 0x07, 0x50, 0x14, 0x01, 0xB0
// Sensor 2 : 0x28, 0xAA, 0xF9, 0x05, 0x50, 0x14, 0x01, 0xAB
uint8_t sensor1[8] = { 0x28, 0xAA, 0x31, 0x07, 0x50, 0x14, 0x01, 0xB0 };
uint8_t sensor2[8] = { 0x28, 0xAA, 0xF9, 0x05, 0x50, 0x14, 0x01, 0xAB};

// BME280:
Adafruit_BME280 bme; // I2C




// Initialize global variables:
char time_str[15];                // time string
char date_str[15];                // date string

int current_sec; 
int old_sec;

unsigned long old_ms = 0;

uint8_t button1_event;  // Button 1 event code
uint8_t button2_event;  // Button 2 event code

uint8_t state;          // State machine state

bool flag_sera_on = true;

void setup() 
{
#ifndef ESP8266
  while (!Serial); // for Leonardo/Micro/Zero
#endif

  unsigned status;

  Serial.begin(9600); // Iitiale serial interface with 9600 baud rate
  delay(3000); // wait for console opening

  if (! rtc.begin()) {      // Actually, nothing is checked here, because rtc.begin() ALWAYS retruns true!
    Serial.println("Couldn't find RTC");
    while (1);
  }

  // ### Set RTC time and date ### 
  Serial.println("Setting up RTC");
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, lets set the time!");
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    //rtc.adjust(DateTime(2020, 3, 30, 12, 0, 0));
  }
  else {
    Serial.println("RTC did not lose power!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    //rtc.adjust(DateTime(2020, 3, 31, 23, 55, 00));
  }

  // ### Set up OLED Display ### 
  Serial.println("Setting up OLED display");
  u8g2.begin();

  // ### Set up buttons ### 
  button1.init();
  button2.init();

  // ### Set up DS18B20 temp sensor ### 
  tempSensor.begin();                         // initialize the temp sensor

  // ### Set up BME280 ###
  status = bme.begin(0x76);
    if (!status) {
        Serial.println("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
        Serial.print("SensorID was: 0x"); Serial.println(bme.sensorID(),16);
        while (1) delay(10);
    }

  // ### Init variables ###
  current_sec = 0;
  old_sec = 0;
  button1_event = EV_NONE;
  button2_event = EV_NONE;
  state = 3; // initial state (3 = all sensors)

  Serial.println("Setup complete!");

}


void loop()
{
  // Degug output:
  if (flag_sera_on) {
    Serial.print("Current state: ");
    Serial.println(state);
  }

  // ------------Buttons------------
  // read the state of the pushbutton value and process them:
  button1_event = button1.handle();
  button2_event = button2.handle();
  
  switch(state) {
    case 0: // display time and date
      display_date_time();
      if (button2_event == EV_LONGPRESS_AUTO) {
        state = 11; // enter clock edit mode
      }
      if (button1_event == EV_SHORTPRESS) {
        state = 2; // display time, date and temp1 (DS18B20 #1)
      }
      break;
    case 1: // enter edit mode
      // statements
      break;
    case 11: // edit year
      set_year(button1_event, button2_event);
      break;
    case 12: // edit month
      set_month(button1_event, button2_event);
      break;
    case 13: // edit day
      set_day(button1_event, button2_event);
      break;
    case 14: // edit hour
      set_hour(button1_event, button2_event);
      break;
    case 15: // edit minute
      set_minute(button1_event, button2_event);
      break;
    case 16: // edit second
      set_second(button1_event, button2_event);
      break;
      
    case 2: // display time, date and temp1 (DS18B20 #1)
      display_date_time_temp1();
      if (button1_event == EV_SHORTPRESS) {
        state = 3; // display time and date
      }
      break;
    case 3: // display all sensors
      display_all_sensors();
      if (button1_event == EV_SHORTPRESS) {
        state = 0; // display time and date
      }
      break;
  }
}


// Show the current date and time
void display_date_time() {
  DateTime now = rtc.now();
  current_sec = now.second();

  if (old_sec != current_sec) {

    sprintf(time_str, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    sprintf(date_str, "%02d/%02d/%04d", now.day(), now.month(), now.year());

    u8g2.setFont(u8g2_font_ncenB12_tr);  // choose a suitable font
    u8g2.firstPage();
    do {
      u8g2.drawStr(0,20,time_str);  // write something to the internal memory
      u8g2.drawStr(0,40,date_str);  // write something to the internal memory
    } while (u8g2.nextPage());

    old_sec = current_sec;
  }
}


void set_year(uint8_t button1_event, uint8_t button2_event) {
  DateTime now = rtc.now();
  
  DateTime new_time;
  char buf2[] = "YYYY-MM-DD hh:mm:ss";
  
  if (button2_event == EV_SHORTPRESS) {
    if (now.year() <= 2100) {
      rtc.adjust(DateTime(now.year()+1, now.month(), now.day(), now.hour(), now.minute(), now.second()));
    }
  }

  if (button1_event == EV_SHORTPRESS) {
    if (now.year() > 2000) {
      rtc.adjust(DateTime(now.year()-1, now.month(), now.day(), now.hour(), now.minute(), now.second()));
    }
  }

  sprintf(time_str, "Set year: %02d", now.year());
  u8g2.setFont(u8g2_font_ncenB12_tr);  // choose a suitable font
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB08_tr);  // choose a suitable font
    u8g2.drawStr(0, 10, now.toString(buf2));   // write something to the internal memory
    u8g2.setFont(u8g2_font_ncenB12_tr);  // choose a suitable font
    u8g2.drawStr(0, 40, time_str);  // write something to the internal memory
  } while (u8g2.nextPage());
  
  if (button2_event == EV_LONGPRESS_AUTO) 
    state = 12; // set month
}


void set_month(uint8_t button1_event, uint8_t button2_event) {
  DateTime now = rtc.now();
  DateTime new_time;
  char buf2[] = "YYYY-MM-DD hh:mm:ss";
  
  if (button2_event == EV_SHORTPRESS) {
    TimeSpan t_diff = (DateTime(now.year(), now.month()+1, now.day(), now.hour(), now.minute(), now.second()) -  now);
    if (t_diff.days() <= 31) {
      new_time = (now + t_diff); // 1 month
      rtc.adjust(new_time);
    }
  }

  if (button1_event == EV_SHORTPRESS) {
    TimeSpan t_diff = (now - DateTime(now.year(), now.month()-1, now.day(), now.hour(), now.minute(), now.second()));
    new_time = (now - t_diff);
    rtc.adjust(new_time);
  }

  sprintf(time_str, "Set month: %02d", now.month());
  u8g2.setFont(u8g2_font_ncenB12_tr);  // choose a suitable font
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB08_tr);  // choose a suitable font
    u8g2.drawStr(0, 10, now.toString(buf2));   // write something to the internal memory
    u8g2.setFont(u8g2_font_ncenB12_tr);  // choose a suitable font
    u8g2.drawStr(0, 40, time_str);  // write something to the internal memory
  } while (u8g2.nextPage());
  
  if (button2_event == EV_LONGPRESS_AUTO) 
    state = 13; // set day
}


void set_day(uint8_t button1_event, uint8_t button2_event) {
  DateTime now = rtc.now();
  DateTime new_time;
  char buf2[] = "YYYY-MM-DD hh:mm:ss";
  
  if (button2_event == EV_SHORTPRESS) {
    new_time = (now + TimeSpan (1, 0, 0, 0)); // 1 day
    rtc.adjust(new_time);
  }

  if (button1_event == EV_SHORTPRESS) {
    new_time = (now - TimeSpan (1, 0, 0, 0));
    rtc.adjust(new_time);
  }

  sprintf(time_str, "Set day: %02d", now.day());
  u8g2.setFont(u8g2_font_ncenB12_tr);  // choose a suitable font
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB08_tr);  // choose a suitable font
    u8g2.drawStr(0, 10, now.toString(buf2));   // write something to the internal memory
    u8g2.setFont(u8g2_font_ncenB12_tr);  // choose a suitable font
    u8g2.drawStr(0, 40, time_str);  // write something to the internal memory
  } while (u8g2.nextPage());
  
  if (button2_event == EV_LONGPRESS_AUTO) 
    state = 14; // set hour
}


void set_hour(uint8_t button1_event, uint8_t button2_event) {
  DateTime now = rtc.now();
  DateTime new_time;
  char buf2[] = "YYYY-MM-DD hh:mm:ss";
  
  if (button2_event == EV_SHORTPRESS) {
    new_time = (now + TimeSpan(3600));
    rtc.adjust(new_time);
  }

  if (button1_event == EV_SHORTPRESS) {
    new_time = (now - TimeSpan(3600));
    rtc.adjust(new_time);
  }

  sprintf(time_str, "Set hour: %02d", now.hour());
  u8g2.setFont(u8g2_font_ncenB12_tr);  // choose a suitable font
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB08_tr);  // choose a suitable font
    u8g2.drawStr(0, 10, now.toString(buf2));   // write something to the internal memory
    u8g2.setFont(u8g2_font_ncenB12_tr);  // choose a suitable font
    u8g2.drawStr(0, 40, time_str);  // write something to the internal memory
  } while (u8g2.nextPage());
  
  if (button2_event == EV_LONGPRESS_AUTO) 
    state = 15; // set minutes
}


void set_minute(uint8_t button1_event, uint8_t button2_event) {
  DateTime now = rtc.now();
  DateTime new_time;
  char buf2[] = "YYYY-MM-DD hh:mm:ss";
  
  if (button2_event == EV_SHORTPRESS) {
    new_time = (now + TimeSpan(60));
    rtc.adjust(new_time);
  }

  if (button1_event == EV_SHORTPRESS) {
    new_time = (now - TimeSpan(60));
    rtc.adjust(new_time);
  }

  sprintf(time_str, "Set min: %02d", now.minute());
  u8g2.setFont(u8g2_font_ncenB12_tr);  // choose a suitable font
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB08_tr);  // choose a suitable font
    u8g2.drawStr(0, 10, now.toString(buf2));   // write something to the internal memory
    u8g2.setFont(u8g2_font_ncenB12_tr);  // choose a suitable font
    u8g2.drawStr(0, 40, time_str);  // write something to the internal memory
  } while (u8g2.nextPage());
  
  if (button2_event == EV_LONGPRESS_AUTO) 
    state = 16; // set second
}


void set_second(uint8_t button1_event, uint8_t button2_event) {
  DateTime now = rtc.now();
  DateTime new_time;
  char buf2[] = "YYYY-MM-DD hh:mm:ss";
  
  if (button2_event == EV_SHORTPRESS) {
    new_time = (now + TimeSpan(1));
    rtc.adjust(new_time);
  }

  if (button1_event == EV_SHORTPRESS) {
    new_time = (now - TimeSpan(1));
    rtc.adjust(new_time);
  }
  
  sprintf(time_str, "Set sec: %02d", now.second());
  u8g2.setFont(u8g2_font_ncenB12_tr);  // choose a suitable font
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB08_tr);  // choose a suitable font
    u8g2.drawStr(0, 10, now.toString(buf2));   // write something to the internal memory
    u8g2.setFont(u8g2_font_ncenB12_tr);  // choose a suitable font
    u8g2.drawStr(0, 40, time_str);  // write something to the internal memory
  } while (u8g2.nextPage());
  
  if (button2_event == EV_LONGPRESS_AUTO) 
    state = 0; // Show date and time
}


// Show the current date, time and temp1&2 (DS18B20 #1 & #2)
void display_date_time_temp1() {
  DateTime now = rtc.now();
  current_sec = now.second();

  if (old_sec != current_sec) {
    
    tempSensor.requestTemperatures();                             // request temperature sample from sensor on the one wire bus

    sprintf(time_str, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    sprintf(date_str, "%02d/%02d/%04d", now.day(), now.month(), now.year());

    u8g2.setFont(u8g2_font_ncenB12_tr);  // choose a suitable font
    u8g2.firstPage();
    do {
      u8g2.drawStr(0,15,time_str);      // write something to the internal memory
      u8g2.drawStr(0,30,date_str);      // write something to the internal memory
      u8g2.setCursor(0, 45);
      u8g2.print("T1 = ");
      u8g2.print(tempSensor.getTempC(sensor1), 2);
      u8g2.print(" C");
      u8g2.setCursor(0, 60);
      u8g2.print("T2 = ");
      u8g2.print(tempSensor.getTempC(sensor2), 2);
      u8g2.print(" C");
      
    } while (u8g2.nextPage());

    old_sec = current_sec;
  }
}


// Display all connected sensors:
void display_all_sensors() {
  DateTime now = rtc.now();
  current_sec = now.second();

  if (old_sec != current_sec) {
    
    tempSensor.requestTemperatures();                             // request temperature sample from sensor on the one wire bus

    sprintf(time_str, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    //sprintf(date_str, " %02d/%02d/%04d", now.day(), now.month(), now.year());

    u8g2.setFont(u8g2_font_ncenB08_tr);  // choose a suitable font
    u8g2.firstPage();
    do {
      // RTC:
      u8g2.setCursor(0, 10);
      u8g2.print(time_str);
      //u8g2.print(date_str);

      // 2x DS18B20
      u8g2.setCursor(0, 20);
      u8g2.print("T1/T2 = ");
      u8g2.print(tempSensor.getTempC(sensor1), 2);
      u8g2.print("/");
      u8g2.print(tempSensor.getTempC(sensor2), 2);
      u8g2.print(" C");

      // BME280:
      u8g2.setCursor(0, 30);
      u8g2.print("T3 = ");
      u8g2.print(bme.readTemperature(), 2);
      u8g2.print(" C");

      u8g2.setCursor(0, 40);
      u8g2.print("P1 = ");
      u8g2.print(bme.readPressure() / 100.0F, 2);
      u8g2.print(" C");

      u8g2.setCursor(0, 50);
      u8g2.print("Alt. = ");
      u8g2.print(bme.readAltitude(SEALEVELPRESSURE_HPA), 2);
      u8g2.print(" m");

      u8g2.setCursor(0, 60);
      u8g2.print("Hum. = ");
      u8g2.print(bme.readHumidity(), 2);
      u8g2.print(" %");
      
    } while (u8g2.nextPage());

    old_sec = current_sec;
  }
}


// print device address from the address array
void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++)
  {
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}
