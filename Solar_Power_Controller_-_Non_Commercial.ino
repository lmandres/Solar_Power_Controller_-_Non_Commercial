
/*
This program calculates solar positions as a function of location, date, and time.
The equations are from Jean Meeus, Astronomical Algorithms, Willmann-Bell, Inc., Richmond, VA
(C) 2015, David Brooks, Institute for Earth Science Research and Education.

Source: http://www.instesre.org/ArduinoUnoSolarCalculations.pdf
*/

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Source: https://github.com/PaulStoffregen/Time.git
#include <TimeLib.h>

// Source: https://github.com/njh/EtherCard.git
#include <EtherCard.h>

// Source: https://www.airspayce.com/mikem/arduino/AccelStepper/index.html
#include <AccelStepper.h>

// Source: https://github.com/adafruit/Adafruit-Motor-Shield-library.git
// Note: This version of the Adafruit Motor Shield has been discontinued.
#include <AFMotor.h>

#include <LiquidCrystal.h>

#define DEG_TO_RAD 0.01745329
#define PI 3.141592654
#define TWOPI 6.28318531

#define ENC28J60_CS_PIN 3

// ethernet interface mac address, must be unique on the LAN
static byte mymac[] = { 0xDE,0xAD,0xBE,0xEF,0xFE,0xED };
static byte myip[] = { 192, 168, 0, 11 };

byte Ethernet::buffer[600];
BufferFiller bfill;

// two stepper motors one on each port
AF_Stepper elevMotor(200, 1);
AF_Stepper azimuthMotor(200, 2);

void elevForward() {
  elevMotor.onestep(FORWARD, SINGLE);
}

void elevBackward() {
  elevMotor.onestep(BACKWARD, SINGLE);
}

void azimuthForward() {
  azimuthMotor.onestep(FORWARD, SINGLE);
}

void azimuthBackward() {
  azimuthMotor.onestep(BACKWARD, SINGLE);
}

// Define some steppers and the pins the will use
AccelStepper elevStepper(elevForward, elevBackward);
AccelStepper azimuthStepper(azimuthForward, azimuthBackward);

float elevAdj = 2000.0;
float azimuthAdj = 2000.0;

float elev, azimuth;
float lon=-94.68896*DEG_TO_RAD, lat=29.746973*DEG_TO_RAD;
//float lon=0, lat=0;
int elevSteps, azimuthSteps;
byte manualExit = 0;

LiquidCrystal lcd(8,9,4,5,6,7);

const char homePage[] PROGMEM =
  "HTTP/1.0 200 OK\r\n"
  "Content-Type: text/html\r\n"
  "Pragma: no-cache\r\n"
  "\r\n"
  "<h1>Date: $D-$D$D-$D$D</h1>"
  "<h1>Time: $D$D:$D$D:$D$D</h1>"
  "<h1>Latitude: $S</h1>"
  "<h1>Longitude: $S</h1>"
  "<h1>Elevation: $S<h1>"
  "<h1>Azimuth: $S</h1>"
  "<h1>Elevation Adjustment: $S</h1>"
  "<h1>Azimuth Adjustment: $S</h1>"
  "<h1>Elevation Steps: $S</h1>"
  "<h1>Azimuth Steps: $S</h1>"
  "<h1>Elev Distance to Go: $S</h1>"
  "<h1>Azimuth Distance to Go: $S</h1>"
  "<h1>Elevation Current Position: $S</h1>"
  "<h1>Azimuth Current Position: $S</h1>"
  "<h1>Status: $S</h1>"
"";

const char busyPage[] PROGMEM = "HTTP/1.0 500 Internal Server Error\r\n";

void processNetRequest(int pos, void (*callbackFunc)(const char*)) {

    char* start_line;
    char* end_line;
    char* query_string;
    
    char* data = (char *) Ethernet::buffer + pos;
    
    end_line = strchr(data, '\n');
    query_string = (char*)malloc(end_line-data+1);

    strncpy(query_string, data, end_line-data);
    query_string[end_line-data] = '\0';
    start_line = strchr(query_string, '?');
    start_line++;
    
    end_line = strchr(start_line, ' ');
    start_line[end_line-start_line] = '\0';
    callbackFunc(start_line);
    
    free(query_string);
}

void manualMoveAndAdjust(void (*callbackFunc)(const char*)) {

  char* lineString = "    ";
  byte prevSecond;
  word len;
  word pos;

  int azimuthIn;
  int elevIn;

  elev=(elevAdj*elev-elevStepper.distanceToGo())/elevAdj;
  azimuth=(azimuthAdj*azimuth-azimuthStepper.distanceToGo())/azimuthAdj;

  while (true) {

    sendHomePage(3); // send web page data

    prevSecond = second();
    while (true) {
      
      len = ether.packetReceive();
      pos = ether.packetLoop(len);
      
      if (pos) { // check if valid tcp data is received
        break;
      }
    }
    
    processNetRequest(pos, callbackFunc);
    
    elevStepper.move((long)elevSteps);
    azimuthStepper.move((long)azimuthSteps);
  
    lcd.home();
    if (-999 <= elevSteps && elevSteps <= 9999) {
      sprintf(lineString, "%4d", elevSteps);
    } else {
      lineString = "????";
    }
    lcd.print(F("Elev Steps: "));
    lcd.print(lineString);
    if (-999 <= azimuthSteps && azimuthSteps <= 9999) {
      sprintf(lineString, "%4d", azimuthSteps);
    } else {
      lineString = "????";
    }
    lcd.setCursor(0, 1);
    lcd.print(F("Azim Steps: "));
    lcd.print(lineString);
    
    while (elevStepper.distanceToGo() || azimuthStepper.distanceToGo()) {
      len = ether.packetReceive();
      pos = ether.packetLoop(len);
      if (pos) {
        sendHomePage(2); // send web page data
      }
      elevStepper.run();
      azimuthStepper.run();
    }

    elev=(elevAdj*elev+elevSteps)/elevAdj;
    azimuth=(azimuthAdj*azimuth+azimuthSteps)/azimuthAdj;

    if (manualExit) {
      break;
    }
  }
}

void resetEtherConnection() {

  bool networkSuccess = 0;
  
  if (!ether.begin(sizeof(Ethernet::buffer), mymac, ENC28J60_CS_PIN)) {
    lcd.home();
    lcd.print(F("Ethernet        "));
    lcd.setCursor(0, 1);
    lcd.print(F("failed . . .    "));
  }
  
  lcd.home();

  if (sizeof(myip) == sizeof(byte)*4) {
    networkSuccess = ether.staticSetup(myip);
  } else {
    networkSuccess = ether.dhcpSetup();
  }
  
  if (!networkSuccess) {
    lcd.print(F("IP network setup"));
    lcd.setCursor(0, 1);
    lcd.print(F("failed . . .    "));
  }
}

void setup() {
  
  lcd.begin(16, 2);
  delay(1000);
  
  lcd.home();
  lcd.print(F("Starting        "));
  lcd.setCursor(0, 1);
  lcd.print(F("tracker . . .   "));
  
  elevStepper.setMaxSpeed(1000.0);
  elevStepper.setAcceleration(1000.0);
  
  azimuthStepper.setMaxSpeed(1000.0);
  azimuthStepper.setAcceleration(1000.0);

  lcd.home();
  lcd.print(F("Starting        "));
  lcd.setCursor(0, 1);
  lcd.print(F("network . . .   "));
  
  resetEtherConnection();
}

void loop() {

  word len;
  word pos;

  time_t timenow = now();
  
  getElevAzimuth(lon, lat, year(timenow), month(timenow), day(timenow), hour(timenow), minute(timenow), second(timenow), &elev, &azimuth);
  printDisplay();

  if (elevAdj*elev > 0) {
    elevStepper.moveTo((long)elevAdj*elev);
    azimuthStepper.moveTo((long)azimuthAdj*azimuth);
  }
  
  while (elevStepper.distanceToGo() || azimuthStepper.distanceToGo()) {
    len = ether.packetReceive();
    pos = ether.packetLoop(len);
    if (pos) {
      sendHomePage(2); // send web page data
    }
    elevStepper.run();
    azimuthStepper.run();
  }
  
  len = ether.packetReceive();
  pos = ether.packetLoop(len);
  if (pos) { // check if valid tcp data is received
    processNetRequest(pos, setControl);
    getElevAzimuth(lon, lat, year(timenow), month(timenow), day(timenow), hour(timenow), minute(timenow), second(timenow), &elev, &azimuth);
    sendHomePage(0); // send web page data
  }
}

void printDisplay() {
  
  char lineVar[17];
  time_t timenow = now();

  lcd.home();

  switch ((second(timenow) / 5) % 3) {

    case 0:
    
      dtostrf(elev/DEG_TO_RAD, 4, 8, lineVar);
      lineVar[10] = '\0';
      lcd.print(F("Elev: "));
      lcd.print(lineVar);
      lcd.setCursor(0, 1);
      
      dtostrf(azimuth/DEG_TO_RAD, 4, 8, lineVar);
      lineVar[10] = '\0';
      lcd.print(F("Azim: "));
      lcd.print(lineVar);
  
      break;
  
    case 1:
  
      sprintf(lineVar, "%d:%02d:%02d", hour(timenow), minute(timenow), second(timenow));
      lcd.print(F("Time: "));
      lcd.print(lineVar);
      for (int i = 0; i < (10 - strlen(lineVar)); i++) {
        lcd.write(' ');
      }
      
      lcd.setCursor(0, 1);
  
      sprintf(lineVar, "%d-%02d-%02d", year(timenow), month(timenow), day(timenow));
      lcd.print(F("Date: "));
      lcd.print(lineVar);
      for (int i = 0; i < (10 - strlen(lineVar)); i++) {
        lcd.write(' ');
      }
  
      break;
  
    case 2:
    
      ether.makeNetStr(lineVar, ether.myip, 4, '.', 10);
      lcd.print(F("IP Address:     "));
      lcd.setCursor(0, 1);
      lcd.print(lineVar);
      for (int i = 0; i < (16 - strlen(lineVar)); i++) {
        lcd.write(' ');
      }
  
      break;
  }
}

void getElevAzimuth(float lonIn, float latIn, int yearIn, int monthIn, int dayIn, int hourIn, int minuteIn, int secondIn, float* elevIn, float* azimuthIn) {
  
  float T,JD_frac,L0,M,e,C,L_true,f,R,GrHrAngle,Obl,RA,Decl,HrAngle;
  long JD_whole,JDx;
  
  JD_whole=JulianDate(yearIn,monthIn,dayIn);
  JD_frac=(hourIn+minuteIn/60.+secondIn/3600.)/24.-.5;
  T=JD_whole-2451545;
  T=(T+JD_frac)/36525.;
  L0=DEG_TO_RAD*fmod(280.46645+36000.76983*T,360);
  M=DEG_TO_RAD*fmod(357.5291+35999.0503*T,360);
  e=0.016708617-0.000042037*T;
  C=DEG_TO_RAD*((1.9146-0.004847*T)*sin(M)+(0.019993-0.000101*T)*sin(2*M)+0.00029*sin(3*M));
  f=M+C;
  Obl=DEG_TO_RAD*(23+26/60.+21.448/3600.-46.815/3600*T);     
  JDx=JD_whole-2451545;  
  GrHrAngle=280.46061837+(360*JDx)%360+.98564736629*JDx+360.98564736629*JD_frac;
  GrHrAngle=fmod(GrHrAngle,360.);    
  L_true=fmod(C+L0,TWOPI);
  R=1.000001018*(1-e*e)/(1+e*cos(f));
  RA=atan2(sin(L_true)*cos(Obl),cos(L_true));
  Decl=asin(sin(Obl)*sin(L_true));
  HrAngle=DEG_TO_RAD*GrHrAngle+lonIn-RA;
  
  *elevIn=asin(sin(latIn)*sin(Decl)+cos(latIn)*(cos(Decl)*cos(HrAngle)));
  // Azimuth measured eastward from north.
  *azimuthIn=PI+atan2(sin(HrAngle),cos(HrAngle)*sin(latIn)-tan(Decl)*cos(latIn));
}

void setControl(const char* query_string) {

  int controlOp;
  
  int julianDateIn;
  int elevSteps, azimuthSteps;

  controlOp = getIntFromQueryString(query_string, "controlOp", 0);
  
  switch (controlOp) {
    case 1: // Set Time (Date)
      setDeviceTime(query_string);
      break;
    case 2: // Set Time (Julian Date)
      setDeviceJDTime(query_string);
      break;
    case 3: // Set Longitude/Latitude
      setLatLon(query_string);
      break;
    case 4: // Adjust Azimuth/Elevation Position
      adjustElevAzimuthPos();
      break;
    case 5: // Adjust Azimuth/Elevation Values
      setElevAzimuthVals(query_string);
      break;
    case 6: // Adjust Elevation Stepper
      setElevAzimuthAdjVals(query_string);
      break;
    case 7: // Manual Move
      manualMove();
      break;
    case 8: // Manual Adjust
      manualAdjust();
      break;
    default:
      break;  
  }
}

long JulianDate(int yearIn, int monthIn, int dayIn) {
  long JD_whole;
  int A,B;
  if (monthIn<=2) { 
    yearIn--;
    monthIn+=12;
  }  
  A=yearIn/100; 
  B=2-A+A/4;
  JD_whole=(long)(365.25*(yearIn+4716))+(int)(30.6001*(monthIn+1))+dayIn+B-1524;
  return JD_whole;
}

char* getStringFromQueryString(const char* query_string, const char* key) {
  
  char queryValue[50];
  
  memset(queryValue, '\0', sizeof(queryValue));
  if (ether.findKeyVal(query_string, queryValue, sizeof(queryValue), key)) {
    return queryValue;
  }

  return NULL;
}

int getIntFromQueryString(const char* query_string, const char* key, int errorReturn) {

  int valueValid = 1;
  char* value = getStringFromQueryString(query_string, key);
  
  if (value) {
    for (int i = 0; i < strlen(value); i++) {
      if (!isdigit(value[i])) {
        valueValid = 0;
      }
    }
    if (valueValid) {
      return (int)atoi(value);
    }
  }

  return errorReturn;
}

void setLatLon(const char* query_string) {
  if (getStringFromQueryString(query_string, "lon")) {
    lon = strtod(getStringFromQueryString(query_string, "lon"), NULL)*DEG_TO_RAD;
  }
  if (getStringFromQueryString(query_string, "lat")) {
    lat = strtod(getStringFromQueryString(query_string, "lat"), NULL)*DEG_TO_RAD;
  }
}

void setDeviceTime(const char* query_string) {
  
  int yearIn, monthIn, dayIn, hourIn, minuteIn, secondIn;

  yearIn = getIntFromQueryString(query_string, "year", 0);
  monthIn = getIntFromQueryString(query_string, "month", 0);
  dayIn = getIntFromQueryString(query_string, "day", 0);
  hourIn = getIntFromQueryString(query_string, "hour", 0);
  minuteIn = getIntFromQueryString(query_string, "minute", 0);
  secondIn = getIntFromQueryString(query_string, "second", 0);

  setTime(hourIn, minuteIn, secondIn, dayIn, monthIn, yearIn);
}

void setDeviceJDTime(const char* query_string) {
  int jdtime = getIntFromQueryString(query_string, "jdtime", 0);
  setTime(jdtime);
}

void setElevAzimuthAdjVals(const char* query_string) {
  azimuthAdj = (float)getIntFromQueryString(query_string, "azimuthAdj", azimuthAdj);
  elevAdj = (float)getIntFromQueryString(query_string, "elevAdj", elevAdj);
}

void setElevAzimuthVals(const char* query_string) {
  if (getStringFromQueryString(query_string, "azimuth")) {
    azimuth = strtod(getStringFromQueryString(query_string, "azimuth"), NULL)*DEG_TO_RAD;
  }
  if (getStringFromQueryString(query_string, "elev")) {
    elev = strtod(getStringFromQueryString(query_string, "elev"), NULL)*DEG_TO_RAD;
  }
}

void adjustElevAzimuthPos() {
  
  byte prevSecond;
  
  word len;
  word pos;
  
  lcd.home();
  lcd.print(F("Align w/ horizon"));
  lcd.setCursor(0, 1);
  lcd.print(F("due North.      "));

  elevStepper.moveTo(0);
  azimuthStepper.moveTo(0);
  while (elevStepper.distanceToGo() || azimuthStepper.distanceToGo()) {
    len = ether.packetReceive();
    pos = ether.packetLoop(len);
    if (pos) {
      sendHomePage(2); // send web page data
    }
    elevStepper.run();
    azimuthStepper.run();
  }

  manualMove();
  elevStepper.setCurrentPosition(0);
  azimuthStepper.setCurrentPosition(0);
}

void setManualSteps(const char* query_string) {
    azimuthSteps = getIntFromQueryString(query_string, "azimuthSteps", 0);
    elevSteps = getIntFromQueryString(query_string, "elevSteps", 0);
    manualExit = getIntFromQueryString(query_string, "manualExit", 0);
}

void setManualStepsAndAdjust(const char* query_string) {
    setElevAzimuthAdjVals(query_string);
    setElevAzimuthVals(query_string);
    elevStepper.setCurrentPosition(getIntFromQueryString(query_string, "elevPos", elevStepper.currentPosition()));
    azimuthStepper.setCurrentPosition(getIntFromQueryString(query_string, "azimuthPos", azimuthStepper.currentPosition()));
    azimuthSteps = getIntFromQueryString(query_string, "azimuthSteps", 0);
    elevSteps = getIntFromQueryString(query_string, "elevSteps", 0);
    manualExit = getIntFromQueryString(query_string, "manualExit", 0);
}

void manualMove() {
  manualMoveAndAdjust(setManualSteps);
}

void manualAdjust() {
  manualMoveAndAdjust(setManualStepsAndAdjust);
}

void sendHomePage(int trackerStatusIn) {

  time_t timenow = now();

  byte encchunk[17];

  char latEmit[13];
  char lonEmit[13];
  char elevEmit[13];
  char azimuthEmit[13];
  char elevAdjEmit[13];
  char azimuthAdjEmit[13];
  char elevStepsEmit[13];
  char azimuthStepsEmit[13];
  char elevDTGEmit[13];
  char azimuthDTGEmit[13];
  char elevCPEmit[13];
  char azimuthCPEmit[13];
  char trackerStatusEmit[3];
  
  int trackerStatus = 0;
  
  if (trackerStatusIn != 0) {
    trackerStatus = trackerStatusIn;
  } else {
    trackerStatus = 1;
  }

  dtostrf(lat/DEG_TO_RAD, 3, 7, latEmit);
  dtostrf(lon/DEG_TO_RAD, 3, 7, lonEmit);
  dtostrf((elevAdj*elev-elevStepper.distanceToGo())/elevAdj/DEG_TO_RAD, 3, 7, elevEmit);
  dtostrf((azimuthAdj*azimuth-azimuthStepper.distanceToGo())/azimuthAdj/DEG_TO_RAD, 3, 7, azimuthEmit);
  dtostrf(elevAdj, 10, 0, elevAdjEmit);
  dtostrf(azimuthAdj, 10, 0, azimuthAdjEmit);
  dtostrf(elevSteps, 10, 0, elevStepsEmit);
  dtostrf(azimuthSteps, 10, 0, azimuthStepsEmit);
  dtostrf(elevStepper.distanceToGo(), 10, 0, elevDTGEmit);
  dtostrf(azimuthStepper.distanceToGo(), 10, 0, azimuthDTGEmit);
  dtostrf(elevStepper.currentPosition(), 10, 0, elevCPEmit);
  dtostrf(azimuthStepper.currentPosition(), 10, 0, azimuthCPEmit);
  dtostrf(trackerStatus, 2, 0, trackerStatusEmit);

  bfill = ether.tcpOffset();
  bfill.emit_p(
    homePage,
    year(timenow),
    month(timenow)/10, month(timenow)%10,
    day(timenow)/10, day(timenow)%10,
    hour(timenow)/10, hour(timenow)%10,
    minute(timenow)/10, minute(timenow)%10,
    second(timenow)/10, second(timenow)%10,
    latEmit,
    lonEmit,
    elevEmit,
    azimuthEmit,
    elevAdjEmit,
    azimuthAdjEmit,
    elevStepsEmit,
    azimuthStepsEmit,
    elevDTGEmit,
    azimuthDTGEmit,
    elevCPEmit,
    azimuthCPEmit,
    trackerStatusEmit
  );
  ether.httpServerReply(bfill.position());
}

void sendBusyPage() {
  ether.httpServerReplyAck(); // send ack to the request
  memcpy_P(ether.tcpOffset(), busyPage, sizeof(busyPage));//only the first part will sended
  ether.httpServerReply(sizeof(busyPage) - 1);
}

