/*
 * 3D printed smart shelving with a giant hidden digital clock in the front edges of the shelves - DIY Machines

==========

More info and build instructions: https://www.youtube.com/watch?v=8E0SeycTzHw

3D printed parts can be downloaded from here: https://www.thingiverse.com/thing:4207524

You will need to install the Adafruit Neopixel library which can be found in the library manager.

This project also uses the handy DS3231 Simple library:- https://github.com/sleemanj/DS3231_Simple   Please follow the instruction on installing this provided on the libraries page

Before you install this code you need to set the time on your DS3231. Once you have connected it as shown in this project and have installed the DS3231_Simple library (see above) you
 to go to  'File' >> 'Examples' >> 'DS3231_Simple' >> 'Z1_TimeAndDate' >> 'SetDateTime' and follow the instructions in the example to set the date and time on your RTC

==========

 * MOD: Quarter-hour "round trip" chime animation - VARIANT 2 (multi-comet hour chime)
 *  - at hh:15 -> 1 sweep through all clock LEDs (minute colour)
 *  - at hh:30 -> 2 sweeps (minute colour)
 *  - at hh:45 -> 3 sweeps (minute colour)
 *  - at hh:00 -> ONE revolution with as many evenly spaced comets as the current hour
 *                (12h format, so 0:00/12:00 = 12 comets). Constant duration (~1.5 s)
 *                no matter the hour - count the comets to read the hour.
 *
 * MOD: Night mode (optional, currently DISABLED)
 *  - if nightModeEnabled is true: all LEDs are off from 23:00 to 06:00
 *  - if false: the moonlight colour scheme takes over at night instead
 *
 * MOD: Time-of-day colour schemes (4 pairs)
 *  - 00:00-05:59 moon:    very dim deep blue / faint ember - readable glow, does not light the room
 *  - 06:00-11:59 morning: sunrise (golden hours, soft sky blue minutes)
 *  - 12:00-17:59 day:     vivid high-contrast colours for bright rooms (orange / azure)
 *  - 18:00-23:59 evening: cozy sunset (ember hours, warm white minutes)
 *  - the colours switch automatically; chime animations use the active scheme
 *
 * MOD: Brightness fix
 *  - constrain() stops map() from extrapolating (negative values wrapped to full
 *    brightness in dark rooms -> random flash every few seconds)
 *  - deadband of 4 stops the visible twitching on small sensor fluctuations

*/

#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#endif

#include <DS3231_Simple.h>
DS3231_Simple Clock;

// Create a variable to hold the time data
DateTime MyDateAndTime;

// Which pin on the Arduino is connected to the NeoPixels?
#define LEDCLOCK_PIN    6
#define LEDDOWNLIGHT_PIN    5

// How many NeoPixels are attached to the Arduino?
#define LEDCLOCK_COUNT 216
#define LEDDOWNLIGHT_COUNT 12

//(red * 65536) + (green * 256) + blue ->for 32-bit merged colour value so 16777215 equals white
// or 3 hex byte 00 -> ff for RGB eg 0x123456 for red=12(hex) green=34(hex), and green=56(hex)
// this hex method is the same as html colour codes just with "0x" instead of "#" in front

// ----- time-of-day colour schemes -----
// MOON 00:00-05:59 - "moonlight": deliberately very dim, deep colours.
// Together with the auto-dimming this is just a faint glow - readable up close,
// but it will not light up the room at night.
const uint32_t moonHourColour   = 0x200800; // faint ember
const uint32_t moonMinuteColour = 0x001030; // deep midnight blue

// MORNING 06:00-11:59 - "sunrise": low warm sun + pale morning sky
const uint32_t morningHourColour   = 0xA05000; // golden
const uint32_t morningMinuteColour = 0x3070A0; // soft sky blue

// DAY 12:00-17:59 - "full daylight": vivid complementary pair, brightest scheme
// so the clock stays readable when the room is flooded with daylight
const uint32_t dayHourColour   = 0xFF5000; // vivid orange
const uint32_t dayMinuteColour = 0x0080FF; // vivid azure

// EVENING 18:00-23:59 - "sunset/fireplace": warm, calm, low-key
const uint32_t eveningHourColour   = 0x902000; // ember / copper
const uint32_t eveningMinuteColour = 0xA06428; // warm white
// --------------------------------------

// ----- colour scheme TEST flags -----
// set exactly ONE of these to true to preview that scheme right now,
// regardless of the actual time (first true flag wins, top to bottom).
// Set all four back to false for normal automatic time-of-day switching.
const bool forceMoonColours    = false;
const bool forceMorningColours = false;
const bool forceDayColours     = false;
const bool forceEveningColours = false;
// ------------------------------------

// active colours - set automatically every loop by updateColoursForTimeOfDay()
uint32_t clockMinuteColour = dayMinuteColour;
uint32_t clockHourColour   = dayHourColour;

int clockFaceBrightness = 0;

// ----- chime animation settings -----
int lastChimeMinute = -1;          // remembers the last minute a chime ran so it only fires once
const int chimeTailLength = 15;    // length of the fading comet tail (in LEDs)
const int chimeStepSize = 2;       // LEDs the comet head advances per frame (higher = faster sweep)
const int chimeFrameDelay = 2;     // extra delay per frame in ms (higher = slower sweep)
const int chimePauseBetween = 150; // pause between quarter sweeps in ms so they are countable
// ------------------------------------

// ----- night mode settings -----
// If enabled, the LEDs go completely dark from nightStartHour to nightEndHour
// and override the moonlight colour scheme. Currently disabled: at night the
// clock shows the dim moonlight colours instead of switching off.
const bool nightModeEnabled = false;
const int nightStartHour = 23;       // LEDs off from this hour (24h format)...
const int nightEndHour   = 6;        // ...until this hour
// -------------------------------

// Declare our NeoPixel objects:
Adafruit_NeoPixel stripClock(LEDCLOCK_COUNT, LEDCLOCK_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripDownlighter(LEDDOWNLIGHT_COUNT, LEDDOWNLIGHT_PIN, NEO_GRB + NEO_KHZ800);
// Argument 1 = Number of pixels in NeoPixel strip
// Argument 2 = Arduino pin number (most are valid)
// Argument 3 = Pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)


//Smoothing of the readings from the light sensor so it is not too twitchy
const int numReadings = 12;

int readings[numReadings];      // the readings from the analog input
int readIndex = 0;              // the index of the current reading
long total = 0;                  // the running total
long average = 0;                // the average



void setup() {

  Serial.begin(9600);
  Clock.begin();

  stripClock.begin();           // INITIALIZE NeoPixel stripClock object (REQUIRED)
  stripClock.show();            // Turn OFF all pixels ASAP
  stripClock.setBrightness(100); // Set inital BRIGHTNESS (max = 255)


  stripDownlighter.begin();           // INITIALIZE NeoPixel stripClock object (REQUIRED)
  stripDownlighter.show();            // Turn OFF all pixels ASAP
  stripDownlighter.setBrightness(50); // Set BRIGHTNESS (max = 255)

  //smoothing
    // initialize all the readings to 0:
  for (int thisReading = 0; thisReading < numReadings; thisReading++) {
    readings[thisReading] = 0;
  }

}

void loop() {

  //read the time
  readTheTime();

  //pick the colour scheme matching the time of day (moon/morning/day/evening)
  updateColoursForTimeOfDay();

  //night mode (optional): keep all LEDs dark between nightStartHour and nightEndHour
  if (isNightTime()){
    stripClock.clear();
    stripClock.show();
    stripDownlighter.clear();
    stripDownlighter.show();
    lastChimeMinute = 0; // suppresses the hour chime at the wake-up minute (06:00)
    delay(5000);
    return;              // skip chime, time display, sensor and downlighter while sleeping
  }

  //run the chime animation on quarter hours (15/30/45/00)
  checkForChime();

  //display the time on the LEDs
  displayTheTime();




  //Record a reading from the light sensor and add it to the array
  readings[readIndex] = analogRead(A0); //get an average light level from previouse set of samples
  Serial.print("Light sensor value added to array = ");
  Serial.println(readings[readIndex]);
  readIndex = readIndex + 1; // advance to the next position in the array:

  // if we're at the end of the array move the index back around...
  if (readIndex >= numReadings) {
    // ...wrap around to the beginning:
    readIndex = 0;
  }

  //now work out the sum of all the values in the array
  int sumBrightness = 0;
  for (int i=0; i < numReadings; i++)
    {
        sumBrightness += readings[i];
    }
  Serial.print("Sum of the brightness array = ");
  Serial.println(sumBrightness);

  // and calculate the average:
  int lightSensorValue = sumBrightness / numReadings;
  Serial.print("Average light sensor value = ");
  Serial.println(lightSensorValue);


  //set the brightness based on ambiant light levels
  int targetBrightness = constrain(map(lightSensorValue, 50, 1000, 200, 1), 1, 200);

  // deadband: only follow changes of 4 or more, so the clock
  // doesn't visibly twitch on tiny sensor fluctuations
  static int appliedBrightness = 100;
  if (abs(targetBrightness - appliedBrightness) >= 4){
    appliedBrightness = targetBrightness;
  }
  clockFaceBrightness = appliedBrightness;
  stripClock.setBrightness(clockFaceBrightness);
  Serial.print("Mapped brightness value = ");
  Serial.println(clockFaceBrightness);

  stripClock.show();

   //(red * 65536) + (green * 256) + blue ->for 32-bit merged colour value so 16777215 equals white
  stripDownlighter.fill(16777215, 0, LEDDOWNLIGHT_COUNT);
  stripDownlighter.show();

  delay(5000);   //this 5 second delay to slow things down during testing

}


void readTheTime(){
  // Ask the clock for the data.
  MyDateAndTime = Clock.read();

  // And use it
  Serial.println("");
  Serial.print("Time is: ");   Serial.print(MyDateAndTime.Hour);
  Serial.print(":"); Serial.print(MyDateAndTime.Minute);
  Serial.print(":"); Serial.println(MyDateAndTime.Second);
  Serial.print("Date is: 20");   Serial.print(MyDateAndTime.Year);
  Serial.print(":");  Serial.print(MyDateAndTime.Month);
  Serial.print(":");    Serial.println(MyDateAndTime.Day);
}


// sets clockHourColour / clockMinuteColour based on the time of day
// (or based on a test flag, if one is set)
void updateColoursForTimeOfDay(){

  // test overrides - first flag that is true wins
  if (forceMoonColours){
    clockHourColour   = moonHourColour;
    clockMinuteColour = moonMinuteColour;
    return;
  }
  if (forceMorningColours){
    clockHourColour   = morningHourColour;
    clockMinuteColour = morningMinuteColour;
    return;
  }
  if (forceDayColours){
    clockHourColour   = dayHourColour;
    clockMinuteColour = dayMinuteColour;
    return;
  }
  if (forceEveningColours){
    clockHourColour   = eveningHourColour;
    clockMinuteColour = eveningMinuteColour;
    return;
  }

  int h = MyDateAndTime.Hour;

  if (h < 6){
    // moon (00:00-05:59)
    clockHourColour   = moonHourColour;
    clockMinuteColour = moonMinuteColour;
  } else if (h < 12){
    // morning (06:00-11:59)
    clockHourColour   = morningHourColour;
    clockMinuteColour = morningMinuteColour;
  } else if (h < 18){
    // day (12:00-17:59)
    clockHourColour   = dayHourColour;
    clockMinuteColour = dayMinuteColour;
  } else {
    // evening (18:00-23:59)
    clockHourColour   = eveningHourColour;
    clockMinuteColour = eveningMinuteColour;
  }
}


// true while the clock should stay dark (handles the range crossing midnight)
bool isNightTime(){
  if (!nightModeEnabled){
    return false; // night mode switched off -> moonlight colours are shown at night
  }
  int h = MyDateAndTime.Hour;
  return (h >= nightStartHour || h < nightEndHour);
}


void checkForChime(){
  int currentMinute = MyDateAndTime.Minute;
  bool isChimeMinute = (currentMinute == 0 || currentMinute == 15 || currentMinute == 30 || currentMinute == 45);

  if (isChimeMinute && currentMinute != lastChimeMinute){

    if (currentMinute == 15){
      Serial.println("Chime! 1 sweep");
      roundTripAnimation(1, clockMinuteColour);
    } else if (currentMinute == 30){
      Serial.println("Chime! 2 sweeps");
      roundTripAnimation(2, clockMinuteColour);
    } else if (currentMinute == 45){
      Serial.println("Chime! 3 sweeps");
      roundTripAnimation(3, clockMinuteColour);
    } else {
      // full hour: one revolution with as many comets as the hour (12h format)
      int comets = MyDateAndTime.Hour % 12;
      if (comets == 0){
        comets = 12;
      }
      Serial.print("Hour chime! Multi-comet revolution, comets = ");
      Serial.println(comets);
      multiCometRevolution(comets, clockHourColour);
    }
  }

  lastChimeMinute = currentMinute; // remember so the chime only fires once per quarter
}


// quarter chime: a single comet sweeps the whole strip, repeated 'sweeps' times
void roundTripAnimation(int sweeps, uint32_t colour){

  // unpack the 32-bit colour into its channels so we can fade the tail
  uint8_t r = (colour >> 16) & 0xFF;
  uint8_t g = (colour >> 8) & 0xFF;
  uint8_t b = colour & 0xFF;

  for (int s = 0; s < sweeps; s++){

    // the comet head travels the whole strip (plus tail length so the tail fully exits)
    for (int head = 0; head < LEDCLOCK_COUNT + chimeTailLength; head += chimeStepSize){

      stripClock.clear();

      for (int i = 0; i < chimeTailLength; i++){
        int pos = head - i;
        if (pos >= 0 && pos < LEDCLOCK_COUNT){
          uint8_t fade = 255 - ((255 / chimeTailLength) * i); // brightest at the head, fading to the tail
          stripClock.setPixelColor(pos, stripClock.Color(
            ((uint16_t)r * fade) / 255,
            ((uint16_t)g * fade) / 255,
            ((uint16_t)b * fade) / 255));
        }
      }

      stripClock.show();
      delay(chimeFrameDelay);
    }

    delay(chimePauseBetween); // short dark pause so the individual sweeps are countable
  }

  // clear the strip; the normal loop redraws the time right afterwards
  stripClock.clear();
  stripClock.show();
}


// hour chime: 'comets' evenly spaced comets travel ONE full revolution around the strip.
// Duration is constant (~1.5 s) regardless of the hour - count the comets to read the hour.
void multiCometRevolution(int comets, uint32_t colour){

  uint8_t r = (colour >> 16) & 0xFF;
  uint8_t g = (colour >> 8) & 0xFF;
  uint8_t b = colour & 0xFF;

  int spacing = LEDCLOCK_COUNT / comets; // distance between comet heads

  // shorten the tail if the comets sit close together (e.g. 12 comets -> spacing 18)
  // so each comet stays clearly separated from the next one
  int tail = chimeTailLength;
  if (tail > spacing - 4){
    tail = spacing - 4;
  }

  // one full revolution: every point on the strip sees every comet exactly once
  for (int revStep = 0; revStep < LEDCLOCK_COUNT; revStep += chimeStepSize){

    stripClock.clear();

    for (int c = 0; c < comets; c++){

      int head = (revStep + (c * spacing)) % LEDCLOCK_COUNT;

      for (int i = 0; i < tail; i++){
        int pos = head - i;
        if (pos < 0){
          pos += LEDCLOCK_COUNT; // wrap around the strip
        }
        uint8_t fade = 255 - ((255 / tail) * i); // brightest at the head, fading to the tail
        stripClock.setPixelColor(pos, stripClock.Color(
          ((uint16_t)r * fade) / 255,
          ((uint16_t)g * fade) / 255,
          ((uint16_t)b * fade) / 255));
      }
    }

    stripClock.show();
    delay(chimeFrameDelay);
  }

  // clear the strip; the normal loop redraws the time right afterwards
  stripClock.clear();
  stripClock.show();
}


void displayTheTime(){

  stripClock.clear(); //clear the clock face

  int firstMinuteDigit = MyDateAndTime.Minute % 10; //work out the value of the first digit and then display it
  displayNumber(firstMinuteDigit, 0, clockMinuteColour);

  int secondMinuteDigit = floor(MyDateAndTime.Minute / 10); //work out the value for the second digit and then display it
  displayNumber(secondMinuteDigit, 63, clockMinuteColour);

  int firstHourDigit = MyDateAndTime.Hour; //work out the value for the third digit and then display it
  if (firstHourDigit > 12){
    firstHourDigit = firstHourDigit - 12;
  }

 // Comment out the following three lines if you want midnight to be shown as 12:00 instead of 0:00
//  if (firstHourDigit == 0){
//    firstHourDigit = 12;
//  }

  firstHourDigit = firstHourDigit % 10;
  displayNumber(firstHourDigit, 126, clockHourColour);


  int secondHourDigit = MyDateAndTime.Hour; //work out the value for the fourth digit and then display it

// Comment out the following three lines if you want midnight to be shwon as 12:00 instead of 0:00
//  if (secondHourDigit == 0){
//    secondHourDigit = 12;
//  }

 if (secondHourDigit > 12){
    secondHourDigit = secondHourDigit - 12;
  }
    if (secondHourDigit > 9){
      stripClock.fill(clockHourColour,189, 18);
    }

  }


void displayNumber(int digitToDisplay, int offsetBy, uint32_t colourToUse){
    switch (digitToDisplay){
    case 0:
    digitZero(offsetBy,colourToUse);
      break;
    case 1:
      digitOne(offsetBy,colourToUse);
      break;
    case 2:
    digitTwo(offsetBy,colourToUse);
      break;
    case 3:
    digitThree(offsetBy,colourToUse);
      break;
    case 4:
    digitFour(offsetBy,colourToUse);
      break;
    case 5:
    digitFive(offsetBy,colourToUse);
      break;
    case 6:
    digitSix(offsetBy,colourToUse);
      break;
    case 7:
    digitSeven(offsetBy,colourToUse);
      break;
    case 8:
    digitEight(offsetBy,colourToUse);
      break;
    case 9:
    digitNine(offsetBy,colourToUse);
      break;
    default:
     break;
  }
}
