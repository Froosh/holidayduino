#include <Wire.h>
#include <FastSPI_LED.h>

#define STRT 2  // INT 0
#define DBGI 3  // INT 1
#define DOUT 4
#define BUT1 5
#define BUT2 6
#define BUT3 7
#define ACKB 8

// First LED is used for own status until Linux apps take control
#define NUM_LEDS 50

// WS2811 is GGRRBB
struct CRGB { unsigned char g; unsigned char r; unsigned char b; };
struct CRGB *leds;
byte *ledsraw;

boolean active = false;
int i = 1, j = 0;
long next = 0;
long lastDBGI = -1000;
volatile byte pos;
volatile boolean got_frame;

#define SERMAX 8
char serbuf[SERMAX];
char serlen = 0;

void setup()
{
  Serial.begin (57600);

  // set up buttons with pullups
  pinMode(BUT1, INPUT_PULLUP);
  pinMode(BUT2, INPUT_PULLUP);
  pinMode(BUT3, INPUT_PULLUP);

  // set up acknowledge/busy pin
  pinMode(ACKB, OUTPUT);
  digitalWrite(ACKB, 1);

  // set up SPI slave
  pinMode(MISO, OUTPUT);
  SPCR |= _BV(SPE);

  // set up FastSPI output
  FastSPI_LED.setLeds(NUM_LEDS);
  FastSPI_LED.setChipset(CFastSPI_LED::SPI_WS2811);
  FastSPI_LED.setPin(DOUT);
  FastSPI_LED.init();
  FastSPI_LED.start();

  leds = (struct CRGB*)FastSPI_LED.getRGBData(); 
  ledsraw = (byte *)leds;
  pos = 0;  // skip status LED
  got_frame = false;

  // detect falling STRT to indicate start/end of frame
  attachInterrupt(0, spiStartISR, CHANGE);

  // detect falling DBGI to indicate iMX boot messages
  attachInterrupt(1, dbgISR, FALLING);
  
  // enable SPI slave interrupts
  SPCR |= _BV(SPIE);
  
  Wire.begin(0x08);  // set up I2C as slave (can still act as a master)
  Serial.write('~');
}

void spiStartISR() {
  if (digitalRead(STRT)==0)  // start of frame
  {
    if (!got_frame)  // ignore if still processing last frame?
    {
      pos = 0;       // reset frame position to start
      digitalWrite(ACKB, 0);  // indicate ack
    }
    // else // ???
  }
  else
  {
    if (pos==NUM_LEDS*3)
    {
      active = true;
      next = millis() + 60000;
      got_frame = true;
    }
    else // incomplete frame
    {
      pos = 0;       // reset frame position to start
      digitalWrite(ACKB, 1);    // indicate error with immediate ack/busy change
    }
  }
}

void dbgISR() {
  lastDBGI = millis();
}

ISR (SPI_STC_vect)
{
  byte c = SPDR;
  if (pos < NUM_LEDS*3)
  {
    ledsraw[pos] = c;
    pos++;
  }
}

int ConvNib(char a)
{
  // returns -1 for bad input
  a -= 48;
  if (a<0) return -1;
  if (a>9 && a<17) return -1;
  if (a>9) a -= 7;
  if (a>15 && a<40) return -1;
  if (a>15) a -= 32;
  if (a>15) return -1;
  return a;
}

int ConvHex8(char *t)
{
  // returns a negative value for bad input (may not be -1 due to left shift)
  return ConvNib(t[0])<<4 | ConvNib(t[1]);
}

int ConvHex16(char *t)
{
  // returns a negative value for bad input (may not be -1 due to left shifts)
  // note max 16 bit value we can read is 0x7fff - 32kB which is plenty of EEPROM
  return ConvNib(t[0])<<12 | ConvNib(t[1])<<8 | ConvNib(t[2])<<4 | ConvNib(t[3]);
}

void PrintHex8(byte data)
{
  char tmp;
  tmp = (data >> 4) | 48;
  if (tmp > 57) tmp += 39;
  Serial.write(tmp);
  tmp = (data & 0x0F) | 48;
  if (tmp > 57) tmp += 39;
  Serial.write(tmp);
}

void loop() {
  if (Serial.available())
  {
    active = true;
    next = millis() + 60000;

    char c = Serial.read();
    if (c=='?')
    {
      serlen = 0;
      Serial.print(F("HolidayDuino01\r\n"));
    }
//    else if (c=='[' || c==']')
//    {
//      serlen = 0;
//      if (c=='[')
//        Wire.begin(0x08);  // make I2C slave
//      else
//        Wire.begin();      // back to I2C master
//      Serial.print(c);
//      Serial.print(F("\r\n"));
//    }
    else if (c=='\r' || c=='\n')
    {
      int a = -1;
      byte b;
      if (serbuf[0]=='R' || serbuf[0]=='r')
      {
        if (serlen==1) a = 0;
        else if (serlen==3) a = ConvHex8(serbuf+1);
        else if (serlen==5) a = ConvHex16(serbuf+1);
      }
      else if (serbuf[0]=='W' || serbuf[0]=='w')
      {
        if (serlen==3)       { a = ConvHex8(serbuf+1); if (a>=0) { b = a; a = 0; } }
        else if (serlen==5)  { a = ConvHex8(serbuf+3); if (a>=0) { b = a; a = ConvHex8(serbuf+1); } }
        else if (serlen==7)  { a = ConvHex8(serbuf+5); if (a>=0) { b = a; a = ConvHex16(serbuf+1); } }
        if (a>=0)
        {
          Wire.beginTransmission(0x50);
          Wire.write(a>>8);
          Wire.write(a&0xff);
          Wire.write(b);
          Wire.endTransmission();
          delay(5);  // wait for write before read back
        }
      }
      if (a<0)
        Serial.write('?');
      else {
          Wire.beginTransmission(0x50);
          Wire.write(a>>8);
          Wire.write(a&0xff);
          Wire.endTransmission();
          Wire.requestFrom(0x50,1);
          if (Wire.available()) b = Wire.read();
          PrintHex8(b);
      }
      Serial.print(F("\r\n"));
      serlen = 0;
    }
    else
    {
      if (serlen<SERMAX)
        serbuf[serlen++] = c;
      else
      {
        Serial.write("?\r\n");
        serlen = 0;
      }
    }
  }

  if (!active && next <= millis())
  {
    memset(leds, 0, NUM_LEDS * 3);
    if (lastDBGI + 1000 < millis())
      leds[0].r = 63;
    else
      leds[0].b = 63;
     
    if (digitalRead(BUT1)==0) leds[0].b += 63;
    if (digitalRead(BUT2)==0) leds[0].b += 63;
    if (digitalRead(BUT3)==0) leds[0].b += 63;

    switch(j&3) { 
      case 0: leds[i].r = 255; break;
      case 1: leds[i].g = 255; break;
      case 2: leds[i].b = 255; break;
      default:
              leds[i].r = 255;
              leds[i].g = 255;
              leds[i].b = 255;
              break;
    }
    FastSPI_LED.show();

    i++;
    if (i>=NUM_LEDS) { i = 1; j++; }

    next = millis() + 50;
  }
  
  if (got_frame)
  {
    //Serial.print(F("got frame!\r\n"));
    FastSPI_LED.show();
    got_frame = false;
    digitalWrite(ACKB, 1);  // indicate complete with (delayed) ack/busy change
  }

  if (active && next <= millis())
    active = false;
}
