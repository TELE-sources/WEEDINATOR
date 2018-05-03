#define DEBUG_PORT Serial
const uint32_t DEBUG_BAUD = 115200;

#define gpsPort    Serial1

//#define SIMULATE_DEVICES  // comment this out for real system

#if defined( SIMULATE_DEVICES )

  const uint32_t GPS_BAUD   =  9600;

  const uint32_t FONA_BAUD  = DEBUG_BAUD;
  #define fonaPort   Serial

  static const bool useConsole = true;

#else

  const uint32_t GPS_BAUD   =  19200;

  #define fonaPort   Serial2
  const uint32_t FONA_BAUD  =   4800;

  static const bool useConsole = false;

#endif

const int FONA_RST            = 5;
const int SPEAKER             = 10;
const int I2C_REQUEST         = 12;
const int I2C_RECEIVE         = 13;
const int BLUE_LED            = 37;
const int ORANGE_LED          = 39;
const int PIXY_PROCESSING     = 47;
const int USING_GPS_HEADING   = 49;

const uint8_t MEGA_I2C_ADDR   = 26;

const uint32_t HEARTBEAT_PERIOD =  200; // ms
const uint16_t BEEP_FREQUENCY   = 2500; // Hz

////////////////////////////////////////////////
// Changing one of these flags to false will
//   disable the code for that device.  This is
//   handy for my testing.

static const bool usePixy    = not useConsole;
static const bool useCompass = not useConsole;
static const bool useI2C     = not useConsole;
static const bool useFona    = not useConsole;


#include <Wire.h>

#include <Adafruit_FONA.h>
Adafruit_FONA fona( FONA_RST );


// Change these settings! (Network APN, username ,password)
const char networkAPN[] PROGMEM = "pp.vodafone.co.uk";
const char username  [] PROGMEM = "wap";
const char password  [] PROGMEM = "wap";
const char webAddress[] PROGMEM =
  "http://www.goatindustries.co.uk/weedinator/select";
const char dotPhp    [] PROGMEM = ".php";

// Handy macro for passing PROGMEM char arrays to anything 
//   that expects a FLASH string, like DEBUG_PORT.print or 
//   fona.setGPRSNetworkSettings
#define CF(x) ((const __FlashStringHelper *)x)


#include <Pixy.h>
Pixy pixy;
#define X_CENTER        ((PIXY_MAX_X-PIXY_MIN_X)/2)
#define Y_CENTER        ((PIXY_MAX_Y-PIXY_MIN_Y)/2)


#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_LSM303_U.h>
Adafruit_LSM303_Mag_Unified mag( 12345 );

#include <NMEAGPS.h>
NMEAGPS gps;

NeoGPS::Location_t base( 532558000L, -43114000L ); // Llangefni

const float MM_PER_M  = 1000.0;
const float M_PER_KM  = 1000.0;
const float MM_PER_KM = MM_PER_M * M_PER_KM;


#include <NeoPrintToRAM.h>

long zz;
long yy;
float bearing;
int zbearing;
long zdistance;

int sendDataState = LOW;

double compass;

int oldSignature;  //  <-- what is this used for?  Never set, so always 0
long maxSize;
long newSize;
float zheading;
float ubloxBearing;

////////////////////////////////////////////////////////////////////////////

class ServoLoop
{
public:
  ServoLoop(int32_t pgain, int32_t dgain);

  void update(int32_t error);

  int32_t m_pos;
  int32_t m_prevError;
  int32_t m_pgain;
  int32_t m_dgain;
};

ServoLoop panLoop(300, 500);
ServoLoop tiltLoop(500, 700);

ServoLoop::ServoLoop(int32_t pgain, int32_t dgain)
{
  m_pos       = PIXY_RCS_CENTER_POS;
  m_pgain     = pgain;
  m_dgain     = dgain;
  m_prevError = 0x80000000L;
}

void ServoLoop::update(int32_t error)
{
  long int vel;
  char buf[32];
}

////////////////////////////////////////////////////////////////////////////

void setup()
{
  DEBUG_PORT.begin( DEBUG_BAUD );
  DEBUG_PORT.println( F("Mega") );

  gpsPort.begin( GPS_BAUD );

  if (usePixy)
    pixy.init();

  if (useI2C) {
    Wire.begin( MEGA_I2C_ADDR );
    Wire.onReceive(receiveNewWaypoint); // register event
    Wire.onRequest(sendNavData); // register event
  }

  if (useCompass) {
    mag.enableAutoRange(true);

    if(!mag.begin())
    {
      /* There was a problem detecting the LSM303 ... check your connections */
      DEBUG_PORT.println( F("Ooops, no LSM303 detected ... Check your wiring!") );
      while(1);
    }
  }

  pinMode(BLUE_LED,OUTPUT);
  pinMode(ORANGE_LED,OUTPUT);
  pinMode(I2C_REQUEST,OUTPUT);
  pinMode(I2C_RECEIVE,OUTPUT);
  pinMode(USING_GPS_HEADING,OUTPUT);
  pinMode(51,OUTPUT);
  pinMode(53,OUTPUT);

  digitalWrite(USING_GPS_HEADING,HIGH);
  digitalWrite(PIXY_PROCESSING,HIGH);
  digitalWrite(53,LOW);

  beep( 1000 );

  delay(1000);
  digitalWrite(PIXY_PROCESSING,LOW);
  digitalWrite(USING_GPS_HEADING,LOW);

  initFona();

} // setup


void loop()
{
  heartbeat      ();
  checkBeep      ();
  checkGPS       ();
  pixyModule     ();
  checkForFonaMsg();

} // loop

////////////////////////////////////////////////////////////////////////////

void checkGPS()
{
  if (gps.available( gpsPort ))
  {
    gps_fix fix = gps.read(); // save the latest

    if (fix.valid.location)
    {
      beep(100);
      digitalWrite(ORANGE_LED,HIGH);

      float range = fix.location.DistanceKm( base );
      zheading = fix.heading();


      //float heading = prevFix.location.BearingToDegrees ( currentFix.location );
      //float bob = fix.location();
      //zheading = fix.location.BearingToDegrees ( currentFix.location );
      //prevFix = fix;


      DEBUG_PORT.print( F("Zheading:             ") );
      DEBUG_PORT.println(zheading,2);
      
      zdistance = range * MM_PER_KM;
      DEBUG_PORT.print( F("Distance mm:             ") );
      DEBUG_PORT.println(zdistance);

      ubloxBearing = fix.location.BearingToDegrees( base );
      bearing = ubloxBearing;
      zbearing = ubloxBearing * 100; //Float to integer. zbearing is sent to TC275.
      
      DEBUG_PORT.print( F("Bearing:         ") );
      DEBUG_PORT.println(ubloxBearing,5);
      DEBUG_PORT.print( F("Compass Heading: ") );
      DEBUG_PORT.println(compass);
      DEBUG_PORT.println();

      zz = (fix.location.lat()); // zz is a 'long integer'.
      yy = (fix.location.lon());

      //DEBUG_PORT.print( F("Current Ublox latitude:  ") );
      //DEBUG_PORT.println(zz);
      //DEBUG_PORT.print( F("Latitude from Fona:      ") );
      //DEBUG_PORT.println( base.lat() );
      //DEBUG_PORT.print( F("Current Ublox longitude: ") );
      //DEBUG_PORT.println(yy);
      //DEBUG_PORT.print( F("Longitude from Fona:     ") );
      //DEBUG_PORT.println( base.lon() );
      //DEBUG_PORT.println();

      // This switches data set that is transmitted to TC275 via I2C.
      if (sendDataState == LOW)
      {
        characterCompileA(); // Bearing, distance and compass
        sendDataState = HIGH;
      }
      else
      {
        characterCompileB(); // Longitude and latitude
        sendDataState = LOW;
      }

    } else {
      // No valid location, machine will drive in straight 
      //    line until fix received or pixie overrides it.
      digitalWrite( ORANGE_LED, LOW );

      //compass = ubloxBearing;

      // Waiting...
      //DEBUG_PORT.print( '.' );
    }

    compassModule();

  }
} // checkGPS

////////////////////////////////////////////////////////////////////////////

void pixyModule()
{
  if (not usePixy)
    return;

  static       uint8_t frameCount         = 0;
  static const uint8_t PRINT_FRAME_PERIOD = 50;

  static uint16_t blockCount = 0;
         uint16_t blocks     = pixy.getBlocks();

  if (blocks)
  {
    digitalWrite(PIXY_PROCESSING,HIGH);
    int32_t panError  = X_CENTER - pixy.blocks[0].x;
    panLoop.update(panError);

    int32_t tiltError = pixy.blocks[0].y - Y_CENTER;
    tiltLoop.update(tiltError);

    blockCount = blocks;

    // do this (print) every so often frames because printing every
    // frame would bog down the Arduino
    frameCount++;
    if (frameCount >= PRINT_FRAME_PERIOD)
    {
      frameCount = 0;
      //DEBUG_PORT.print( F("Detected ") );
      //DEBUG_PORT.print( blocks );
      //DEBUG_PORT.println( ':' );

      for (uint16_t j=0; j<blocks; j++)
      {
        long size = pixy.blocks[j].height * pixy.blocks[j].width;
        DEBUG_PORT.print( F("No. of blocks: ") );DEBUG_PORT.println(blocks);
        DEBUG_PORT.print( F("Block no.:     ") );DEBUG_PORT.println(j+1);
        DEBUG_PORT.print( F("Size:          ") );DEBUG_PORT.println(size);
        DEBUG_PORT.print( F("Max. size:     ") );DEBUG_PORT.println(maxSize);
        DEBUG_PORT.print( F("PAN POS:       ") );DEBUG_PORT.println(panError);
        DEBUG_PORT.print( F("TILT POS:      ") );DEBUG_PORT.println(tiltError);

        pixy.blocks[j].print();
        DEBUG_PORT.println();
      }
    }

    // Overide compass module with object recognition:
    if (panError > 300)
    {
      DEBUG_PORT.print( F("PAN POS:       ") );DEBUG_PORT.println(panError);
      DEBUG_PORT.print( F("TILT POS:      ") );DEBUG_PORT.println(tiltError);
      //compassModule();
    }

    compass = bearing + panError*0.2;

  } else {
    // No blocks
    digitalWrite(PIXY_PROCESSING,LOW);
  }
  //compassModule();

  int trackedBlock = 0;
  maxSize = 0;
  for (int k = 0; k < blockCount; k++)
  {
    if ((oldSignature == 0) || (pixy.blocks[k].signature == oldSignature))
    {
      newSize = pixy.blocks[k].height * pixy.blocks[k].width;

      if (newSize > maxSize)
      {
        //DEBUG_PORT.print( F("newSize:      ") );DEBUG_PORT.println(newSize);
        trackedBlock = k;
        maxSize = newSize;
        //DEBUG_PORT.print( F("maxSize:      ") );DEBUG_PORT.println(maxSize);
      }
    }
  }
} // pixyModule

////////////////////////////////////////////////////////////////////////////

      uint32_t lastPHP              = 0;
const uint32_t MIN_PHP_CHECK_PERIOD = 1000;

void checkWaypoint()
{
  // Don't try to get waypoints too quickly
  if (millis() - lastPHP >= MIN_PHP_CHECK_PERIOD) {

    // Was a new waypoint requested?
    disableInterrupts();
      int safeID = newWaypointID;
    enableInterrupts();

    if (waypointID != safeID) {

      waypointID = safeID;
      DEBUG_PORT.print( F("New Waypoint ID ") );
      DEBUG_PORT.println( waypointID );

      // Yes, get it now.
      getWaypoint();
      lastPHP = millis();
    }
  }

} // checkWaypoint

///////////////////////////////////////////////////////////////////////

void getWaypoint()
{
  if (not useFona)
    return;

  DEBUG_PORT.print( F("Select php page from TC275:        ") );
  DEBUG_PORT.println( waypointID );

  // Builds the url character array:
  char url[60];
  Neo::PrintToRAM urlChars( url, sizeof(url) );
  urlChars.print( CF(webAddress) );
  urlChars.print( waypointID );
  urlChars.print( CF(dotPhp) );
  urlChars.terminate(); // add NUL terminator to this C string

  //urlChars.print( F("bollox") );
  //urlChars.print( waypointID );
  //urlChars.print( F(".php") );
  //urlChars.terminate();

  DEBUG_PORT.print( F("url for GET request:       ") );
  showData( url, strlen(url) );
  DEBUG_PORT.println();

  digitalWrite(BLUE_LED, HIGH);

  // Issue GET request.  Reply will be a waypoint ID.
  uint16_t statuscode;
  uint16_t length;

  if (fona.HTTP_GET_start( url, &statuscode, &length )) {

    char receive[40];
    Neo::PrintToRAM receiveChars( receive, sizeof(receive) );

    // This is blocking, because the complete reply has not arrived yet.
    DEBUG_PORT.print( F("Raw PHP reply: '") );
    while (length > 0) {
      if (fona.available()) {
        char c = fona.read();
        if (isprint( c ))
          receiveChars.write( c ); // add to array
        showData( &c, 1 );

        length--;
      }
      yield();
    }
    receiveChars.terminate();
    DEBUG_PORT.println('\'');

    DEBUG_PORT.print( F("Lat and Lon from database (receive):     ") );
    showData( receive, receiveChars.numWritten() );
    DEBUG_PORT.println();

    parseWaypoint( receive, receiveChars.numWritten() );

  } else {
     DEBUG_PORT.println( F("HTTP GET Failed!") );
  }
 
  DEBUG_PORT.println( F("\n****") );
  fona.HTTP_GET_end();

  digitalWrite(BLUE_LED, LOW);

  beep(1000);

} // getWaypoint

////////////////////////////////////////////////////////////////////////////

void parseWaypoint( char *ptr, size_t remaining )
{
  static const char     LAT_LABEL[] PROGMEM = "LAT";
  static const size_t   LAT_LABEL_LEN       = sizeof(LAT_LABEL)-1;
  static const char     LON_LABEL[] PROGMEM = "LONG";
  static const size_t   LON_LABEL_LEN       = sizeof(LON_LABEL)-1;
               uint32_t latValue, lonValue;

  if (remaining > 0) {
    // Skip the first character (what is it?)
    ptr++;
    remaining--;

    if (findText( LAT_LABEL, LAT_LABEL_LEN, ptr, remaining )) {

      if (parseValue( ptr, remaining, latValue )) {

        if (findText( LON_LABEL, LON_LABEL_LEN, ptr, remaining )) {

          if (parseValue( ptr, remaining, lonValue )) {

            // Set the new waypoint location
            waypoint.lat( latValue );
            waypoint.lon( lonValue );

            DEBUG_PORT.print  ( F("Location from Fona:  ") );
            DEBUG_PORT.print  ( waypoint.lat() );
            DEBUG_PORT.print  ( ',' );
            DEBUG_PORT.println( waypoint.lon() );

            //  Make sure we used all the characters
            if (remaining > 0) {
              DEBUG_PORT.print( remaining );
              DEBUG_PORT.print( F(" extra characters after lonValue: '") );
              showData( ptr, remaining );
              DEBUG_PORT.println('\'');
            }

          } else {
            DEBUG_PORT.println( F("Invalid longitude") );
          }
        }

      } else {
        DEBUG_PORT.println( F("Invalid latitude") );
      }
    }
  } else {
    DEBUG_PORT.println( F("response too short") );
  }

} // parseWaypoint

///////////////////////////////////////////////////////////////////////

void turnOnGPRS()
{
  while (true) {
    DEBUG_PORT.println( F("Turning off GPRS ...") );
    if (fona.enableGPRS(false)) {
      DEBUG_PORT.println( F("GPRS turned off.") );
    } else {
      DEBUG_PORT.println( F("FAILED: GPRS not turned off") );
    }

    DEBUG_PORT.println( F("Turning on GPRS ...") );
    if (fona.enableGPRS(true)) {
      DEBUG_PORT.println( F("GPRS turned on.") );
      break;
    } else {
      DEBUG_PORT.println( F("FAILED: GPRS not turned on, retrying"));
    }

    delay( 1000 );
  }

} // turnOnGPRS

////////////////////////////////////////////////////////////////////////////

void initFona()
{
  if (not useFona)
    return;

  fonaPort.begin( FONA_BAUD );
  if (! fona.begin( fonaPort )) {
    DEBUG_PORT.println( F("Couldn't find FONA") );
    //while (1);
  }
  DEBUG_PORT.println( F("FONA is OK") );

  uint8_t type = fona.type();
  const __FlashStringHelper *typeString;
  switch (type) {
    case FONA800L:
      typeString = F("FONA 800L");          break;
    case FONA800H:
      typeString = F("FONA 800H");          break;
    case FONA808_V1:
      typeString = F("FONA 808 (v1)");      break;
    case FONA808_V2:
      typeString = F("FONA 808 (v2)");      break;
    case FONA3G_A:
      typeString = F("FONA 3G (American)"); break;
    case FONA3G_E:
      typeString = F("FONA 3G (European)"); break;
    default: 
      typeString = F("???");                break;
  }
  DEBUG_PORT.print  ( F("Found ") );
  DEBUG_PORT.println( typeString );

  //networkStatus();   // Check the network is available. Home is good.
  DEBUG_PORT.println();

  DEBUG_PORT.println( F("Checking that GPRS is turned off to start with .........") );

  fona.setGPRSNetworkSettings( CF(networkAPN), CF(username), CF(password) );
  //delay (1000);

  //delay (1000);
  //networkStatus();   // Check the network is available. Home is good.

  turnOnGPRS();

} // initFona

////////////////////////////////////////////////////////////////////////////

void showData( char *data, size_t n )
{
  for (size_t i=0; i < n; i++) {

    if (isprint(data[i]))
      DEBUG_PORT.print( data[i] );
    else if (data[i] == 0x00)
      DEBUG_PORT.print( F("<NUL>") );
    else if (data[i] == 0x0A)
      DEBUG_PORT.print( F("<LF>") );
    else if (data[i] == 0x0D)
      DEBUG_PORT.print( F("<CR>") );
    else {
      // Some other value?  Just show its HEX value.
      DEBUG_PORT.print( F("<0x") );
      if (data[i] < 0x10)
        DEBUG_PORT.print( '0' );
      DEBUG_PORT.print( data[i], HEX );
      DEBUG_PORT.print( '>' );
    }
  }

} // showData


bool findText
  ( const char *   text_P, const size_t   len,
          char * & ptr   ,       size_t & remaining )
{
  bool found = false;
  
  if (remaining >= len) {

    static const int MATCHED = 0;
  
    if (memcmp_P( ptr, text_P, len ) == MATCHED) {
      found = true;
      
      ptr       += len; // Advance pointer past the label
      remaining -= len;

    } else {
      DEBUG_PORT.print  ( (const __FlashStringHelper *) text_P );
      DEBUG_PORT.println( F(" label not found") );
    }

  } else {
    DEBUG_PORT.print  ( F("Length ") );
    DEBUG_PORT.print  ( remaining );
    DEBUG_PORT.print  ( F(" too short for label ") );
    DEBUG_PORT.println( (const __FlashStringHelper *) text_P );
  }
  
  return found;

} // findText


bool parseValue( char * & ptr, size_t & remaining, uint32_t & value )
{
  bool ok = false;

  if (remaining > 0) {

    bool negative = (*ptr == '-');
    if (negative) {
      ptr++; // advance past minus sign
      remaining--;
    }

    // Interpret digit characters
    value = 0;
    while (remaining and isDigit(*ptr)) {
      value = value * 10 + (*ptr++ - '0'); // advances pointer, too
      remaining--;
      ok = true; // received at least one digit
    }

    if (negative)
      value = -value;
  }

  return ok;

} // parseValue

////////////////////////////////////////////////////////////////////////////

uint32_t lastHeartbeat;

void heartbeat()
{
  uint32_t currentMillis = millis();

  if ((currentMillis - lastHeartbeat) >= HEARTBEAT_PERIOD)
  {
    lastHeartbeat = currentMillis;
    digitalWrite( BLUE_LED, !digitalRead(BLUE_LED) ); // toggle
  }
} // heartbeat

////////////////////////////////////////////////////////////////////////////

void receiveNewWaypoint(int howMany)
{
  while (Wire.available()) {
    char c = Wire.read();
    if (isdigit(c)) {
      newWaypointID = c - '0'; // E.g., char '5' to integer value 5
    }
  }
} // receiveNewWaypoint

////////////////////////////////////////////////////////////////////////////

         char navData[120];
volatile bool navDataSent = false;

void sendNavData()
{
  digitalWrite(I2C_REQUEST,HIGH);
  //DEBUG_PORT.println( F("Request event start  ") );
  Wire.write(url);     // as expected by master
  //delay(100);
  //DEBUG_PORT.println( F("Request event end  ") );
  digitalWrite(I2C_REQUEST,LOW);

} // sendNavData

////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////

// For sending Ublox bearing and distance data to TC275

void characterCompileA()
{
  Neo::PrintToRAM msg( url, sizeof(url) );

  // Prevent requestEvent from getting the first half of the old
  //   response and the second half of the new response.
  noInterrupts();
    msg.print( F("BEAR") );
    msg.print( zbearing );
    msg.print( F("DIST") );
    msg.print( zdistance );
    msg.print( F("COMP") );
    msg.print( compass );
    msg.terminate();
  interrupts();

  //DEBUG_PORT.print( F("msg A to send:  ") );
  //DEBUG_PORT.println( url );

} // characterCompileA

////////////////////////////////////////////////////////////////////////////

// For sending Ublox lat and long to TC275

void characterCompileB()
{
  Neo::PrintToRAM msg( url, sizeof(url) );

  // Prevent requestEvent from getting the first half of the old
  //   response and the second half of the new response.
  noInterrupts();
    msg.print( F("LOON") );
    msg.print( yy );
    msg.print( F("LAAT") );
    msg.print( zz );
    msg.terminate();
  interrupts();

  //DEBUG_PORT.print( F("msg B to send:  ") );
  //DEBUG_PORT.println( url );

} // characterCompileB

////////////////////////////////////////////////////////////////////////////

float autoXMax = -1000.0;
float autoXMin =  1000.0;
float autoYMax = -1000.0;
float autoYMin =  1000.0;
//unsigned long compassCount = 0;

void compassModule()
{
  if (not useCompass)
    return;

  //compassCount++;

  //DEBUG_PORT.println( F("####################### 1") );
  sensors_event_t magEvent;
  //DEBUG_PORT.println( F("####################### 1.5") );
  delay(100);
  mag.getEvent(&magEvent);  // This is where the problem is!!!!!!!!!!!!!!!!
  //DEBUG_PORT.println( F("####################### 2") );

  // Observed readings (integers)
  int xMin =  -19.45;
  int xMax =  23.27;
  int yMin = -49.82;
  int yMax =  -9.82;

  //DEBUG_PORT.println( F("####################### 3") );
  float xxBlob = magEvent.magnetic.x;
  float yyBlob = magEvent.magnetic.y;
  //DEBUG_PORT.println( F("####################### 4") );
  if((xxBlob!=0)&&(yyBlob!=0))
    {
    if(xxBlob>autoXMax)
      {
        autoXMax = xxBlob;
      }
    if(xxBlob<autoXMin)
      {
        autoXMin = xxBlob;
      }
    if(yyBlob>autoYMax)
      {
        autoYMax = yyBlob;
      }
    if(yyBlob<autoYMin)
      {
        autoYMin = yyBlob;
      }
    }

// Now normalise to min -50 and max 50:
  //DEBUG_PORT.println( F("####################### 5") );
  float xxx = ((magEvent.magnetic.x - xMin)*100/(xMax-xMin))-50;
  float yyy = ((magEvent.magnetic.y - yMin)*100/(yMax-yMin))-50;
  //DEBUG_PORT.println( F("####################### 6") );
  compass = atan2( yyy, xxx ) * RAD_TO_DEG;
  compass = compass + 270 +15; //Lower this value to make clockwise turn.

  if (zheading!=0)
  {
    compass = zheading; // Overide compass with GPS derived heading
    digitalWrite(USING_GPS_HEADING,HIGH);
  }
  else                  // Makes the machine drive straight ahead.
  {
    compass = zbearing/100;
    digitalWrite(USING_GPS_HEADING,LOW);
  }

  DEBUG_PORT.print( F("autoXMin:  ") ); DEBUG_PORT.println(autoXMin);
  DEBUG_PORT.print( F("autoXMax:  ") ); DEBUG_PORT.println(autoXMax);
  DEBUG_PORT.print( F("autoYMin:  ") ); DEBUG_PORT.println(autoYMin);
  DEBUG_PORT.print( F("autoYMax:  ") ); DEBUG_PORT.println(autoYMax);
  //DEBUG_PORT.print( F("Compass Heading: ") );DEBUG_PORT.println(compass);
  //DEBUG_PORT.println();

} // compassModule

////////////////////////////////////////////////////////////////////////////

uint32_t beepDuration;
uint32_t beepStart;
bool     beeping;

void beep( uint32_t duration )
{
  beeping      = true;
  beepStart    = millis();
  beepDuration = duration;
  tone( SPEAKER, BEEP_FREQUENCY, 0 );   // pin,pitch,duration (forever)
}

void checkBeep()
{
  if (beeping and ((millis() - beepStart) >= beepDuration)) {
    noTone( SPEAKER );
    beeping = false;
  }
}
