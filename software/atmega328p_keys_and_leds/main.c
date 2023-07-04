///////////////////////////////////////////////////////////////////////////////
//
// atmega328p_keys_and_leds.c
//
// This is the firmware for the little ATMEGA328P MCU of the Pico DAW.
// It controls the LED's, reads the keyboard, and communicates with
// Pi Pico over an I2C interface.
//
// SWITCH MATRIX - PHYSICAL
//
// There's a row of eight click-able rotary encoders up top, and each
// contains three outputs: click, quadrature A, and quadrature B.
//
// Beneath the encoders are six rows of eight SPDT pushbuttons.
// Each button has one LED and two outputs: a normally-closed contact,
// and a normally-open contact.
//
// When a button is pressed or released, we can time the break-before-make
// between the two contacts to get an indication of velocity, though the
// clicky nature of these buttons distorts the results.
//
// Seven pushbuttons are left vacant in the design.
//
// SWITCH MATRIX AND LED MATRIX - ELECTRICAL
//
// The electrical switch matrix has fifteen rows of eight columns,
// accounting for all the outputs of the encoders and pushbuttons.
//
// Each device occupies one column of the matrix, and either three or
// two rows, respectively, for encoders or pushbuttons.
//
// The top three rows are for the encoders:
//
// Row 0: Encoder Quadrature A
// Row 1: Encoder Quadrature B
// Row 2: Encoder Click
//
// The remaining twelve rows are for the pushbuttons:
//
// Row 3: Pushbutton row 0 contact, Normally-Open
// Row 4: Pushbutton row 0 contact, Normally-Closed
// ...
// Row 13: Pushbutton row 5 contact, Normally-Open
// Row 14: Pushbutton row 5 contact, Normally-Closed
//
// Each button has an LED, so there is also a LED matrix which is
// electrically distinct from the switch matrix. It is six rows
// by eight columns.
//
// MATRIX INTERFACE
//
// A naive approach for hooking both the matrices up to an MCU might
// to to hook each row and column to a GPIO on the MCU, but there are
// 37 of them! We don't have that many pins to spare.
//
// One alternative is to use an IC called a 74LS164D, which is a
// serial-in, parallel-out shift register. It has two inputs,
// clock and data, and eight outputs. You can clock eight bits into
// the chip to set the state of outputs, using only two GPIO pins.
//
// These chips can also be cascaded to add more outputs without
// needing any more GPIO's.
//
// Two of these chips are cascaded to allow the MCU to select which
// of the 15 switch rows to read. A row is selected by outputting
// a zero state (0 Volts) on that row, and one states (5V) on all
// the other rows.
//
// Each column is hooked up to a GPIO input, and reading them
// will let us determine the state of the switches in the selected
// row. (Open switches will read one, and closed switches will read
// zero.)
//
// On startup, the row selection is initialized by clocking in sixteen
// ones, followed by a single zero. This selects the first switch row.
//
// Fifteen times, we read PORTD and then clock in a one, which shifts
// that zero bit down to select the next row.
//
// After all fifteen rows have been read, we clock in a single zero,
// which shifts that old zero out of the top of the register and
// selects the first row again.
//
// The circuitry for the LED matrix is similar, but now one shift
// register selects LED row, and another shift register sets the
// state of all the columns of the selected row. The column-select
// is the same 74LS164D as was used on the switch matrix, but the
// row select is a slightly different chip: It is a 74LS595D, which
// adds two inputs: latch-enable and output-enable. This is so we
// can turn off the output-enable (turning off all LEDs) whenever
// new column data is shifted in, so OFF LEDs wont be seen dimly
// flickering during the update process.
//
// We want to scan the key matrix quickly to allow velocity measurement,
// but we can update the LED matrix much slower: one LED row for every full
// scan of the KB, or all LED rows will be updated every six KB scans.
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
#include "TWI_Master.h"
// ----------------------------------------------------------------------------
#define MAIN_CPU_HZ     8000000.0
#define KB_FULLSCAN_HZ  1000.0    // (333 Hz LED update rate)
// ----------------------------------------------------------------------------
uint8_t sw_states[16];
uint8_t led_states[6];
uint8_t cur_led_row = 0;  // current led row for refresh
unsigned char TWI_targetSlaveAddress = 0x30;
// ----------------------------------------------------------------------------
// timer ISR sets this flag to tell main-loop to scan the key matrix again
// ----------------------------------------------------------------------------
volatile uint8_t kb_scan_flag = 0;
// ----------------------------------------------------------------------------
// Setup the GPIO pins, event timer(s), and I2C communications.
// ----------------------------------------------------------------------------
void init(void)
{
  // Setup GPIO pins.

  // PORTB: 7: NC,          6: NC,          5: LED_COL_CLK, 4: LED_COL_DAT,
  //        3: LED_ROW_CLK, 2: LED_ROW_DAT, 1: SW_ROW_CLK,  0: SW_ROW_DAT
  DDRB  = 0b00111111;
  PORTB = 0b11111111;
  // PORTC: 7: NC,          6: NC,          5: I2C_SCL,     4: I2C_SDA,
  //        3: NC,          2: NC,          1: NC,          0: SHIFTREGS_RESET
  DDRC  = 0b00000001;
  PORTC = 0b11001111;
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
  // timer 0 - set TOP value for desired rollover rate
  OCR0A = MAIN_CPU_HZ / 256.0 / KB_FULLSCAN_HZ;
  // enable timer 0 rollover interrupt
  TIMSK0 = 0x01;

  // Setup our main output interface (i2c) to the Pi Pico W (main) CPU.
  TWI_Master_Initialise();

  // enable interrupts
  sei();
}
// ----------------------------------------------------------------------------
ISR (TIMER0_OVF_vect)
{
  kb_scan_flag = 1;
}
// ----------------------------------------------------------------------------
// Initialize the state of all shift registers.
// Selects the first row each of both the switch and the LED matrices.
// ----------------------------------------------------------------------------
inline void regs_reset(void)
{
  // De-select all switch rows, all led rows, and all LED columns
  PORTB |=  0x11;  // LED_COL_DAT = 1, SW_ROW_DAT = 1
  PORTB &= ~0x04;  // LED_ROW_DAT = 0
  for (uint8_t i=0;i<16;i++)
  {
    PORTB &= ~0x2A; // start clock pulses on led row, sw row, and sw col
    PORTB |=  0x2A; // end clock pulses
  }
  // clock a single 0 into the sw row reg to select the first sw row,
  // and clock a single 1 into the led row reg to select the first led row
  PORTB &= 0xFE;  // SW_ROW_DAT = 0
  PORTB |= 0x04;  // LED_ROW_DAT = 1
  PORTB &= ~0x0A; // LED_ROW_CLK = 0, SW_ROW_CLK = 0
  PORTB |= 0x0A;  // LED_ROW_CLK = 1, SW_ROW_CLK = 1
  PORTB &= ~0x04; // LED_ROW_DAT = 0
  PORTB |= 0x01;  // SW_ROW_DAT = 1
}
// ----------------------------------------------------------------------------
// resets all shift regs to 0's - not really very useful
// ----------------------------------------------------------------------------
inline void regs_clear(void)
{
  PORTC &= 0xFE; // start reset pulse
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
    PORTB |= 0x20;    // LED_COL_CLK = 1
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
    PORTB |= 0x08;  // LED_ROW_CLK = 1
    // don't select more than one LED row at a time
    PORTB &= ~0x04; // LED_ROW_DAT = 0
  } else  // still have led rows left to update
  {
    // select next led row
    PORTB &= ~0x08; // LED_ROW_CLK = 0
    PORTB |= 0x08;  // LED_ROW_CLK = 1
  }
}
// ----------------------------------------------------------------------------
// begins a full scan of the keyboard matrix
// ----------------------------------------------------------------------------
inline void scan_kb(void)
{
  // sample all 15 switch rows
  for(uint8_t row=0;row<15;row++)
  {
    uint8_t b = PIND;
    sw_states[row+1] = b;  // read the eight columns of the switch row
    PORTB &= ~0x02; // SW_ROW_CLK = 0
    PORTB |= 0x02;  // SW_ROW_CLK = 1
  }
  sw_states[0] = TWI_targetSlaveAddress << 1;
  TWI_Start_Transceiver_With_Data( sw_states, 16 );
  // re-select first switch row
  PORTB &= ~0x01; // SW_ROW_DAT = 0
  PORTB &= ~0x02; // SW_ROW_CLK = 0
  PORTB |= 0x02;  // SW_ROW_CLK = 1
  PORTB |= 0x01;  // SW_ROW_DAT = 1
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
int main(void)
{
    uint8_t rows = 0;
    init();       // init MCU IO and timers
    regs_clear();
    regs_reset(); // select first switch row and first LED row
    while (1)     // begin main-loop
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
        if(kb_scan_flag)  // too slow! go into guru meditation mode
        {
          do_guru_meditation();
        }
      }           // finished a scan of the keyboard
    }             // end main-loop
}
///////////////////////////////////////////////////////////////////////////////
// EOF
///////////////////////////////////////////////////////////////////////////////
