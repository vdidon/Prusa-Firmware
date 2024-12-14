//menu.cpp

#include "lcd.h"
#include <stdio.h>
#include <stdarg.h>
#include <avr/pgmspace.h>
#include <util/atomic.h>
#include <util/delay.h>
#include "Timer.h"

#include "Configuration.h"
#include "pins.h"
#include <Arduino.h>
#include "Marlin.h"
#include "fastio.h"
#include "sound.h"
#include "backlight.h"

#define LCD_DEFAULT_DELAY 100

#if (defined(LCD_PINS_D0) && defined(LCD_PINS_D1) && defined(LCD_PINS_D2) && defined(LCD_PINS_D3))
	#define LCD_8BIT
#endif

// commands
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_CURSORSHIFT 0x10
#define LCD_FUNCTIONSET 0x20
#define LCD_SETCGRAMADDR 0x40
#define LCD_SETDDRAMADDR 0x80

// flags for display entry mode
#define LCD_ENTRYRIGHT 0x00
#define LCD_ENTRYLEFT 0x02
#define LCD_ENTRYSHIFTINCREMENT 0x01
#define LCD_ENTRYSHIFTDECREMENT 0x00

// flags for display on/off control
#define LCD_DISPLAYON 0x04
#define LCD_DISPLAYOFF 0x00
#define LCD_CURSORON 0x02
#define LCD_CURSOROFF 0x00
#define LCD_BLINKON 0x01
#define LCD_BLINKOFF 0x00

// flags for display/cursor shift
#define LCD_DISPLAYMOVE 0x08
#define LCD_CURSORMOVE 0x00
#define LCD_MOVERIGHT 0x04
#define LCD_MOVELEFT 0x00

// flags for function set
#define LCD_8BITMODE 0x10
#define LCD_4BITMODE 0x00
#define LCD_2LINE 0x08
#define LCD_1LINE 0x00
#define LCD_5x10DOTS 0x04
#define LCD_5x8DOTS 0x00

// bitmasks for flag argument settings
#define LCD_RS_FLAG 0x01
#define LCD_HALF_FLAG 0x02

constexpr uint8_t row_offsets[] PROGMEM = { 0x00, 0x40, 0x14, 0x54 };

FILE _lcdout; // = {0}; Global variable is always zero initialized, no need to explicitly state that.

static uint8_t lcd_displayfunction = 0;
static uint8_t lcd_displaycontrol = 0;
static uint8_t lcd_displaymode = 0;

uint8_t lcd_currline;
static uint8_t lcd_ddram_address; // no need for preventing ddram overflow

struct CustomCharacter {
    uint8_t colByte;
    uint8_t rowData[4];
    char alternate;
};

static uint8_t lcd_custom_characters[8] = {0};
static const CustomCharacter Font[] PROGMEM = {
#include "FontTable.h"
};

#define CUSTOM_CHARACTERS_CNT (sizeof(Font) / sizeof(Font[0]))

static void lcd_display(void);
static void lcd_print_custom(uint8_t c);
static void lcd_invalidate_custom_characters();

static void lcd_pulseEnable(void)
{
	WRITE(LCD_PINS_ENABLE,HIGH);
	_delay_us(1);    // enable pulse must be >450ns
	WRITE(LCD_PINS_ENABLE,LOW);
}

static void lcd_writebits(uint8_t value)
{
#ifdef LCD_8BIT
	WRITE(LCD_PINS_D0, value & 0x01);
	WRITE(LCD_PINS_D1, value & 0x02);
	WRITE(LCD_PINS_D2, value & 0x04);
	WRITE(LCD_PINS_D3, value & 0x08);
#endif
	WRITE(LCD_PINS_D4, value & 0x10);
	WRITE(LCD_PINS_D5, value & 0x20);
	WRITE(LCD_PINS_D6, value & 0x40);
	WRITE(LCD_PINS_D7, value & 0x80);

	lcd_pulseEnable();
}

static void lcd_send(uint8_t data, uint8_t flags, uint16_t duration = LCD_DEFAULT_DELAY)
{
	WRITE(LCD_PINS_RS,flags&LCD_RS_FLAG);
	_delay_us(5);
	lcd_writebits(data);
#ifndef LCD_8BIT
	if (!(flags & LCD_HALF_FLAG)) {
		// _delay_us(LCD_DEFAULT_DELAY); // should not be needed when sending a two nibble instruction.
		lcd_writebits((data << 4) | (data >> 4)); //force efficient swap opcode even though the lower nibble is ignored in this case
	}
#endif
	delayMicroseconds(duration);
}

static void lcd_command(uint8_t value, uint16_t duration = LCD_DEFAULT_DELAY)
{
	lcd_send(value, LOW, duration);
}

static void lcd_write(uint8_t value)
{
	if (value == '\n') {
		if (lcd_currline > 3) lcd_currline = -1;
		lcd_set_cursor(0, lcd_currline + 1); // LF
	} else if ((value >= 0x80) && (value < (0x80 + CUSTOM_CHARACTERS_CNT))) {
		lcd_print_custom(value);
	} else {
		lcd_send(value, HIGH);
		lcd_ddram_address++; // no need for preventing ddram overflow
	}
}

static void lcd_begin(uint8_t clear)
{
	lcd_currline = 0;
	lcd_ddram_address = 0;

	lcd_invalidate_custom_characters();

	lcd_send(LCD_FUNCTIONSET | LCD_8BITMODE, LOW | LCD_HALF_FLAG, 4500); // wait min 4.1ms
	// second try
	lcd_send(LCD_FUNCTIONSET | LCD_8BITMODE, LOW | LCD_HALF_FLAG, 150);
	// third go!
	lcd_send(LCD_FUNCTIONSET | LCD_8BITMODE, LOW | LCD_HALF_FLAG, 150);
#ifndef LCD_8BIT
	// set to 4-bit interface
	lcd_send(LCD_FUNCTIONSET | LCD_4BITMODE, LOW | LCD_HALF_FLAG, 150);
#endif

	// finally, set # lines, font size, etc.0
	lcd_command(LCD_FUNCTIONSET | lcd_displayfunction);
	// turn the display on with no cursor or blinking default
	lcd_displaycontrol = LCD_CURSOROFF | LCD_BLINKOFF;
	lcd_display();
	// clear it off
	if (clear) lcd_clear();
	// Initialize to default text direction (for romance languages)
	lcd_displaymode = LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;
	// set the entry mode
	lcd_command(LCD_ENTRYMODESET | lcd_displaymode);
}

static int lcd_putchar(char c, FILE *)
{
	lcd_write(c);
	return 0;
}

void lcd_init(void)
{
	WRITE(LCD_PINS_ENABLE,LOW);
	SET_OUTPUT(LCD_PINS_RS);
	SET_OUTPUT(LCD_PINS_ENABLE);

#ifdef LCD_8BIT
	SET_OUTPUT(LCD_PINS_D0);
	SET_OUTPUT(LCD_PINS_D1);
	SET_OUTPUT(LCD_PINS_D2);
	SET_OUTPUT(LCD_PINS_D3);
#endif
	SET_OUTPUT(LCD_PINS_D4);
	SET_OUTPUT(LCD_PINS_D5);
	SET_OUTPUT(LCD_PINS_D6);
	SET_OUTPUT(LCD_PINS_D7);

#ifdef LCD_8BIT
	lcd_displayfunction |= LCD_8BITMODE;
#endif
	lcd_displayfunction |= LCD_2LINE;
	_delay_us(50000);
	lcd_begin(1); //first time init
	fdev_setup_stream(lcdout, lcd_putchar, NULL, _FDEV_SETUP_WRITE); //setup lcdout stream
}

void lcd_refresh(void)
{
    lcd_begin(1);
}

void lcd_refresh_noclear(void)
{
    lcd_begin(0);
}

// Clear display, set cursor position to zero and unshift the display. It also invalidates all custom characters
void lcd_clear(void)
{
	lcd_command(LCD_CLEARDISPLAY, 1600);
	lcd_currline = 0;
	lcd_ddram_address = 0;
	lcd_invalidate_custom_characters();
}

// Set cursor position to zero and in DDRAM. It does not unshift the display.
void lcd_home(void)
{
	lcd_set_cursor(0, 0);
	lcd_ddram_address = 0;
}

// Turn the display on/off (quickly)
void lcd_display(void)
{
    lcd_displaycontrol |= LCD_DISPLAYON;
    lcd_command(LCD_DISPLAYCONTROL | lcd_displaycontrol);
}

/// @brief set the current LCD row
/// @param row LCD row number, ranges from 0 to LCD_HEIGHT - 1
static void FORCE_INLINE lcd_set_current_row(uint8_t row)
{
	lcd_currline = min(row, LCD_HEIGHT - 1);
}

/// @brief Calculate the LCD row offset
/// @param row LCD row number, ranges from 0 to LCD_HEIGHT - 1
/// @return row offset which the LCD register understands
static uint8_t __attribute__((noinline)) lcd_get_row_offset(uint8_t row)
{
	return pgm_read_byte(row_offsets + min(row, LCD_HEIGHT - 1));
}

void lcd_set_cursor(uint8_t col, uint8_t row)
{
	lcd_set_current_row(row);
    uint8_t addr = col + lcd_get_row_offset(lcd_currline);
	lcd_ddram_address = addr;
	lcd_command(LCD_SETDDRAMADDR | addr);
}

void lcd_set_cursor_column(uint8_t col)
{
	uint8_t addr = col + lcd_get_row_offset(lcd_currline);
	lcd_ddram_address = addr;
	lcd_command(LCD_SETDDRAMADDR | addr);
}

// Allows us to fill the first 8 CGRAM locations
// with custom characters
void lcd_createChar_P(uint8_t location, const CustomCharacter *char_p)
{
	uint8_t charmap[8]; // unpacked font data

	// The LCD expects the CGRAM data to be sent as pixel data, row by row. Since there are 8 rows per character, 8 bytes need to be sent.
	// However, storing the data in the flash as the LCD expects it is wasteful since 3 bits per row are don't care and are not used.
	// Therefore, flash can be saved if the character data is packed. For the AVR to unpack efficiently and quickly, the following scheme was used:
	//
	// colbyte data0 data1 data2 data3
	//    a      b     c     d     e
	//
	// ** ** ** b7 b6 b5 b4 a0
	// ** ** ** b3 b2 b1 b0 a1
	// ** ** ** c7 c6 c5 c4 a2
	// ** ** ** c3 c2 c1 c0 a3
	// ** ** ** d7 d6 d5 d4 a4
	// ** ** ** d3 d2 d1 d0 a5
	// ** ** ** e7 e6 e5 e4 a6
	// ** ** ** e3 e2 e1 e0 a7
	//
	// The bits marked as ** in the unpacked data are don't care and they will contain garbage.

	uint8_t temp;
	uint8_t colByte;
	__asm__ __volatile__ (
		// load colByte
		"lpm %1, Z+" "\n\t"

		// begin for loop
		"ldi %0, 8" "\n\t"
		"mov __zero_reg__, %0" "\n\t"		// use zero_reg as loop counter
		"forBegin_%=: " "\n\t"
			"sbrs __zero_reg__, 0" "\n\t"	// test LSB of counter. Fetch new data if counter is even
			"lpm __tmp_reg__, Z+" "\n\t"	// load next data byte from progmem, increment
			"swap __tmp_reg__" "\n\t"		// swap the nibbles
			"mov %0, __tmp_reg__" "\n\t"	// copy row data to temp

			// "andi %0, 0xF" "\n\t"			// mask lower nibble - Not needed since bits 7-5 of the CGRAM are don't care, so they can contain garbage
			"ror %1" "\n\t" 				// consume LSB of colByte and push it to the carry
			"rol %0" "\n\t"					// insert the column LSB from carry
			"st %a3+, %0" "\n\t"			// push the generated row data to the output
		// end for loop
		"dec __zero_reg__" "\n\t"
		"brne forBegin_%=" "\n\t"

		: "=&d" (temp), "=&r" (colByte)
		: "z" (char_p), "e" (charmap)
	);

	lcd_command(LCD_SETCGRAMADDR | (location << 3));
	for (uint8_t i = 0; i < 8; i++) {
		lcd_send(charmap[i], HIGH);
	}
	lcd_command(LCD_SETDDRAMADDR | lcd_ddram_address); // no need for masking the address
}

int lcd_putc(char c)
{
	return fputc(c, lcdout);
}

int lcd_putc_at(uint8_t c, uint8_t r, char ch)
{
	lcd_set_cursor(c, r);
	return fputc(ch, lcdout);
}

int lcd_puts_P(const char* str)
{
	return fputs_P(str, lcdout);
}

int lcd_puts_at_P(uint8_t c, uint8_t r, const char* str)
{
	lcd_set_cursor(c, r);
	return fputs_P(str, lcdout);
}

int lcd_printf_P(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	int ret = vfprintf_P(lcdout, format, args);
	va_end(args);
	return ret;
}

void lcd_space(uint8_t n)
{
	while (n--) lcd_putc(' ');
}


void lcd_print(const char* s)
{
	while (*s) lcd_write(*(s++));
}

uint8_t lcd_print_pad(const char* s, uint8_t len)
{
    while (len && *s) {
        lcd_write(*(s++));
        --len;
    }
    lcd_space(len);
    return len;
}

uint8_t lcd_print_pad_P(const char* s, uint8_t len)
{
    while (len && pgm_read_byte(s)) {
        lcd_write(pgm_read_byte(s++));
        --len;
    }
    lcd_space(len);
    return len;
}

void lcd_print(char c, int base)
{
	lcd_print((long) c, base);
}

void lcd_print(unsigned char b, int base)
{
	lcd_print((unsigned long) b, base);
}

void lcd_print(int n, int base)
{
	lcd_print((long) n, base);
}

void lcd_print(unsigned int n, int base)
{
	lcd_print((unsigned long) n, base);
}

void lcd_print(long n, int base)
{
	if (base == 0)
		lcd_write(n);
	else if (base == 10)
	{
		if (n < 0)
		{
			lcd_print('-');
			n = -n;
		}
		lcd_printNumber(n, 10);
	}
	else
		lcd_printNumber(n, base);
}

void lcd_print(unsigned long n, int base)
{
	if (base == 0)
		lcd_write(n);
	else
		lcd_printNumber(n, base);
}

void lcd_printNumber(unsigned long n, uint8_t base)
{
	unsigned char buf[8 * sizeof(long)]; // Assumes 8-bit chars.
	uint8_t i = 0;
	if (n == 0)
	{
		lcd_print('0');
		return;
	}
	while (n > 0)
	{
		buf[i++] = n % base;
		n /= base;
	}
	for (; i > 0; i--)
		lcd_print((char) (buf[i - 1] < 10 ?	'0' + buf[i - 1] : 'A' + buf[i - 1] - 10));
}

uint8_t lcd_draw_update = 2;
int16_t lcd_encoder = 0;
static int8_t lcd_encoder_diff = 0;

uint8_t lcd_click_trigger = 0;
uint8_t lcd_update_enabled = 1;
static bool lcd_backlight_wake_trigger; // Flag set by interrupt when the knob is pressed or rotated

uint32_t lcd_next_update_millis = 0;



lcd_longpress_func_t lcd_longpress_func = 0;

lcd_lcdupdate_func_t lcd_lcdupdate_func = 0;

static ShortTimer buttonBlanking;
ShortTimer longPressTimer;
LongTimer lcd_timeoutToStatus;


//! @brief Was button clicked?
//!
//! Consume click event, following call would return 0.
//! See #LCD_CLICKED macro for version not consuming the event.
//!
//! Generally is used in modal dialogs.
//!
//! @retval 0 not clicked
//! @retval nonzero clicked
uint8_t lcd_clicked(void)
{
	bool clicked = LCD_CLICKED;
	if(clicked)
	{
	    lcd_consume_click();
	}
    return clicked;
}

void lcd_beeper_quick_feedback(void) {
	Sound_MakeSound(e_SOUND_TYPE_ButtonEcho);
}

void lcd_quick_feedback(void)
{
  lcd_draw_update = 2;
  lcd_beeper_quick_feedback();
}

void lcd_knob_update() {
	if (lcd_backlight_wake_trigger) {
		lcd_backlight_wake_trigger = false;
		backlight_wake();
		bool did_rotate = false;
		ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
			if (abs(lcd_encoder_diff) >= ENCODER_PULSES_PER_STEP) {
				lcd_encoder += lcd_encoder_diff / ENCODER_PULSES_PER_STEP;
				lcd_encoder_diff %= ENCODER_PULSES_PER_STEP;
				did_rotate = true;
			}
			else {
				// Get lcd_encoder_diff in sync with the encoder hard steps.
				// We assume that a click happens only when the knob is rotated into a stable position
				lcd_encoder_diff = 0;
			}
		}
		Sound_MakeSound(did_rotate ? e_SOUND_TYPE_EncoderMove : e_SOUND_TYPE_ButtonEcho);

		if (lcd_draw_update == 0) {
			// Update LCD rendering at minimum
			lcd_draw_update = 1;
		}
	}
}

void lcd_update(uint8_t lcdDrawUpdateOverride)
{
	if (lcd_draw_update < lcdDrawUpdateOverride)
		lcd_draw_update = lcdDrawUpdateOverride;

	if (!lcd_update_enabled) return;

	if (lcd_lcdupdate_func)
		lcd_lcdupdate_func();
}

void lcd_update_enable(uint8_t enabled)
{
	// printf_P(PSTR("lcd_update_enable(%u -> %u)\n"), lcd_update_enabled, enabled);
	if (lcd_update_enabled != enabled)
	{
		lcd_update_enabled = enabled;
		if (enabled)
		{ // Reset encoder position. This is equivalent to re-entering a menu.
			lcd_encoder = 0;
			lcd_encoder_diff = 0;
			// Enabling the normal LCD update procedure.
			// Reset the timeout interval.
			lcd_timeoutToStatus.start();
			// Force the keypad update now.
			lcd_next_update_millis = _millis() - 1;
			// Full update.
			lcd_clear();
			lcd_update(2);
		} else
		{
			// Clear the LCD always, or let it to the caller?
		}
	}
}

bool lcd_longpress_trigger = 0;

// WARNING: this function is called from the temperature ISR.
//          Only update flags, but do not perform any menu/lcd operation!
void lcd_buttons_update(void)
{
    static uint8_t lcd_long_press_active = 0;
    static uint8_t lcd_button_pressed = 0;
    if (READ(BTN_ENC) == 0)
    { //button is pressed
        if (buttonBlanking.expired_cont(BUTTON_BLANKING_TIME)) {
            buttonBlanking.start();
            safetyTimer.start();
            if ((lcd_button_pressed == 0) && (lcd_long_press_active == 0))
            {
                longPressTimer.start();
                lcd_button_pressed = 1;
            }
            else if (longPressTimer.expired(LONG_PRESS_TIME))
            {
                lcd_long_press_active = 1;
                lcd_longpress_trigger = 1;
            }
        }
    }
    else
    { //button not pressed
        if (lcd_button_pressed)
        { //button was released
            lcd_button_pressed = 0; // Reset to prevent double triggering
            if (!lcd_long_press_active)
            { //button released before long press gets activated
                lcd_click_trigger = 1; // This flag is reset when the event is consumed
            }
            lcd_backlight_wake_trigger = true; // flag event, knob pressed
            lcd_long_press_active = 0;
        }
    }

    //manage encoder rotation
	static const int8_t encrot_table[] PROGMEM = {
		0, -1, 1, 2,
		1, 0, 2, -1,
		-1, -2, 0, 1,
		-2, 1, -1, 0,
	};

	static uint8_t enc_bits_old = 0;
	uint8_t enc_bits = 0;
    if (!READ(BTN_EN1)) enc_bits |= _BV(0);
    if (!READ(BTN_EN2)) enc_bits |= _BV(1);

	if (enc_bits != enc_bits_old)
    {
		int8_t newDiff = pgm_read_byte(&encrot_table[(enc_bits_old << 2) | enc_bits]);
		lcd_encoder_diff += newDiff;

        if (abs(lcd_encoder_diff) >= ENCODER_PULSES_PER_STEP) {
            lcd_backlight_wake_trigger = true; // flag event, knob rotated
        }
        enc_bits_old = enc_bits;
    }
}


////////////////////////////////////////////////////////////////////////////////
// Custom character data

// #define DEBUG_CUSTOM_CHARACTERS

static void lcd_print_custom(uint8_t c) {
	uint8_t charToSend = pgm_read_byte(&Font[c - 0x80].alternate); // in case no empty slot is found, use the alternate character.
	int8_t slotToUse = -1;

	for (uint8_t i = 0; i < 8; i++) {
		// first check if we already have the character in the lcd memory
		if ((lcd_custom_characters[i] & 0x7F) == (c & 0x7F)) {
			lcd_custom_characters[i] = c; // mark the custom character as used
			charToSend = i; // send the found custom character id
#ifdef DEBUG_CUSTOM_CHARACTERS
			printf_P(PSTR("found char %02x at slot %u\n"), c, i);
#endif // DEBUG_CUSTOM_CHARACTERS
			goto sendChar;
		} else if (lcd_custom_characters[i] == 0x7F) { //found an empty slot. create a new custom character and send it
			lcd_custom_characters[i] = c; // mark the custom character as used
			slotToUse = i;
			goto createChar;
		} else if (!(lcd_custom_characters[i] & 0x80)) { // found potentially unused slot. Remember it in case it's needed
			slotToUse = i;
		}
	}

	// If this point was reached, then there is no empty slot available.
	// If there exists any potentially unused slot, then use that one instead.
	// Otherwise, use the alternate form of the character.
	if (slotToUse < 0) {
#ifdef DEBUG_CUSTOM_CHARACTERS
		printf_P(PSTR("used alternate for char %02x\n"), c);
#endif // DEBUG_CUSTOM_CHARACTERS
		goto sendChar;
	}

#ifdef DEBUG_CUSTOM_CHARACTERS
	printf_P(PSTR("replaced char %02x at slot %u\n"), lcd_custom_characters[slotToUse], slotToUse);
#endif // DEBUG_CUSTOM_CHARACTERS

createChar:
	charToSend = slotToUse;
	lcd_createChar_P(slotToUse, &Font[c - 0x80]);
#ifdef DEBUG_CUSTOM_CHARACTERS
	printf_P(PSTR("created char %02x at slot %u\n"), c, slotToUse);
#endif // DEBUG_CUSTOM_CHARACTERS

sendChar:
	lcd_send(charToSend, HIGH);
	lcd_ddram_address++; // no need for preventing ddram overflow
}

static void lcd_invalidate_custom_characters() {
	memset(lcd_custom_characters, 0x7F, sizeof(lcd_custom_characters));
}

void lcd_frame_start() {
	// check all custom characters and discard unused ones
	for (uint8_t i = 0; i < 8; i++) {
		uint8_t c = lcd_custom_characters[i];
		if (c == 0x7F) { //slot empty
			continue;
		}
		else if (c & 0x80) { //slot was used on the last frame update, mark it as potentially unused this time
			lcd_custom_characters[i] = c & 0x7F;
		}
		else { //character is no longer used (or invalid?), mark it as unused
#ifdef DEBUG_CUSTOM_CHARACTERS
			printf_P(PSTR("discarded char %02x at slot %u\n"), c, i);
#endif // DEBUG_CUSTOM_CHARACTERS
			lcd_custom_characters[i] = 0x7F;
		}

	}

#ifdef DEBUG_CUSTOM_CHARACTERS
	printf_P(PSTR("frame start:"));
	for (uint8_t i = 0; i < 8; i++) {
		printf_P(PSTR(" %02x"), lcd_custom_characters[i]);
	}
	printf_P(PSTR("\n"));
#endif // DEBUG_CUSTOM_CHARACTERS
}
