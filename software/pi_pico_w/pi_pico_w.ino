#include <SPI.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <I2S.h>
#include "hardware/pwm.h"

static char buff[100];
static char codec_i2c_buff[100];


#define TFT_VFD_BLUWHT   0xCF7B   // sampled frm a classic vacuum fluorescent display in a VCR
#define TFT_VFD_ORANGE   0xEB43   // sampled from same VCR

TFT_eSPI tft = TFT_eSPI();       // Invoke custom library

volatile uint8_t last_len = 0;
volatile uint8_t codec_i2c_last_len = 0;

// Create the I2S port using a PIO state machine
I2S i2s(OUTPUT);

// GPIO pin numbers audio codec I2S
#define pBCLK 27
#define pWS (pBCLK+1)
#define pDOUT 26
#define pMCLK 16
// audio codec I2C
#define pSCL  15
#define pSDA  14

#define NAU8822_I2C_ADDRESS 0x1A

const int frequency = 440; // frequency of square wave in Hz
const int amplitude = 500; // amplitude of square wave
const int sampleRate = 16000; // minimum for UDA1334A
const int halfWavelength = (sampleRate / frequency); // half wavelength of square wave
int16_t sample = amplitude; // current sample value
int count = 0;

void I2S_EnableMCLK(unsigned long rate_hz)
{
  // output 12MHz on pMCLK
 //  vreg_set_voltage(VREG_VOLTAGE_1_30);

	gpio_set_function(pMCLK, GPIO_FUNC_PWM);	
	uint osc_slice = pwm_gpio_to_slice_num(pMCLK);
	pwm_set_clkdiv_int_frac(osc_slice, 1, 0);
	pwm_set_wrap(osc_slice, 2);
	pwm_set_chan_level(osc_slice, pwm_gpio_to_channel(pMCLK), 1);
	pwm_set_enabled(osc_slice, true);  
}

void I2C_WriteWAU8822(uint8_t u8addr, uint16_t u16data) 
{
  Wire.beginTransmission(NAU8822_I2C_ADDRESS);
  Wire.write(u8addr<<1);
  Wire.write((uint8_t)((u8addr << 1) | (u16data >> 8)));
  Wire.write((uint8_t)(u16data & 0x00FF));
  Wire.endTransmission();
}

void WAU8822_ConfigSampleRate(uint32_t u32SampleRate)
{
    if((u32SampleRate % 8) == 0)
    {
        I2C_WriteWAU8822(36, 0x008);    //12.288Mhz
        I2C_WriteWAU8822(37, 0x00C);
        I2C_WriteWAU8822(38, 0x093);
        I2C_WriteWAU8822(39, 0x0E9);
    }
    else
    {
        I2C_WriteWAU8822(36, 0x007);    //11.2896Mhz
        I2C_WriteWAU8822(37, 0x021);
        I2C_WriteWAU8822(38, 0x131);
        I2C_WriteWAU8822(39, 0x026);
    }

    switch (u32SampleRate)
    {
    case 16000:
        I2C_WriteWAU8822(6, 0x1AD);   /* Divide by 6, 16K */
        I2C_WriteWAU8822(7, 0x006);   /* 16K for internal filter coefficients */
        break;

    case 32000:
        I2C_WriteWAU8822(6, 0x16D);    /* Divide by 3, 32K */
        I2C_WriteWAU8822(7, 0x002);    /* 32K for internal filter coefficients */
        break;

    case 44100:
    case 48000:
        I2C_WriteWAU8822(6, 0x14D);    /* Divide by 1, 48K */
        I2C_WriteWAU8822(7, 0x000);    /* 48K for internal filter coefficients */
        break;
    }
}

void Delay(int count)
{
    volatile uint32_t i;
    for (i = 0; i < count ; i++);
}

void setup()   
{
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(0, 0);

  // kb controller i2c

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


  // audio codec i2c
/* 
  pinMode(pSCL,INPUT);
  pinMode(pSDA,INPUT);
  Wire.setSDA(pSDA);
  Wire.setSCL(pSCL);
  Wire.setClock(100000);
  Wire.onReceive(codec_i2c_recv);
  Wire.onRequest(codec_i2c_req);
  Wire.begin();

  I2C_WriteWAU8822(0,  0x000);   // Reset all registers 
  Delay(0x200);
  I2C_WriteWAU8822(1,  0x02F);
  I2C_WriteWAU8822(2,  0x1B3);   // Enable L/R Headphone, ADC Mix/Boost, ADC 
  I2C_WriteWAU8822(3,  0x07F);   // Enable L/R main mixer, DAC 
  // offset: 0x4 => default, 24bit, I2S format, Stereo
  I2C_WriteWAU8822(4,  0x010);   // 16-bit word length, I2S format, Stereo 

  I2C_WriteWAU8822(5,  0x000);   // Companding control and loop back mode (all disable) 
  I2C_WriteWAU8822(6,  0x1AD);   // Divide by 6, 16K 
  I2C_WriteWAU8822(7,  0x006);   // 16K for internal filter coefficients 
  I2C_WriteWAU8822(10, 0x008);   // DAC soft mute is disabled, DAC oversampling rate is 128x 
  I2C_WriteWAU8822(14, 0x108);   // ADC HP filter is disabled, ADC oversampling rate is 128x 
  I2C_WriteWAU8822(15, 0x1EF);   // ADC left digital volume control 
  I2C_WriteWAU8822(16, 0x1EF);   // ADC right digital volume control 

  I2C_WriteWAU8822(44, 0x000);   // LLIN/RLIN is not connected to PGA 
  I2C_WriteWAU8822(47, 0x050);   // LLIN connected, and its Gain value 
  I2C_WriteWAU8822(48, 0x050);   // RLIN connected, and its Gain value 
  I2C_WriteWAU8822(50, 0x001);   // Left DAC connected to LMIX 
  I2C_WriteWAU8822(51, 0x001);   // Right DAC connected to RMIX 
  
  // Set MCLK and enable MCLK 
  I2S_EnableMCLK(12000000);
  WAU8822_ConfigSampleRate(sampleRate);
  */
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

// Called when the I2C slave gets written to
void codec_i2c_recv(int len) 
{
    for (int i=0; i<len; i++) codec_i2c_buff[i] = Wire1.read();
    codec_i2c_last_len = len;
}

// Called when the I2C slave is read from
void codec_i2c_req() 
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

