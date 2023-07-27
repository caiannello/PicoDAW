Note, this Arduino project uses the excellent [TFT LCD library, TFT_eSPI, by Bodmer.](https://github.com/Bodmer/TFT_eSPI)

Before compiling, you have to customize one file of the library to reflect the GPIO connections which this project has defined for control of the LCD. 

In "Arduino/libraries/TFT_eSPI-master/User_setup.h":
```
#define USER_SETUP_INFO "User_Setup"
#define RPI_ILI9486_DRIVER // 20MHz maximum SPI

#define TFT_SPI_PORT 0
#define TFT_MISO  0   // Automatically assigned with ESP8266 if not defined
#define TFT_MOSI  3   // Automatically assigned with ESP8266 if not defined
#define TFT_SCLK  2   // Automatically assigned with ESP8266 if not defined
#define TFT_CS    1   // Chip select control pin D8
#define TFT_DC    4   // Data Command control pin
#define TFT_RST   12  // Reset pin (could connect to NodeMCU RST, see next line)
#define TOUCH_CS 8    // Chip select pin (T_CS) of touch screen

#define SPI_FREQUENCY  27000000
#define SPI_READ_FREQUENCY  20000000
```



