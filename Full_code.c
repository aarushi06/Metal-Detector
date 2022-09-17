//  square.c: Uses timer 2 interrupt to generate a square wave in pin
//  P2.0 and a 75% duty cycle wave in pin P2.1
//  Copyright (c) 2010-2018 Jesus Calvino-Fraga
//  ~C51~

#include <stdio.h>
#include <stdlib.h>
#include <EFM8LB1.h>

// ~C51~  

#define LCD_RS P2_6
// #define LCD_RW Px_x // Not used in this code.  Connect to GND
#define LCD_E  P2_5
#define LCD_D4 P2_4
#define LCD_D5 P2_3
#define LCD_D6 P2_2
#define LCD_D7 P2_1


//#define TIMER_4_FREQ 5000L


#define CHARS_PER_LINE 16

#define SYSCLK 72000000L
#define BAUDRATE 115200L
#define OUT0 P0_1 

unsigned char overflow_count;
volatile unsigned char pwm_count = 0;
volatile unsigned int pwm_numb = 65;

char _c51_external_startup(void)
{
	// Disable Watchdog with key sequence
	SFRPAGE = 0x00;
	WDTCN = 0xDE; //First key
	WDTCN = 0xAD; //Second key

	VDM0CN = 0x80;       // enable VDD monitor
	RSTSRC = 0x02 | 0x04;  // Enable reset on missing clock detector and VDD

#if (SYSCLK == 48000000L)	
	SFRPAGE = 0x10;
	PFE0CN = 0x10; // SYSCLK < 50 MHz.
	SFRPAGE = 0x00;
#elif (SYSCLK == 72000000L)
	SFRPAGE = 0x10;
	PFE0CN = 0x20; // SYSCLK < 75 MHz.
	SFRPAGE = 0x00;
#endif

#if (SYSCLK == 12250000L)
	CLKSEL = 0x10;
	CLKSEL = 0x10;
	while ((CLKSEL & 0x80) == 0);
#elif (SYSCLK == 24500000L)
	CLKSEL = 0x00;
	CLKSEL = 0x00;
	while ((CLKSEL & 0x80) == 0);
#elif (SYSCLK == 48000000L)	
	// Before setting clock to 48 MHz, must transition to 24.5 MHz first
	CLKSEL = 0x00;
	CLKSEL = 0x00;
	while ((CLKSEL & 0x80) == 0);
	CLKSEL = 0x07;
	CLKSEL = 0x07;
	while ((CLKSEL & 0x80) == 0);
#elif (SYSCLK == 72000000L)
	// Before setting clock to 72 MHz, must transition to 24.5 MHz first
	CLKSEL = 0x00;
	CLKSEL = 0x00;
	while ((CLKSEL & 0x80) == 0);
	CLKSEL = 0x03;
	CLKSEL = 0x03;
	while ((CLKSEL & 0x80) == 0);
#else
#error SYSCLK must be either 12250000L, 24500000L, 48000000L, or 72000000L
#endif

	P0MDOUT |= 0x10; // Enable UART0 TX as push-pull output
	XBR0 = 0x01; // Enable UART0 on P0.4(TX) and P0.5(RX)                     
	XBR1 = 0X10;
	XBR2 = 0x40; // Enable crossbar and weak pull-ups

	// Configure Uart 0
#if (((SYSCLK/BAUDRATE)/(2L*12L))>0xFFL)
#error Timer 0 reload value is incorrect because (SYSCLK/BAUDRATE)/(2L*12L) > 0xFF
#endif
	SCON0 = 0x10;
	CKCON0 |= 0b_0000_0000;
	TH1 = 0x100 - ((SYSCLK / BAUDRATE) / (2L * 12L));
	TL1 = TH1;      // Init Timer1
	TMOD &= ~0xf0;  // TMOD: timer 1 in 8-bit auto-reload
	TMOD |= 0x20;
	TR1 = 1; // START Timer1
	TI = 1;  // Indicate TX0 ready

	// Initialize timer 2 for periodic interrupts
	TMR2CN0 = 0x00;   // Stop Timer2; Clear TF2;
	CKCON0 |= 0b_0001_0000; // Timer 2 uses the system clock
	TMR2RL = (0x10000L - (SYSCLK / 10000L)); // Initialize reload value
	TMR2 = 0xffff;   // Set to reload immediately
	ET2 = 1;         // Enable Timer2 interrupts
	TR2 = 1;         // Start Timer2 (TMR2CN is bit addressable)

	EA = 1; // Enable interrupts

	return 0;
}
// Uses Timer3 to delay <us> micro-seconds. 


void waitms(unsigned int ms)
{
	unsigned int j;
	for (j = ms; j != 0; j--)
	{
		Timer3us(250);
		Timer3us(250);
		Timer3us(250);
		Timer3us(250);
	}
}

void Timer3us(unsigned char us)
{
	unsigned char i;               // usec counter

	// The input for Timer 3 is selected as SYSCLK by setting T3ML (bit 6) of CKCON0:
	CKCON0 |= 0b_0100_0000;

	TMR3RL = (-(SYSCLK) / 1000000L); // Set Timer3 to overflow in 1us.
	TMR3 = TMR3RL;                 // Initialize Timer3 for first overflow

	TMR3CN0 = 0x04;                 // Sart Timer3 and clear overflow flag
	for (i = 0; i < us; i++)       // Count <us> overflows
	{
		while (!(TMR3CN0 & 0x80));  // Wait for overflow
		TMR3CN0 &= ~(0x80);         // Clear overflow indicator
		if (TF0) {
			TF0 = 0;
			overflow_count++;
		}
	}
	TMR3CN0 = 0;                   // Stop Timer3 and clear overflow flag
}
void Timer2_ISR(void) interrupt INTERRUPT_TIMER2
{
	TF2H = 0; // Clear Timer2 interrupt flag
	pwm_count++;
	if (pwm_count > 100) pwm_count = 0;
	OUT0 = pwm_count > pwm_numb ? 0 : 1;
}

void TIMER0_Init(void)
{
	TMOD &= 0b_1111_0000; // Set the bits of Timer/Counter 0 to zero
	TMOD |= 0b_0000_0101; // Timer/Counter 0 used as a 16-bit counter
	TR0 = 0; // Stop Timer/Counter 0
}

void LCD_pulse(void)
{
	LCD_E = 1;
	Timer3us(40);
	LCD_E = 0;
}

void LCD_byte(unsigned char x)
{
	// The accumulator in the C8051Fxxx is bit addressable!
	ACC = x; //Send high nible
	LCD_D7 = ACC_7;
	LCD_D6 = ACC_6;
	LCD_D5 = ACC_5;
	LCD_D4 = ACC_4;
	LCD_pulse();
	Timer3us(40);
	ACC = x; //Send low nible
	LCD_D7 = ACC_3;
	LCD_D6 = ACC_2;
	LCD_D5 = ACC_1;
	LCD_D4 = ACC_0;
	LCD_pulse();
}

void WriteData(unsigned char x)
{
	LCD_RS = 1;
	LCD_byte(x);
	waitms(2);
}

void WriteCommand(unsigned char x)
{
	LCD_RS = 0;
	LCD_byte(x);
	waitms(5);
}

void LCD_4BIT(void)
{
	LCD_E = 0; // Resting state of LCD's enable is zero
	//LCD_RW=0; // We are only writing to the LCD in this program
	waitms(20);
	// First make sure the LCD is in 8-bit mode and then change to 4-bit mode
	WriteCommand(0x33);
	WriteCommand(0x33);
	WriteCommand(0x32); // Change to 4-bit mode

	// Configure the LCD
	WriteCommand(0x28);
	WriteCommand(0x0c);
	WriteCommand(0x01); // Clear screen command (takes some time)
	waitms(20); // Wait for clear screen command to finsih.
}

void LCDprint(char* string, unsigned char line, bit clear)
{
	int j;

	WriteCommand(line == 2 ? 0xc0 : 0x80);
	waitms(5);
	for (j = 0; string[j] != 0; j++)	WriteData(string[j]);// Write the message
	if (clear) for (; j < CHARS_PER_LINE; j++) WriteData(' '); // Clear the rest of the line
}

int getsn(char* buff, int len)
{
	int j;
	char c;

	for (j = 0; j < (len - 1); j++)
	{
		c = getchar();
		if ((c == '\n') || (c == '\r'))
		{
			buff[j] = 0;
			return j;
		}
		else
		{
			buff[j] = c;
		}
	}
	buff[j] = 0;
	return len;
}

void main(void)
{
	unsigned long F;
	TIMER0_Init();
	LCD_4BIT();
	waitms(1000);

	printf("\x1b[2J"); // Clear screen using ANSI escape sequence.
	printf("EFM8 Frequency measurement using Timer/Counter 0.\n"
		"File: %s\n"
		"Compiled: %s, %s\n\n",
		__FILE__, __DATE__, __TIME__);

	while (1)
	{
		TL0 = 0;
		TH0 = 0;
		overflow_count = 0;
		TF0 = 0;
		TR0 = 1; // Start Timer/Counter 0
		waitms(100);
		TR0 = 0; // Stop Timer/Counter 0
		F = overflow_count * 0x10000L + TH0 * 0x100L + TL0;


		// waitms(400);
		if (F > 2420) {

			TR2 = 1;
			LCDprint("Metal detected", 1, 1);

			if (F > 2425 && F < 2435) {
				LCDprint("Distance~1cm", 2, 1);
			}

			else if (F > 2435) {
				LCDprint("Distance~0cm", 2, 1);

				if (F > 2425 && F < 2440) {
					waitms(700);
					LCDprint("Gold", 1, 1);
					waitms(700);
				}

				if (F < 2490 && F>2465) {
					waitms(700);
					LCDprint("Cents", 1, 1);
					waitms(700);
				}

				if (F > 2490 && F < 2520) {
					waitms(700);
					LCDprint("Dollar", 1, 1);
					waitms(700);
				}

				if (F > 2520 && F < 2600) {
					waitms(700);
					LCDprint("Aluminium", 1, 1);
					waitms(700);
				}

				if (F > 2610) {
					waitms(700);
					LCDprint("Big metal", 1, 1);
					waitms(700);
				}
			}
		}

		else {
			OUT0 = 0;
			TR2 = 0;
			LCDprint("Not detected", 1, 1);
			LCDprint("Distance>3cm", 2, 1);
		}

		printf("\rf=%luHz", F);
		printf("\x1b[0K"); // ANSI: Clear from cursor to end of line.
	}
}
