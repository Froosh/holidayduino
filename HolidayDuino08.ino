// HolidayDuino firmware
// compiled with Arduino 1.5.4 r2
// using FastSPI_LED2 RC5 with fix for 20MHz timing

// v07 fixes timing to match latest WorldSemi recommendationsS
// v08 fixes issues handling LED string lengths > 50 pixels
//  always handles 50 pixel SPI packets for backwards compatibilty
//  now reinits SPI slave on each packet (in case of lost clock pulses)
//  also avoid test pattern firing during SPI packet reception

// at 20Mhz we can get 50ns precision
// recommended timing is 400ns/850ns for 0, 850ns/400ns for 1
// at 16MHz we only get 62.5ns precision
// so at 16MHz the timing will be closer to 437ns/812ns

// version for 16MHz or 20MHz using FastSPI_LED2
// also now 115200 baud (was 57600 in versfions 01 to 03)
// and when 20MHz uses OptiBoot to get more usable flash

// when compiling, use board "Holiday Atmega328 20MHz" for 20MHz
// or "Holiday Atmega328 16MHz" for 16MHz (and larger bootloader)

// version number includes MHz so we use correct update
// ** can't easily compute MHZ from F_CPU and get it in string form :-(
#if F_CPU == 16000000L
#define CPUMHZ "16"
#elif F_CPU == 20000000L
#define CPUMHZ "20"
#else
#error "Unexpected F_CPU"
#endif
#define VERSION "HolidayDuino08-" CPUMHZ

// FastSPI_LED2 is seriously amazing!
// It handles 20MHz CPU and byte colour reordering
#include <FastSPI_LED2.h>

#include <Wire.h>

#define STRT 2  // INT 0
#define DBGI 3  // INT 1
#define DOUT 4
#define BUT1 5
#define BUT2 6
#define BUT3 7
#define ACKB 8

#define MAX_LEDS 250

CRGB leds[MAX_LEDS];
byte *ledsraw;

boolean echo = false;
boolean testmode = false;
boolean eeok = false;
byte startbut = 0;
unsigned int num_leds = 50;  // using byte type causes cast issues when multipled
byte cur_prog = 0;
byte num_progs = 0;
byte eepagesize = 32;
byte reg[16];
unsigned int wdog = 0;
byte argc = 0;
unsigned int argv[2] = {0, 0};
byte led = 1, col = 0;
volatile long next = 0;
volatile long lastDBGI = -1000;
volatile unsigned int pos;
volatile boolean active = false;
volatile boolean got_frame;

void setup()
{
  Serial.begin (115200);

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

  // capture button state at boot
  if (digitalRead(BUT1)==0) startbut += 1;
  if (digitalRead(BUT2)==0) startbut += 2;
  if (digitalRead(BUT3)==0) startbut += 4;

  Wire.begin();  // set up I2C as master

  // check eeprom & overwrite defaults if valid
  if (eeread(0)=='h' && eeread(1)==0x01)
  {
    eeok = true;
    eepagesize = eeread(2) * 4;
    num_leds = eeread(3);
    num_progs = eeread(4);
    cur_prog = eeread(5);
  }

  // set up FastSPI output - WS2812 is GGRRBB
  if (num_leds>0) FastLED.addLeds<WS2812, DOUT, GRB>(leds, num_leds);

  ledsraw = (byte *)leds;
  pos = 0;  // skip status LED
  got_frame = false;

  // detect falling STRT to indicate start/end of frame
  attachInterrupt(0, spiStartISR, CHANGE);

  // detect falling DBGI to indicate iMX boot messages
  attachInterrupt(1, dbgISR, FALLING);

  // enable SPI slave interrupts
  SPCR |= _BV(SPIE);

  Serial.println(F(VERSION));
}

void spiStartISR() {
  if (num_leds==0) return;
  if (digitalRead(STRT)==0)  // start of frame
  {
    if (!got_frame)  // ignore if still processing last frame?
    {
      pos = 0;       // reset frame position to start

      // reset SPI (because SS is held low, but bit clock could get out of sync)
      SPCR &= ~(_BV(SPE));
      SPCR |= _BV(SPE);

      next = millis() + (wdog>0?wdog:200);
      digitalWrite(ACKB, 0);  // indicate ack
    }
  }
  else
  {
    // always handle 50 pixel packets for backwards compatibility
    if (pos>=num_leds*3 || pos >=50*3 || testmode)
    {
      // "extend" colour data if more pixels installed
      while (pos<num_leds*3)
      {
        ledsraw[pos] = ledsraw[pos - 3];
        pos++;
      }
      
      active = true;
      next = millis() + (wdog>0?wdog:200);
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
  if (pos < num_leds*3)
  {
    ledsraw[pos] = c;
    pos++;
  }
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

void eewrite(int addr, byte b)
{
  Wire.beginTransmission(0x50);
  Wire.write(addr>>8);
  Wire.write(addr&0xff);
  Wire.write(b);
  Wire.endTransmission();
  delay(5);  // wait for write before read back
}

byte eeread(int addr)
{
  Wire.beginTransmission(0x50);
  Wire.write(addr>>8);
  Wire.write(addr&0xff);
  Wire.endTransmission();
  Wire.requestFrom(0x50,1);
  if (Wire.available())
    return Wire.read();
  else return 0;
}

//http://code.google.com/p/tinkerit/wiki/SecretVoltmeter
//only accurate to about 10% (bandgap can be 1.0V to 1.2V)
long readVcc()
{
  long result;
  // Read 1.1V reference against AVcc
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;
  result = (1100 * 1024L) / result; // Back-calculate AVcc in mV
  return result;
}

void command(char c)
{
  if (c=='?')
  {
    Serial.println(F(VERSION));
  }
  else if (c=='[' || c==']')
  {
    if (c=='[')
      Wire.begin(0x08);  // make I2C slave (can still act like a master)
    else
      Wire.begin();      // back to I2C master
    Serial.println(c);
  }
  else if (c=='A')
  {
    pinMode(14 + (argv[0]&3), INPUT);
    analogRead(argv[0]&3);  // throw away
    Serial.println(analogRead(argv[0]&3));
  }
  else if (c=='B')
  {
    Serial.println((digitalRead(BUT1)==0?1:0)|(digitalRead(BUT2)==0?2:0)|
      (digitalRead(BUT3)==0?4:0)|(startbut<<4));
  }
  else if (c=='C')
  {
    if (argc==2) eewrite(argv[0]%eepagesize, argv[1]);
    Serial.println(eeread(argv[0]%eepagesize));
  }
  else if (c=='D')
  {
    if (argc & eeok) eewrite(5, argv[0]>=num_progs?0:argv[0]);
    Serial.println(eeread(5));
  }
  else if (c=='E')
  {
    if (argc) echo = argv[0]?true:false;
    Serial.println(echo?1:0);
  }
  else if (c=='G')
  {
    pinMode(14 + (argv[0]&3), OUTPUT);
    digitalWrite(14 + (argv[0]&3), LOW);
    Serial.println(digitalRead(14 + (argv[0]&3)));
  }
  else if (c=='H')
  {
    pinMode(14 + (argv[0]&3), OUTPUT);
    digitalWrite(14 + (argv[0]&3), HIGH);
    Serial.println(digitalRead(14 + (argv[0]&3)));
  }
  else if (c=='I')
  {
    pinMode(14 + (argv[0]&3), INPUT);
    Serial.println(digitalRead(14 + (argv[0]&3)));
  }
  else if (c=='L')
  {
    if (argc && eeok)
    {
      num_leds = (argv[0]>MAX_LEDS?MAX_LEDS:argv[0]);
      eewrite(3, num_leds);
    }
    Serial.println(num_leds);
  }
  else if (c=='P')
  {
    if (argc) cur_prog = argv[0];
    Serial.println(cur_prog);
  }
  else if (c=='R')
  {
    if (argc==2) reg[argv[0]&15] = argv[1];
    Serial.println(reg[argv[0]&15]);
  }
  else if (c=='T')
  {
    if (argc) testmode = argv[0]?true:false;
    Serial.println(testmode?1:0);
  }
  else if (c=='V')
  {
    Serial.println(readVcc());
  }
  else if (c=='W')
  {
    if (argc) wdog = argv[0];
    Serial.println(wdog);
  }
  else if (c>=' ')
  {
    Serial.println('?');
  }
}

void loop() {
  if (Serial.available())
  {
    active = true;
    next = millis() + wdog;

    char c = Serial.read();
    if (echo) Serial.print(c);
    if (c>='0' && c<='9') {
      if (argc==0) argc++;
      argv[argc-1] = argv[argc-1] * 10 + c - '0';
    }
    else if (c==',' && argc<2)
    {
      if (argc==0) argc++;
      argc++;
      argv[argc-1] = 0;
    }
    else
    {
      command(c);
      argc = 0;
      argv[0] = 0;
    }
  }

  if (!active && next <= millis())
  {
    memset(leds, 0, num_leds * 3);
    if (lastDBGI + 1000 < millis())
      leds[0].r = 63;
    else
      leds[0].b = 63;

    if (digitalRead(BUT1)==0) leds[0].b += 63;
    if (digitalRead(BUT2)==0) leds[0].b += 63;
    if (digitalRead(BUT3)==0) leds[0].b += 63;

    switch(col&3) { 
    case 0: 
      leds[led].r = 255; 
      break;
    case 1: 
      leds[led].g = 255; 
      break;
    case 2: 
      leds[led].b = 255; 
      break;
    default:
      leds[led].r = 255;
      leds[led].g = 255;
      leds[led].b = 255;
      break;
    }
    if (num_leds>0) FastSPI_LED.show();

    led++;
    if (led>=num_leds) { 
      led = 1; 
      col++; 
    }

    next = millis() + 50;
  }

  if (got_frame)
  {
    if (testmode)
    {
      // send data back via serial
      for (unsigned int i=0; i<pos; i++) {
        if (i>0) Serial.print(',');
        PrintHex8(ledsraw[i]);
      }
      Serial.print("\r\n");
      // and clear rest of buffer before display
      for (; pos<num_leds*3; pos++)
        ledsraw[pos] = 0;
    }

    //Serial.print(F("got frame!\r\n"));
    if (num_leds>0) FastSPI_LED.show();

    got_frame = false;
    digitalWrite(ACKB, 1);  // indicate complete with (delayed) ack/busy change
  }

  /* watchdog */
  if (wdog>0 && active && next <= millis())
    active = false;
}


