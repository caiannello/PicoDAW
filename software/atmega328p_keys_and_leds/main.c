///////////////////////////////////////////////////////////////////////////////
//
// atmega328p_keys_and_leds.c
//
// This is the firmware for the little ATMEGA328P MCU of the Pico DAW.
//
// It uses 74LS164D shift-register IC's for a couple different purposes:
//
// 1. Scans successive rows of key-switches, eight at a time,
// to sample which ones might be pressed down.
//
// There are 15 rows of switches. We walk a single 0-bit through the
// cascaded row-select registers to select that one row at a time.
// If any switch on that row is pressed down, the corresponding
// column-input bit on PORTD will go low.
//
// We read PORTD once per row, and note which switches have changed
// states.
//
// The keys are single-pole-double-throw, so each key actually contains
// two switches: A normally closed one, and a normally open one. By quickly
// scanning the matrix, we can time the break-before-make of the switches
// to get an indication of how quickly each key has been depressed or
// released. (Our switches are fairly clicky though, and this snap-action
// somewhat defeats the velocity sensing. We're going to try it anyway.)
//
// 2. Selects successive rows of LED's, eight at a time, to illuminate
// selected LEDS. Each LED can be set to 0: Off, 1: Slow Blink,
// 2: Fast Blink, or 3: ON.
//
// As keyboard events occur, we queue them for output to the main CPU,
// a Raspberry Pi Pico-W. The interface is a two-wire I2C bus.
//
// As keyboard events go out to the Pico, LED state changes come back in.
//
// Created: 6/19/2023 9:52:17 PM
// Author : Craig Iannello  www.pugbutt.com
//
///////////////////////////////////////////////////////////////////////////////
#include <avr/io.h>
#include <avr/interrupt.h>
// ----------------------------------------------------------------------------
#define MAIN_CPU_HZ     8000000.0
#define KB_FULLSCAN_HZ  1000.0
// ----------------------------------------------------------------------------
// The switch matrix is 15x8, with 120 intersections, but 7 are unpopulated.
// The led matrix is 6x8, so there's 48 LEDs.
// ----------------------------------------------------------------------------
uint8_t sw_states[15];
uint8_t led_states[6];
uint8_t cur_led_row = 0;  // current led row for refresh
// ----------------------------------------------------------------------------
// timer ISR sets this flag to tell mainloop to scan the key matrix again
// ----------------------------------------------------------------------------
volatile uint8_t kb_scan_flag = 0;
// ----------------------------------------------------------------------------
// Setup the GPIO pins, event timer(s), and I2C communications.
// ----------------------------------------------------------------------------
void init(void)
{
  // PORTB: 7: NC,          6: NC,          5: LED_COL_CLK, 4: LED_COL_DAT,
  //        3: LED_ROW_CLK, 2: LED_ROW_DAT, 1: SW_ROW_CLK,  0: SW_ROW_DAT
  DDRB  = 0b00111111;
  PORTB = 0b11111111;
  // PORTC: 7: NC,          6: NC,          5: I2C_SCL,     4: I2C_SDA,
  //        3: NC,          2: NC,          1: NC,          0: SHIFTREGS_RESET
  DDRC  = 0b00000001;
  PORTC = 0b11111111;
  // PORTD: 7: SW_COL_7,    6: SW_COL_6,    5: SW_COL_5,    4: SW_COL_4,
  //        3: SW_COL_3,    2: SW_COL_2,    1: SW_COL_1,    0: SW_COL_0
  DDRD  = 0b00000000;
  PORTD = 0b00000000;

  // disable interrupts

  cli();

  // timer 0 : Each rollover starts a full scan of the key matrix
  // and updates 1 of the 6 LED rows

  // timer 0 - Fast PWM, rollover at TOP (OCR0A) , prescaler: clk/256
  TCCR0A = 0x03;
  TCCR0B = 0x0C;

  // timer 0 - set TOP value for desires rollover rate
  OCR0A = MAIN_CPU_HZ / 256.0 / KB_FULLSCAN_HZ;

  // enable rollover interrupt
  TIMSK0 = 0x01;
  // enable interrupts
  sei();
}
// ----------------------------------------------------------------------------
ISR (TIMER0_OVF_vect)
{
  kb_scan_flag = 1;
}
// ----------------------------------------------------------------------------
// Delay used for shift-register clock pulses
// ----------------------------------------------------------------------------
inline void clk_delay(void)
{
    //asm("nop;");
}
// ----------------------------------------------------------------------------
// Initialize the state of all shift registers.
// Selects the first row each of both the switch and the LED matrices.
//
// Done periodically, in case a noise spike glitched things out of sync.
// ----------------------------------------------------------------------------
inline void regs_reset(void)
{
  // De-select all switch rows, all led rows, and all LED columns
  PORTB |=  0x11;  // LED_COL_DAT = 1, SW_ROW_DAT = 1
  PORTB &= ~0x04;  // LED_ROW_DAT = 0
  for (uint8_t i=0;i<16;i++)
  {
    PORTB &= ~0x2A; // start clock pulses on led row, sw row, and sw col
    clk_delay();
    PORTB |=  0x2A; // end clock pulses
    clk_delay();
  }
  // clock a single 0 into the sw row reg to select the first sw row,
  // and clock a single 1 into the led row reg to select the first led row
  PORTB &= 0xFE;  // SW_ROW_DAT = 0
  PORTB |= 0x04;  // LED_ROW_DAT = 1
  PORTB &= ~0x0A; // LED_ROW_CLK = 0, SW_ROW_CLK = 0
  clk_delay();
  PORTB |= 0x0A;  // LED_ROW_CLK = 1, SW_ROW_CLK = 1
  clk_delay();
  PORTB &= ~0x04;  // LED_ROW_DAT = 0
}
// ----------------------------------------------------------------------------
// resets all shiftregs to 0's - not really very useful
// ----------------------------------------------------------------------------
inline void regs_clear(void)
{
  PORTC &= 0xFE; // start reset pulse
  clk_delay();
  PORTC |= 0x01; // end reset pulse
}
// ----------------------------------------------------------------------------
// Shifts 8 bits into the LED columns register. Bits which are set will cause
// the corresponding LED to be lit on the currently selected row.
// ----------------------------------------------------------------------------
inline void set_led_row(uint8_t cols)
{
  for(uint8_t i=0;i<8;i++)
  {
    PORTB &= ~0x10;    // LED_COL_DAT = 0
    if(!(cols&1))
      PORTB|= 0x10;   // LED_COL_DAT = NOT cols.LSB
    // clock the bit value into the cols register
    PORTB &= ~0x20;   // LED_COL_CLK = 0
    //clk_delay();
    PORTB |= 0x20;    // LED_COL_CLK = 1
    //clk_delay();
    cols>>=1;         // select next bit of cols value
  }

}
// ----------------------------------------------------------------------------
inline void refresh_led_row(void)
{
  set_led_row(led_states[cur_led_row]);
  cur_led_row++;
  if(cur_led_row == 6) // if finished all LED rows
  {
    // reselect first row of leds
    cur_led_row = 0;
    PORTB |= 0x04;  // LED_ROW_DAT = 1
    PORTB &= ~0x08; // LED_ROW_CLK = 0
    clk_delay();
    PORTB |= 0x08;  // LED_ROW_CLK = 1
    clk_delay();
    // don't select more than one LED row at a time
    PORTB &= ~0x04; // LED_ROW_DAT = 0
  } else  // still have led rows left to update
  {
    // select next led row
    PORTB &= ~0x08; // LED_ROW_CLK = 0
    clk_delay();
    PORTB |= 0x08;  // LED_ROW_CLK = 1
    clk_delay();
  }
}
// ----------------------------------------------------------------------------
// begins a full scan of the keyboard matrix
// ----------------------------------------------------------------------------
inline void scan_kb(void)
{

}
// ----------------------------------------------------------------------------
// indicate error state by forever animating first row of LEDs
// ----------------------------------------------------------------------------
void do_guru_meditation()
{
  uint16_t guru_ticks=0;
  uint8_t v = 0x80;
  regs_reset();
  while(1)
  {
    if(kb_scan_flag)
    {
      kb_scan_flag=0;
      if(++guru_ticks > 40)
      {
        guru_ticks = 0;
        set_led_row(v);
        v>>=1;
        if(v==0)
          v=0x80;
      }
    }
  }
}
// ----------------------------------------------------------------------------
// Program entrypoint and mainloop
// ----------------------------------------------------------------------------
int main(void)
{
    uint8_t rows = 0;
    init();
    regs_clear();
    regs_reset();

    // write some random vals to the LED states
    srand(314);
    for(uint8_t i = 0; i<6;i++)
      led_states[i]=rand()%256;

    while (1) // begin mainloop
    {
      if(kb_scan_flag)
      {
        kb_scan_flag = 0;
        refresh_led_row();
        scan_kb();
        if(++rows==12)
        {
          rows=0;
          led_states[rand()%6] = rand()%256;

        }
        if(kb_scan_flag)  // too slow! go into guru meditation
        {
          do_guru_meditation();
        }

      }

    }  // end mainloop
}
///////////////////////////////////////////////////////////////////////////////
// EOF
///////////////////////////////////////////////////////////////////////////////
