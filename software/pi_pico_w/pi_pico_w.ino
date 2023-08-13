#include <SPI.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <I2S.h>
#include "hardware/pwm.h"
#include "defines.h"
#include "mock_screen.h"

static char buff[100];
static char codec_i2c_buff[100];

TFT_eSPI tft = TFT_eSPI();       // Invoke custom library

volatile uint8_t last_len = 0;
volatile uint8_t codec_i2c_last_len = 0;

// Create the I2S port using a PIO state machine
I2S i2s(OUTPUT);

/*
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
*/

void I2C_WriteWAU8822(uint8_t u8addr, uint16_t u16data) 
{
  Wire1.beginTransmission(NAU8822_I2C_ADDRESS);
  Wire1.write(u8addr<<1);
  Wire1.write((uint8_t)((u8addr << 1) | (u16data >> 8)));
  Wire1.write((uint8_t)(u16data & 0x00FF));
  Wire1.endTransmission();
}

uint16_t I2C_ReadWAU8822(uint8_t u8addr) 
{
  uint16_t res = 0;
  Wire1.beginTransmission(NAU8822_I2C_ADDRESS);
  Wire1.write(u8addr<<1);
  Wire1.requestFrom(NAU8822_I2C_ADDRESS,2,true);
  res=Wire1.read();
  res<<=8;
  res|=Wire1.read();
  Wire1.endTransmission();
  return res;
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

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, 1);

  Serial.begin(115200);
  Serial.println("I2S simple tone");

  // kb controller i2c
  pinMode(kSDA,INPUT);
  pinMode(kSCL,INPUT);
  Wire.onReceive(recv);
  Wire.onRequest(req);
  Wire.setSDA(kSDA);
  Wire.setSCL(kSCL);
  Wire.begin(0x30);

  // audio codec i2c
  pinMode(pSDA,INPUT);
  pinMode(pSCL,INPUT);
  Wire1.setSDA(pSDA);
  Wire1.setSCL(pSCL);
  Wire1.setClock(100000);
  Wire1.onReceive(codec_i2c_recv);
  Wire1.onRequest(codec_i2c_req);
  Wire1.begin();

  // audio codec init 
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
  //I2S_EnableMCLK(12000000);
  WAU8822_ConfigSampleRate(SAMPLE_RATE_HZ);
  
  tft.setTextSize(2);
  tft.setCursor(0, 5);

  // start I2S at the sample rate with 16-bits per sample
  i2s.setMCLK(pMCLK);
  i2s.setMCLKmult(256);
  i2s.setBCLK(pBCLK);
  i2s.setDATA(pDOUT);
  i2s.setBitsPerSample(16);
  i2s.setBuffers(2, 256, 0);

  if (!i2s.begin(SAMPLE_RATE_HZ)) 
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
    for (int i=0; i<len; i++) buff[i] = Wire.read();
    last_len = len;
}


// Called when the I2C slave is read from
void req() 
{
    static int ctr = 765;
    char buff[7];
    // Return a simple incrementing hex value
    sprintf(buff, "%06X", (ctr++) % 65535);
    Wire.write(buff, 6);
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

void draw_mockup(void)
{
  uint16_t *p = (uint16_t *)(gimp_image.pixel_data);
  for(int y=0;y<320;y++)
  for(int x=0;x<480;x++)
  {
    tft.drawFastHLine(x, y, 1, *p);
    p++;
  }  
}

void switch_test(void)
{
  draw_mockup();

  for(int y=20;y<42;y++)
    tft.drawFastHLine(0, y, 480, 0);
  
  tft.setTextSize(2);
  char s[6];
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
    uint16_t rval = I2C_ReadWAU8822(36);
    sprintf(s," %04X",buff[rval]);
    strcat(lineout,s);


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

void codec_test(void)
{
  tft.setTextSize(2);
  tft.setCursor(0, 25);
  tft.setTextColor(TFT_VFD_BLUWHT);//TFT_VFD_ORANGE);
  tft.print("I hope she's making some noise!!!");
  while(1)
  {
      i2s.write16(rand()%65536,rand()%65536);
  };
}

void loop() 
{
  switch_test();
  //codec_test();
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

