#include <SPI.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <I2S.h>

static char buff[100];

#define TFT_VFD_BLUWHT   0xCF7B   // sampled frm a classic vacuum fluorescent display in a VCR
#define TFT_VFD_ORANGE   0xEB43   // sampled from same VCR

TFT_eSPI tft = TFT_eSPI();       // Invoke custom library

volatile uint8_t last_len = 0;

// Create the I2S port using a PIO state machine
I2S i2s(OUTPUT);

// GPIO pin numbers
#define pBCLK 27
#define pWS (pBCLK+1)
#define pDOUT 26


const int frequency = 440; // frequency of square wave in Hz
const int amplitude = 500; // amplitude of square wave
const int sampleRate = 16000; // minimum for UDA1334A
const int halfWavelength = (sampleRate / frequency); // half wavelength of square wave
int16_t sample = amplitude; // current sample value
int count = 0;

void setup()   
{
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(0, 0);

  pinMode(6,INPUT);
  pinMode(7,INPUT);
  Wire1.onReceive(recv);
  Wire1.onRequest(req);
  Wire1.setSDA(6);
  Wire1.setSCL(7);
  Wire1.begin(0x30);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, 1);

  Serial.begin(115200);
  Serial.println("I2S simple tone");

  i2s.setBCLK(pBCLK);
  i2s.setDATA(pDOUT);
  i2s.setBitsPerSample(16);
  // start I2S at the sample rate with 16-bits per sample
  tft.setTextSize(2);
  tft.setCursor(0, 5);
  if (!i2s.begin(sampleRate)) 
  {
    Serial.println("Failed to initialize I2S!");
    tft.setTextColor(TFT_VFD_ORANGE);
    tft.print("I2S INIT FAILED!!!");
    while (1); // do nothing
  }
  tft.print("I2S INIT SUCCEEDED.");
  digitalWrite(LED_BUILTIN, 0);

}

// Called when the I2C slave gets written to
void recv(int len) 
{
    for (int i=0; i<len; i++) buff[i] = Wire1.read();
    last_len = len;
}


// Called when the I2C slave is read from
void req() 
{
    static int ctr = 765;
    char buff[7];
    // Return a simple incrementing hex value
    sprintf(buff, "%06X", (ctr++) % 65535);
    Wire1.write(buff, 6);
}


void switch_test(void)
{
  tft.setTextSize(2);
  char s[4];
  char lineout[40]="";
  char oldlineout[40]="";
  while(1)
  {
    lineout[0]=0;
    for(int i=0;i<15;i++)
    {
      sprintf(s,"%02X",buff[i]);
      strcat(lineout,s);
    }
    if(strcmp(lineout,oldlineout))
    {
      tft.setCursor(0, 25);
      tft.setTextColor(TFT_BLACK);
      tft.print(oldlineout);

      tft.setCursor(0, 25);
      tft.setTextColor(TFT_VFD_BLUWHT);//TFT_VFD_ORANGE);
      tft.print(lineout);
      strcpy(oldlineout,lineout);

    }
  }  
}
void loop() 
{
  switch_test();
}

/*
   The MIT License (MIT)

   Copyright (c) 2023 Craig Iannello,  pugbutt.com

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

