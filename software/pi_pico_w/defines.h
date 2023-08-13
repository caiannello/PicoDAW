#ifndef __DEFINES_H__
#define __DEFINES_H__

// some 16-bit colors RGB:565
#define TFT_VFD_BLUWHT   0xCF7B   // sampled frm a classic vacuum fluorescent display in a VCR
#define TFT_VFD_ORANGE   0xEB43   // sampled from same VCR

// GPIO pin numbers audio codec I2S
#define pBCLK 27
#define pWS (pBCLK+1)
#define pDOUT 26
#define pMCLK 16
// audio codec I2C
#define pSDA  14
#define pSCL  15
// keyboard controller I2C
#define kSDA  12
#define kSCL  13

#define NAU8822_I2C_ADDRESS 0x1A

#define SAMPLE_RATE_HZ 48000

#endif