/*

ECE 473 - Microcontrollers
Alarm Code for TekBots mega128 board
Nick McComb | www.nickmccomb.net
Written Fall 2016

*/
//  HARDWARE SETUP:
//  PORTA is connected to the segments of the LED display. and to the pushbuttons.
//  PORTA.0 corresponds to segment a, PORTA.1 corresponds to segement b, etc.
//  PORTB bits 4-6 go to a,b,c inputs of the 74HC138.
//  PORTB bit 7 goes to the PWM transistor base.

//  BARGRAPH SETUP:
//  regclk   PORTB bit 0 (SS_n)
//  srclk    PORTB bit 1 (SCLK)
//  sdin     PORTB bit 2 (MOSI)
//  oe_n     GND

//  ENCODER SETUP
//  sck         PORTB bit 1
//  ENC_CLK_INH PORTE bit 6
//  ENC_SH/LD   PORTE bit 7


//TODO STUFF
/*

Implement DST


Moonshots:
LED spinny blinky while looking for satellites

*/



//#define F_CPU 16000000 // cpu speed in hertz 
#define TRUE 1
#define FALSE 0
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stddef.h>
#include <string.h>
#include "uart_functions.h"
#include "lm73_functions_skel.h"
#include "twi_master.h"
#include "hd44780.h"
#include "si4734.h"

//Program controls
//#define LEADING_0  //Whether or not you want leading zeros
#define USE_GPS_TIME   //Whether or not you want to use GPS Time (if available)


//Segment pin definitions
#define SEG_A  0x01
#define SEG_B  0x02
#define SEG_C  0x04
#define SEG_D  0x08
#define SEG_E  0x10
#define SEG_F  0x20
#define SEG_G  0x40
#define SEG_DP 0x80
#define SEG_OFF 0xFF

//PORTB Definitions
#define DIG_SEL_1 0x10
#define DIG_SEL_2 0x20
#define DIG_SEL_3 0x40
#define PWM_CTRL 0x80

//PORTD Definitions
#define AUDIO_OUT 0x10

//holds data to be sent to the segments. logic zero turns segment on
uint8_t segment_data[5];

//decimal to 7-segment LED display encodings, logic "0" turns on segment
uint8_t dec_to_7seg[12];

//Globals
uint16_t counter = 0;
//TODO: Add rest of globals

volatile char    rxBuffer[64];
volatile uint8_t rxBufferCnt = 0;
volatile char    finalBuffer[64];
volatile uint8_t inputFlag = 0;
volatile char remoteTemp[6];

//Temperature data
extern uint8_t lm73_wr_buf[2];
extern uint8_t lm73_rd_buf[2];
uint16_t lm73_data;
uint16_t lm73_precision;
char tempCelString[7];

//Function Prototypes
void inline incrementCounter( void );
void inline decrementCounter( void );
void setDigit( uint8_t targetDigit );
void configureIO( void );
void configureTimers( void );
void configureSPI( void );
void configureADC( void );
void configureRadio( void );
void inline setSegment( uint16_t targetOutput );
void inline clearSegment( void );
void        processButtonPress( void );
void        processCounterOutput( void );
void inline processAlarm( void );
void inline processOutputBrightness( void );
void inline checkButtons( void );
void inline updateSPI( void );
void processEncoders( void );

//TODO: Move rest of function decs up here

//TODO: Remove all of this shit
uint8_t randoTest = 0;
uint8_t inc2Bool = 0x00;
uint8_t inc4Bool = 0x00;

//Digit control low-level code
void inline SET_DIGIT_DOT(void)   {PORTB |= DIG_SEL_2; PORTB = PORTB & ~(DIG_SEL_1 | DIG_SEL_3);} //Untested, TODO: test!
void inline SET_DIGIT_ONE(void)   {PORTB |= DIG_SEL_3; PORTB = PORTB & ~(DIG_SEL_1 | DIG_SEL_2);}
void inline SET_DIGIT_TWO(void)   {PORTB |= DIG_SEL_1 | DIG_SEL_2; PORTB = PORTB & ~(DIG_SEL_3);}
void inline SET_DIGIT_THREE(void) {PORTB |= DIG_SEL_1; PORTB = PORTB & ~(DIG_SEL_2 | DIG_SEL_3);}
void inline SET_DIGIT_FOUR(void)  {PORTB = PORTB & ~(DIG_SEL_1 | DIG_SEL_2 | DIG_SEL_3);}

//Tri-State Buffer Enable
void inline ENABLE_BUFFER(void)   {PORTB |= DIG_SEL_1 | DIG_SEL_2 | DIG_SEL_3;}

//Port A Control
void inline ENABLE_LED_CONTROL(void) {DDRA = 0xFF; SET_DIGIT_THREE(); PORTB |= DIG_SEL_3;} //Enables PORTA as an output, while also ensuring the Tri-state buffer is disabled by selecting digit one
void inline ENABLE_BUTTON_READ(void) {PORTA = 0xFF; DDRA = 0x00;}  //Enable inputs/pullups on PORTA

void inline ENC_CLK_ENABLE(void)  {PORTE &= ~(0x40);}
void inline ENC_CLK_DISABLE(void) {PORTE |=   0x40 ;}

void inline ENC_PARALLEL_ENABLE(void)  {PORTE &= ~(0x10);}
void inline ENC_PARALLEL_DISABLE(void) {PORTE |=   0x10 ;}


//Parsed commands from the encoders (parsed to one call per detent)
void inline ENC_L_COUNTUP(void)  ;
void inline ENC_L_COUNTDOWN(void);
void inline ENC_R_COUNTUP(void)  ;
void inline ENC_R_COUNTDOWN(void);

//NOP delay
#define NOP() do { __asm__ __volatile__ ("nop"); } while (0)

//Global Variables
uint32_t volatile output[5]; //Note, this is zero indexed for digits!!! The 0 index is for the colon
int16_t  lastEntered = 0;
int16_t  debounceCounter = 0;
uint8_t  unpressed = 1;
uint8_t  lastEncoderValue = 0x13;
uint8_t  upToDateEncoderValue = 0;  //Holds whether the encoder value is a newly measured value
uint8_t  bargraphOutput = 0;
uint8_t  secondsCounter = 0; //When counted is 255, a second has past
uint8_t  volatile quickToggle = 0;
uint16_t volatile lastADCread = 217;  //Last ADC reading, default to a realistic value 
#define P_SET_DEL 20

//Audio shortcuts
#define ALARM_VOLUME 60//%
//Volume control (OCR3A needs to range from 85 to 430 to be within working parameters),
//but we actually want the alarm to turn off, so we go a bit below that: 0 to 430.
void inline SET_VOLUME(uint8_t volumePercentage){ OCR3A = volumePercentage * 4.3 + 0; }
void inline SET_HZ(uint16_t targetHz) {OCR1A = targetHz * 50;}
uint16_t musicCounter = 0;
#define NUM_MUSIC_NOTES 15
//Supposed to be the super mario theme... credit: www.mikrotik.com/wiki/Super_Mario_Theme
uint16_t music[25] = {660, 660, 660, 510, 660, 770, 380, 510, 380, 320, 440, 480, 450, 430, 380, 660, 760, 860, 700, 760, 660, 520, 580, 480};
uint8_t volumeOutputTimer = 0;  //Choses when to display volume on the bar graph
#define VOLUME_OUTPUT_DELAY 3 //seconds

//LED Mangement
uint8_t volatile global_targetDigit = 0;

//time management
uint8_t  seconds = 0;
uint8_t  minutes = 30;
uint8_t  hours   = 0;  //TODO change start time

//alarm management (alarm is in 24 hours)
uint8_t  alarmMinutes = 20;
uint8_t  alarmHours   = 0;
uint8_t  currentlyAlarming = 0;
uint16_t snoozeCount = 0;
#define SNOOZE_SECONDS 10 //60*10 //10 minutes
enum alarmMode_t {BEEP, RADIO};
uint8_t  alarmMode = BEEP; //Set default alarm mode to beep



//Digit points
uint8_t volatile dot[5] = {0,0,0,0,0};
uint8_t volatile upperDot = 0;
uint8_t volatile colon = 0;

//Brightness management
uint8_t  lux[10] = { 0x01, 0x20, 0x70, 0xA0, 0xC0, 0xD0, 0xD8, 0xDF, 0xE0, 0xEF };
uint8_t  brightnessControl = 0;
void inline setLEDBrightness(uint8_t targetBrightness){OCR2 = targetBrightness;} //0 to 255 control, lower is brigher
void inline START_ADC_READ(void){ADCSRA |= (1<<ADSC);}  //Starts the read from the ADC (takes ~108uS)
void inline FINISH_ADC_READ(void){while(bit_is_clear(ADCSRA, ADIF)); ADCSRA |= (1<<ADIF); lastADCread = ADC;}

//#define DEBUG_LIGHT_SENSE_ADC  //Uncomment if you want the ADC count outputted on the LCD

//LCD Output
char lcdOutput[40];
uint8_t lcdCounter = 0;
extern char lcd_string_array[32];
char lcd_final[32];

//Editing settings
uint16_t volatile settings = 0;
enum settings_t {SET_MIN = 0x01, SET_HR = 0x02, TIME24 = 0x04, ALARM_ARMED = 0x08};

//Radio globals

extern enum radio_band{FM, AM, SW};
extern volatile uint8_t STC_interrupt;

volatile enum radio_band current_radio_band = FM;

uint8_t currentlyRadio = 0;  //1 if the radio is "ON"
void inline RADIO_ON(void)  {set_property(0x4000, 0x003F); currentlyRadio = TRUE;}
void inline RADIO_OFF(void) {set_property(0x4000, 0x0000); currentlyRadio = FALSE;}

uint8_t radioVolume = 60;  //Current desired volume from 0% to 100%, default to half

uint8_t radioOutputTimer = 0;
#define RADIO_OUTPUT_DELAY 3 //seconds


extern uint8_t si4734_tune_status_buf[8];

uint16_t eeprom_fm_freq;
uint16_t eeprom_am_freq;
uint16_t eeprom_sw_freq;
uint8_t  eeprom_volume;

uint16_t desired_fm_freq = 10630;  //The system requests an updated frequency by changing this variable
uint16_t current_fm_freq = 10630; //10630; //9910 // 10630;
uint16_t current_am_freq;
uint16_t current_sw_freq;
uint8_t  current_volume;

//Used in debug mode for UART1
//char uart1_tx_buf[40];      //holds string to send to crt
//char uart1_rx_buf[40];      //holds string that recieves data from uart

//This was a function used by me to async update the LCD, however I ended up
//using a modified version of the code that Traylor provided on his GitHub
void inline processLCD(){

    //Output to LCD
    ++lcdCounter;
    if(lcdCounter == 16){
      //lcdCounter = 0;
      line2_col1();
      char2lcd('7');
      char2lcd('7');
      //cursor_home();
    }
    else if(lcdCounter == 33){
      cursor_home();//line1_col1();
      lcdCounter = 0;
    }
    

    char2lcd(lcdOutput[lcdCounter]);
}

//Configures the device IO (port directions and intializes some outputs)
void configureIO( void ){

  ENABLE_LED_CONTROL(); 

  //DDRA = 0xFF; //Initialize DDRA as if we want to control the LEDs
  DDRB  = 0xF0; //Upper nibble of the B register is for controlling the decoder / PWM Transistor

  DDRB |= 0x07;  //Setup the SPI pins as outputs

  //Setup ADC input
  DDRF  &= ~0x01;  //Setup pin 0 as an input (just in case)
  PORTF &= ~0x01;  //Pullups must be off     (just in case)

  //Audio output pin
  DDRD |= AUDIO_OUT;    //Pin 4 as an output

  //Volume control pin
  DDRE |= 0x08;

  uint8_t i;

  //Init output to 0
  for(i = 0; i < 5; ++i){
    output[i] = 0;
  }

  //  Radio configuration
  //PORTE bit 2 is active high reset for Radio
  DDRE  |= 0x04;
  PORTE |= 0x04; //Radio reset is on at powerup (active high)
  //Enable interrupt for radio
  EICRB |= (1<<ISC71) | (1<<ISC70);
  EIMSK |= (1<<INT7);
  

  //Enable encoders
//  DDRE |= 0xC0;  //Enable Clk inhibit pin and async pin as outputs
  //Enable clk inhibit pin and async pin as outputs
  DDRE |= 0x50;
  ENC_CLK_DISABLE();
  ENC_PARALLEL_ENABLE();


//  DDRF |=   DEBUG_PIN; //Enable PORTF PINX as a debug output
//  DEBUG_LOW();  //Set the pin low to start
}

//******************************************************************************
// External interrupt 7 is on Port E bit 7. The interrupt is triggered on the
// rising edge of Port E bit 7.  The i/o clock must be running to detect the
// edge (not asynchronouslly triggered)
//******************************************************************************
ISR(INT7_vect){STC_interrupt = TRUE;}
/***********************************************************************/

//Configures all timer/counters on the device
void configureTimers( void ){
  ////Polling loop
  //Enable TCC0 to be clocked from an external osc,
  ASSR |= (1<<AS0);
  //Enable coutner in normal mode with no prescaler
  TCCR0 = (0<<CS02) | (0<<CS01) | (1<<CS00);

  //Wait for all ascynch warning bits to clear
  while(bit_is_set(ASSR, TCN0UB));
  while(bit_is_set(ASSR, OCR0UB));
  while(bit_is_set(ASSR, TCR0UB));

  //Enable overflow interrupts for T/C 0
  TIMSK |= (1<<TOIE0);

  ////Sound Generation (TCNT1)  (currently desire between 200 and 600 and 1500 Hz
  //CTC mode
  TCCR1A |= 0x00;
  //CTC mode, no prescaler
  TCCR1B |= (1<<WGM12) | (1<<CS10);
  //No forced compare
  TCCR1C |= 0x00;
  //Initial compare value
  OCR1A   = 20000; //About 400Hz?
  //Enable interrupt
  TIMSK  |= (1<<OCIE1A);

  ////LED Dimming Control (TCNT2)
  //Enable fast PWM, non-inverting output mode
  //64 prescaler (goal is 967Hz)
  TCCR2 = (1<<WGM21) | (1<<WGM20) | (1<<COM21) | (1<<CS21) | (1<<CS20);
  //Default PWM value of half brightness
  OCR2 = 0xFF / 7;

  ////Volume control (TCNT3)
  //9bit Fast PWM Mode, non-inverting output on OC3A
  //8 prescaler, frequency is 3.906KHz
  TCCR3A |= (1<<COM3A1) | (1<<WGM31);
  TCCR3B |= (1<<WGM32) | (1<<CS31);
  //No forced compare
  TCCR3C |= 0x00;

  //Initialize with a 50% duty cycle
  OCR3A = 512/2;
  
  SET_VOLUME(20);
  //OCR3A = 200;

  //Eat a potato
}

//Timer 0 overflow vector
//Polls the buttons / interfaces with SPI
//Counts seconds
//Updates values
//This ISR is invoked every 255 clock cycles of the 32.768kHz oscillator (~128Hz)
ISR(TIMER0_OVF_vect){
  //Executed every second
  if(++secondsCounter == 128){//128){  //Make faster using 16
    seconds += 1;
    if(seconds == 60){
      seconds = 0;
      minutes += 1;
      if(minutes == 60){
        minutes = 0;
        hours += 1;
	if(hours >= 24)
	  hours = 0;
      }
    }

    if(snoozeCount > 0){//If a snooze has been activated
      ++snoozeCount;
    }

    if(radioOutputTimer){  //is n/e 0
      radioOutputTimer -= 1;
    }
    if(volumeOutputTimer){
      volumeOutputTimer -= 1;
    }

  }
 //Exectued 128Hz
  if (secondsCounter % 1 == 0){
//DEBUG_HIGH();
    //Check the buttons for input
//    checkButtons();
    NOP();
    NOP();
    
    //Reset the outputs to be what they should be
//    setDigit(global_targetDigit);

    processOutputBrightness();
    START_ADC_READ();

    //processEncoders();
  }

  //Executed 4Hz
  if(secondsCounter % 32 == 0){  //Fast cycle

    //START_ADC_READ(); 

    quickToggle ^= 1;
    
    ++musicCounter;

    if(musicCounter >= NUM_MUSIC_NOTES)
//    if(musicCounter > 2)
      musicCounter = 0;
  }

  if(secondsCounter == 128)
    secondsCounter = 0;
}


//Audio generation interrupt
ISR(TIMER1_COMPA_vect){
   //Toggle audio output bit (only if the alarm is happening, and we are in beep mode)
   if(currentlyAlarming && alarmMode == BEEP)
     PORTD ^= AUDIO_OUT; 
}

//Setup SPI on the interface
void configureSPI( void ){
  //Configure SPI
  //Master mode, clk low on idle, leading edge sample
  SPCR = (1 << SPE) | (1 << MSTR) | (0 << CPOL) | (0 << CPHA);   

}

//Configures the ADC
void configureADC( void ){
  //Configure the MUX for single-ended input on PORTF pin 0, right adjusted, 10 bits
  ADMUX  = (1<<REFS0);

  //Enable the ADC, don't start yet, single shot mode
  //division factor is 128 (125khz)
  //enable interrupts on conversion
  ADCSRA = (1<<ADEN) | (1<<ADPS0) | (1<<ADPS1) | (1<<ADPS2) | (1<<ADIE);
}


//Stores the result of the ADC conversion
ISR(ADC_vect){
  lastADCread = ADC; 
}


//Setup the si4734 radio
void configureRadio( void ){

  //hardware reset of Si4734
  PORTE &= ~(1<<PE7); //int2 initially low to sense TWI mode
  DDRE  |= 0x80;      //turn on Port E bit 7 to drive it low
  PORTE |=  (1<<PE2); //hardware reset Si4734 
  _delay_us(200);     //hold for 200us, 100us by spec         
  PORTE &= ~(1<<PE2); //release reset 
  _delay_us(30);      //5us required because of my slow I2C translators I suspect
  //Si code in "low" has 30us delay...no explaination
  DDRE  &= ~(0x80);   //now Port E bit 7 becomes input from the radio interrupt


}
//Outputs the proper segment based on the input number
//Note: This function only currently supports 0-9 (as alphas were not needed for the assignment)
void inline setSegment( uint16_t targetOutput ){
  switch(targetOutput){
     case 0:
       PORTA = ~(SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F);
       break;
     case 1:
       PORTA = ~(SEG_B | SEG_C);
       break;
     case 2:
       PORTA = ~(SEG_A | SEG_B | SEG_D | SEG_E | SEG_G);
       break;
     case 3:
       PORTA = ~(SEG_A | SEG_B | SEG_C | SEG_D | SEG_G);  //Changed G to E
       break;
     case 4:
       PORTA = ~(SEG_B | SEG_C | SEG_F | SEG_G);
       break;
     case 5:
       PORTA = ~(SEG_A | SEG_C | SEG_D | SEG_F | SEG_G);
       break;
     case 6:
       PORTA = ~(SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G);
       break;
     case 7:
       PORTA = ~(SEG_A | SEG_B | SEG_C);
       break;
     case 8:
       PORTA = ~(SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G);
       break;
     case 9:
       PORTA = ~(SEG_A | SEG_B | SEG_C | SEG_F | SEG_G);
       break;
     case 10: //A
       break; 
     case 11: //B
       break;
     case SEG_OFF:
       clearSegment();
       break;
  }
}

//Clears the segments so nothing is being outputted on the port
void inline clearSegment( void ){
  PORTA = 0xFF;
}

//Sets the decoder to choose the appropriate transistor for the appropriate digit. 
//It also sets the appropriate segment outputs.
//NOTE: There is an inherient 100uS delay with any call of this function
void setDigit( uint8_t targetDigit ){
  clearSegment();
  _delay_us(5);
  NOP();
  NOP();

  if(!radioOutputTimer){ //Last-minute hack to totally change what the LED outputs

  switch(targetDigit){

    case 0: //colon control
      SET_DIGIT_DOT();  //Digit control
      //_delay_us(P_SET_DEL);
      NOP();
      NOP();
      
      if(colon)
	PORTA = PORTA & ~(SEG_A | SEG_B);
      else
        PORTA |= SEG_A | SEG_B; 
      
      if(upperDot)
        PORTA = PORTA & ~(SEG_C);
      else
        PORTA |= SEG_C;
      break;
    case 1:
      SET_DIGIT_ONE();
      //_delay_us(P_SET_DEL);
      NOP();
      NOP();
      if((settings & SET_HR) && quickToggle)
        clearSegment();
      //else
      //  setSegment(output[1]);
      //Check if we want to remove a leading zero
      else if(((hours > 0) && (hours < 10)) || ((hours > 12) && (hours < 22)) && !(settings & TIME24)){ //Then we want to remove 0
        clearSegment();
      }
      else
        setSegment(output[1]);
      if(dot[1])
        PORTA = PORTA & ~(SEG_DP);
      break;
    case 2:
      SET_DIGIT_TWO();
      //_delay_us(P_SET_DEL);
      NOP();
      NOP();
      if((settings & SET_HR) && quickToggle)
        clearSegment();
      else
        setSegment(output[2]);
      if(dot[2])
        PORTA = PORTA & ~(SEG_DP);
      break;
    case 3:
      SET_DIGIT_THREE();
      //_delay_us(P_SET_DEL);
      NOP();
      NOP();
      if((settings & SET_MIN) && quickToggle)
        clearSegment();
      else
        setSegment(output[3]);
      if(dot[3])
        PORTA = PORTA & ~(SEG_DP);
      break;
    case 4:
      SET_DIGIT_FOUR();
      //_delay_us(P_SET_DEL);
      NOP();
      NOP();
      if((settings & SET_MIN) && quickToggle)
        clearSegment();
      else
        setSegment(output[4]);
      if(dot[4])
        PORTA = PORTA & ~(SEG_DP);
      break;
  }
  }//end !radioOutputTimer
  else {//we want to output the radio tuning information
    
    switch(targetDigit){
      case 1:
        SET_DIGIT_ONE();
        if(desired_fm_freq > 9999){ //Then we want to output a 1
          setSegment(1);
	}
        break;
      case 2:
        SET_DIGIT_TWO();
	setSegment((desired_fm_freq / 1000) % 10);
        break;
      case 3:
        SET_DIGIT_THREE();
	setSegment((desired_fm_freq / 100) % 10);
	//Set the digit point
	PORTA = PORTA & ~(SEG_DP);
        break;
      case 4:
        SET_DIGIT_FOUR();
	setSegment((desired_fm_freq / 10) % 10);
        break;
    }
  }
}

//This function is called when a button is pressed, and handles processing the press, as well as
//changing tne numbers that are to be outputted.
void processButtonPress( void ){
  
  uint8_t temp = 0xFF - PINA;

  switch(temp){
    case 0x01: //Add to alarm minutes
      if(settings & SET_MIN){
        minutes = (minutes + 1) % 60;
	seconds = 0;
      }
      else{
        alarmMinutes += 5;
        if(alarmMinutes >= 60){
          alarmMinutes = 0;
  	  ++alarmHours;
  	  if(alarmHours >= 24)
  	    alarmHours = 0;
        }
      }
      break;
    case 0x02: //Add to alarm hours
      if(settings & SET_HR){
        hours = (hours + 1) % 24;
	seconds = 0;
      }
      else {
        ++alarmHours;
        if(alarmHours >= 24)
          alarmHours = 0;
      }
      break;
    case 0x04: //Switch alarm mode
      if(settings & ALARM_ARMED){
        if(alarmMode == BEEP)
	  alarmMode = RADIO;
	else if(alarmMode == RADIO)
	  alarmMode = BEEP;
      }
      break;
    case 0x08: //Snooze alarm
      if(currentlyAlarming){
        currentlyAlarming = 0; //Kill the alarm
        settings &= ~(ALARM_ARMED); //Disarm the alarm
        snoozeCount = 1;       //This starts the snooze counter
      }
      break;
    case 0x10: //Arm alarm button
      if(currentlyAlarming){      //Kills the alarm if currently alarming
        currentlyAlarming = 0;
	snoozeCount = 0;          //We also want to get rid of any snooze
	//if(currentlyRadio)
	  //RADIO_OFF();
      }
      else
        settings ^= ALARM_ARMED;  //Otherwise, toggle alarm
      break;
    case 0x20: //Set military time button or turn on radio
      if((settings & SET_HR) || (settings & SET_MIN)){  //If we are changing the time, then change 12/24 hr modes
        settings ^= TIME24;
        if(settings & TIME24)
          upperDot = TRUE;
        else
          upperDot = FALSE;
      } //Otherwise, turn on the radio
      else {
        if(currentlyRadio)
          RADIO_OFF();
        else
          RADIO_ON();  
      }
      /*
      settings ^= TIME24;
      if(settings & TIME24)
        upperDot = TRUE;
      else
        upperDot = FALSE;
      */
      break;
    case 0x40: //Toggle Set minutes
      settings ^= SET_MIN;
      settings &= ~(SET_HR);
      break;
    case 0x80:
      settings ^= SET_HR;
      settings &= ~(SET_MIN);
      break;
  }

}  
 
//This function processess the output of the counter variable.
//It checks for overflow conditions, and then calculates the numbers to be outputted on each 7 segment digit
void processCounterOutput( void ){
  //We want to check for overflow/underflow here
  if(counter < 10000 && counter > 1023) //Check for simple overflow
    counter = (counter % 1024) + 1;
 
  if(counter > 10000) //Check for overflow, because variable is a uint, it will wrap around
    counter = 1023;

  //We want to calculate the presses here, and not every time, as they can take some time,
  //and the user will be more tolerable of a slight sutter at a button press, but not every
  //execution cycle. (In theory. In practice, this math will be unnoticable). 
  uint16_t tempCounter = counter;
  //calculate new output values

  //Calculate output due for minutes
  //Note: Output 1 is leftmost output
  tempCounter = minutes;
  output[4] = tempCounter % 10;
  tempCounter /= 10;
  output[3] = tempCounter % 10;

  //Calculate the output due for hours
  if(settings & TIME24)  //Check if we want to output military time
    tempCounter = hours;
  else { //Otherwise, output "civilian time"
    tempCounter = hours % 12;
    if(tempCounter == 0)
      tempCounter = 12;
  }
  
  output[2] = tempCounter % 10;
  tempCounter /= 10;
  output[1] = tempCounter % 10;

  //We want to output a dot to indicate "PM" if the time is over 11 and we're not in 24 hour mode
  if((hours > 11) && !(settings & TIME24))
    dot[2] = 1;
  else
    dot[2] = 0;

  //Blink the colon for seconds
  if(seconds % 2) //If seconds are odd
    colon = FALSE;
  else
    colon = TRUE;
  

}

//This function processes everything having to do with the alarm on the clock
//This function has two main parts: detecting alarm triggers and parsing alarm output (LCD and Audio)
void inline processAlarm( void ){
  //Detecting Alarm Triggers

  //Check if the alarm is armed and the time is right...
  if(!(settings & SET_MIN) && !(settings & SET_HR) && (settings & ALARM_ARMED) && alarmHours == hours && alarmMinutes == minutes){
    settings &= ~ALARM_ARMED; //Unarm alarm
    currentlyAlarming = 1;    //Trigger alarm
  }

  //Check the snooze condition
  if(snoozeCount >= (SNOOZE_SECONDS + 1)){
    settings &= ~ALARM_ARMED;  //Disarm alarm
    currentlyAlarming = 1;
    snoozeCount = 0; //Stop the snooze count
  }


  //Detecting Alarm to determine what to output on the LCD
  if(currentlyAlarming){
    uint8_t k;
    for(k = 0; k < 16; ++k)
      lcd_string_array[k] = ' ';
    lcd_string_array[17] = 'W';
    lcd_string_array[18] = 'A';
    lcd_string_array[19] = 'K';
    lcd_string_array[20] = 'E';
    lcd_string_array[21] = ' ';
    lcd_string_array[22] = 'U';
    lcd_string_array[23] = 'P';
    lcd_string_array[24] = ' ';
    lcd_string_array[25] = ':';
    lcd_string_array[26] = ')';

  }
  else if(snoozeCount > 1){
    uint8_t k;
    for(k = 0; k < 16; ++k)
      lcd_string_array[k] = ' ';

    lcd_string_array[17] = 'Z';
    lcd_string_array[18] = 'z';
    lcd_string_array[19] = 'Z';
    lcd_string_array[20] = 'z';
    lcd_string_array[21] = 'Z';
    lcd_string_array[22] = 'z';
    lcd_string_array[23] = 'Z';
    lcd_string_array[24] = 'z';
    lcd_string_array[25] = 'Z';
    lcd_string_array[26] = 'z';

  }
  else if(settings & ALARM_ARMED){  //If no alarm, no snooze, but alarm is set, we want to output when we are going to alarm
    dot[4] = 1;
    lcd_string_array[0] = 'A';
    lcd_string_array[1] = 'L';
    lcd_string_array[2] = 'A';
    lcd_string_array[3] = 'R';
    lcd_string_array[4] = 'M';
    lcd_string_array[5] = ' ';
    lcd_string_array[6] = '@';
    lcd_string_array[7] = ' ';
    if(settings & TIME24){ //24 hour mode
      if(alarmHours == 0){
        lcd_string_array[8] = '0';
	lcd_string_array[9] = '0';
      }
      else{
        if(alarmHours < 10)
	  lcd_string_array[8] = '0';
	else
	  lcd_string_array[8] = (alarmHours / 10) + 48;
        lcd_string_array[9] = (alarmHours % 10) + 48;
      }
    }
    else{ //12 hour mode
      if(alarmHours == 0 || alarmHours == 12){
        lcd_string_array[8] = '1';
	lcd_string_array[9] = '2';
      }
      else{ //We have to do actual math
        if((alarmHours % 12) < 10)
	  lcd_string_array[8] = ' ';
	else
	  lcd_string_array[8] = ((alarmHours % 12) / 10) + 48;
	lcd_string_array[9] = ((alarmHours % 12) % 10) + 48;	
      }
    }
    lcd_string_array[10] = ':';
    //time for minutes
    if(alarmMinutes < 10)
      lcd_string_array[11] = '0';
    else
      lcd_string_array[11] = (alarmMinutes / 10) + 48;
    lcd_string_array[12]  = (alarmMinutes % 10) + 48;
    
    if(!(settings & TIME24)){ //12 hour mode
      if(alarmHours > 11){
        lcd_string_array[13] = 'p';
      }
      else{
        lcd_string_array[13] = 'a';
      }
      lcd_string_array[14] = 'm';
      
    }
    else{
        lcd_string_array[13] = ' ';
	lcd_string_array[14] = ' ';
    }
    uint8_t p;

    //Clean up the second line
    for(p = 16; p < 32; ++p)
      lcd_string_array[p] = ' ';

    //Output what mode we are in
    if(alarmMode == BEEP)
      memcpy(lcd_string_array +16, "Beep mode set", 13);
    else if(alarmMode == RADIO)
      memcpy(lcd_string_array +16, "Radio mode set", 14);
  }
  else{  //The alarm isn't armed, so we want to output that fact
    dot[4] = 0;

    memcpy(lcd_string_array, "no alarm set", 12);

    uint8_t i;
 
    for(i = 0; i < 16; ++i)
      lcd_string_array[i+12] = ' ';

    //Temperature planning
    //2nd row begins at 16
    //16, 17, 18 = ., 19, 20
    //21
    //22, 23, 24 = ., 25, 26

    //Display local temperature
    lcd_string_array[16] = 'l';
    for(i = 0; i < 5; ++i)
      lcd_string_array[i + 18] = tempCelString[i];
    //Display remote temperature
    lcd_string_array[25] = 'r';
    for(i = 0; i < 5; ++i)
      lcd_string_array[i + 27] = remoteTemp[i];
  }

  //Detecting alarm to determine what to output through the speaker
  
  if(currentlyAlarming){
    if(alarmMode == BEEP){ //Then we want to beep
      SET_HZ(music[musicCounter]);
      lcd_string_array[0] = musicCounter + 48;
    }
    else if(alarmMode == RADIO){
      if(!currentlyRadio) //If the radio isn't already on, turn it on
        RADIO_ON();
    }
  } 

/*
  if(!currentlyAlarming){
//    SET_VOLUME(60);
  }
  else {
//    SET_VOLUME(ALARM_VOLUME);
    SET_HZ(music[musicCounter]);
  }
*/  
  #ifdef DEBUG_LIGHT_SENSE_ADC
    //ADC testing code
    lcd_string_array[29] = (lastADCread / 100) + 48;
    lcd_string_array[30] = ((lastADCread / 10)%10) + 48;
    lcd_string_array[31] = (lastADCread % 10) + 48;
  #endif

}

#define MIN_BRT 220
#define MAX_BRT 0
/*
Values discission

~900 is normal ambient brightness, so this is extended to 1024

~450 is the desired target lower end, so a statment needs to be written to catch values below trhat
*/


//Processed the ADC count and adjusts the output brighness for the screen
//Calculations courtesy of: http://academics.triton.edu/faculty/mlarosa/slope.htm
void inline processOutputBrightness( void ){


  if(lastADCread < 480)
    setLEDBrightness(MIN_BRT);
  else
    setLEDBrightness((lastADCread * -.4) + 410);

  //Past values:
  //We want to set the LED brightness based upon the read ADC value
  //The calculations were derived from experimental data
  //setLEDBrightness(0xFF - (lastADCread * .227 + 27));  //Initial calibration
  //setLEDBrightness((lastADCread * -.152) + 170);

  //Testing code:
  //setLEDBrightness(MIN_BRT);
  //setLEDBrightness(230);
  //setLEDBrightness(0xFF - (150  * .227 + 27));
}

//Determine what volume we want to output
void inline processVolume( void ){

  if(currentlyAlarming){
    SET_VOLUME(80);//ALARM_VOLUME);
  }
  else if(currentlyRadio){
    SET_VOLUME(radioVolume);
  }
  else{
    SET_VOLUME(0x00);
  }

  //Sets the bar graph output according to the volume
  //NOTE: The lower two volume bars don't show up on the bar graph
  //because it's 8 digits and didn't feel like doing any more math ¯\_(ツ)_/¯
  if(volumeOutputTimer){
  if(radioVolume <= 20){
    bargraphOutput = 0;
  }
  else if (radioVolume == 30){
    bargraphOutput = 1;
  }
  else{
    bargraphOutput = pow(2, (radioVolume-20) / 10);
  }
  }

}


//This function process the radio tune, as well as the RSSI information
//It only finalizes the tune (and executes) when the user has made their
//selection (the radioOutputTimer has decreased to 0, meaning they haven't
//moved the knob in X seconds.
//The function also only reads RSSI when the radio is not being updated
//(to prevent long delays)
//The function also only outputs a 0 when the station is being changed
//Also, RSSI is only outputted when the RADIO is on (duh)
void inline processRadioTune(){

  if((desired_fm_freq != current_fm_freq) && !radioOutputTimer ){
    current_fm_freq = desired_fm_freq;
    fm_tune_freq();
  }
  else if (!radioOutputTimer && currentlyRadio && !volumeOutputTimer){
    while(twi_busy()){}
    fm_rsq_status();
    bargraphOutput = si4734_tune_status_buf[4];
  }
  else if(radioOutputTimer || (desired_fm_freq != current_fm_freq)){
    bargraphOutput = 0;
  }
  else {
    bargraphOutput = 0;
  }

}

//Checks the buttons when called, and calls a seperate processing function once the buttons have been debounced
//It will call it only once per button press, and resets upon button release.
void inline checkButtons( void ){
  ENABLE_BUTTON_READ();
  ENABLE_BUFFER();

  NOP();
  NOP();
  NOP();
  NOP();

  //Latching button debounce
  //The delay from the for loop at the beginning of this while(1) block will handle
  //most of the important debouncing delay, so we can just use a latch here.
  if(PINA != 0xFF){ //If the buttons read anything
    if(unpressed){
      processButtonPress();
      unpressed = 0; //Latches the button press
    }
    else if(PINA == lastEntered){ //Don't preform any action
      ++debounceCounter;
    }
    else if(PINA != lastEntered){
      processButtonPress();
      debounceCounter = 1;
    }

    lastEntered = PINA;
  }
  else {
    unpressed = 1;  //Release the latch
  }

  ENABLE_LED_CONTROL();

  //Wait for voltages to settle before moving on
  NOP();
  NOP();
  NOP();
  NOP();
  //_delay_us(20);  //Delay to allow voltages to settle
  
}

//Writes out to the bar graph AND reads in from the encoder!
void inline updateSPI( void ){
  
  ENC_CLK_ENABLE();        //Allow us to read in serial data
  ENC_PARALLEL_DISABLE();  //Allow us to read in serial data

  //NOPs required for electrical propogation
  NOP();
  NOP();

  //Write to the bar graph and read from the encoders
  SPDR = bargraphOutput;
  lastEncoderValue = SPDR;

  //Wait for SPI operation
  while (bit_is_clear(SPSR, SPIF)){};

  upToDateEncoderValue = 1;

  ENC_CLK_DISABLE();
  ENC_PARALLEL_ENABLE();

  //Output the bar graph info
  PORTB |=  0x01;
  PORTB &= ~0x01;

}

//Processes commands from the encoders
void processEncoders( void ){
  uint8_t static lEncoderPrev = 0;
  uint8_t static rEncoderPrev = 0;
  uint8_t static lEncoder = 0;
  uint8_t static rEncoder = 0;
  uint8_t static debugBounce = 0; 

  //bargraphOutput = lastEncoderValue;

/*
  uart_putc(((lastEncoderValue /100) % 10) + 48);
  uart_putc((lastEncoderValue / 10) % 10 + 48);
  uart_putc((lastEncoderValue % 10) + 48);
*/

  if(lastEncoderValue == 255 && debugBounce == 0){
    debugBounce = 1;
    uart_putc('|');
  }
  else{
    debugBounce = 0;
  

  lEncoderPrev = lEncoder;
  rEncoderPrev = rEncoder;

  //Save previous values
  lEncoder =  (lastEncoderValue & 0x03);
  rEncoder = ((lastEncoderValue & 0x0C) >> 2);

  //Check if the values have changed, if so process them
  if(lEncoder != lEncoderPrev){
    if((lEncoderPrev == 0x01) && (lEncoder == 0x03))
      ENC_L_COUNTUP();
    if((lEncoderPrev == 0x02) && (lEncoder == 0x03))
      ENC_L_COUNTDOWN();
  }

  if(rEncoder != rEncoderPrev){
    if((rEncoderPrev == 0x01) && (rEncoder == 0x03))
      ENC_R_COUNTUP();
    if((rEncoderPrev == 0x02) && (rEncoder == 0x03))
      ENC_R_COUNTDOWN();
  }

  }
/*
  uart_putc('\n');
  uart_putc('\r');
*/
}

//Called to increment the counter variable
void inline incrementCounter( void ){
  if(inc2Bool & inc4Bool)
    NOP();
  else if (inc2Bool)
    counter += 2;
  else if (inc4Bool)
    counter += 4;
  else
    counter += 1;
    
}

//Called to decrement the counter variable
void inline decrementCounter( void ){
  if(inc2Bool & inc4Bool)
    NOP();
  else if (inc2Bool)
    counter -= 2;
  else if (inc4Bool)
    counter -= 4;
  else
    counter -= 1;
}


//Parsed commands from the encoders (parsed to one call per detent)
void inline ENC_L_COUNTUP(void){
  volumeOutputTimer = VOLUME_OUTPUT_DELAY;
  if(radioVolume < 100)
    radioVolume += 10;
  //ENC_R_COUNTUP();
}
void inline ENC_L_COUNTDOWN(void){
  volumeOutputTimer = VOLUME_OUTPUT_DELAY;
  if(radioVolume > 0)
    radioVolume -= 10;
  //ENC_R_COUNTDOWN();
}
void inline ENC_R_COUNTUP(void){
  radioOutputTimer = RADIO_OUTPUT_DELAY;

  if(desired_fm_freq < 10780){
    desired_fm_freq += 20;
  }
/*
  if(settings & SET_MIN){
      minutes = (minutes + 1) % 60;
      seconds = 0;
  }
  if(settings & SET_HR){
    hours = (hours + 1) % 24;
    seconds = 0;
  }
*/
}
void inline ENC_R_COUNTDOWN(void){
  radioOutputTimer = RADIO_OUTPUT_DELAY;

  if(desired_fm_freq > 8800){
    desired_fm_freq -= 20;
  }
/*
  if(settings & SET_MIN){
    if(minutes == 0)
      minutes = 59;
    else
      minutes -= 1;
    seconds = 0;
  }
  if(settings & SET_HR){
    if(hours == 0)
      hours = 23;
    else
      hours -= 1;
    seconds = 0;
  }
*/
}

ISR(USART0_RX_vect){
  static uint8_t buf;

  buf = UDR0;

  rxBuffer[rxBufferCnt++] = buf;

  if(rxBufferCnt > 60 || rxBuffer[rxBufferCnt-1] == '\n'){
    rxBuffer[rxBufferCnt] = '\0';
    rxBufferCnt = 0;
    inputFlag = 1;
    strcpy(finalBuffer, rxBuffer);
//    uart_puts("##PARSE##");
    //uart_puts(finalBuffer);
  }

//  uart_putc(buf);
//  uart_putc('_');

}

//Initialize the lm73 sensor specifically
void init_lm73(void){
  twi_start_wr(LM73_ADDRESS, lm73_wr_buf, 1);  //Start the TWI write process (twi_start_wr())
}

//Read from the LM73 sensor and place the data in global variables
void lm73Read(void){
  char static tempStr[12];
  uint16_t lm73_temp;  //a place to assemble the temperature from the lm73

  twi_start_rd(LM73_ADDRESS, lm73_rd_buf, 2); //read temperature data from LM73 (2 bytes)  (twi_start_rd())
  _delay_ms(2);    //wait for it to finish
  lm73_temp = lm73_rd_buf[0];  //save high temperature byte into lm73_temp
  lm73_temp = lm73_temp << 8;  //shift it into upper byte 
  lm73_temp |= lm73_rd_buf[1]; //"OR" in the low temp byte to lm73_temp 
  lm73_data = lm73_temp >> 7;

//  lm73_data = lm73_temp;
 
  lm73_precision = 0;
 
  if(lm73_rd_buf[1] & 0b01000000) //Check for .5degC //bit_is_set(lm73_temp,9))
    lm73_precision |= 0x02;
  if(lm73_rd_buf[1] & 0b00100000) //Check for .25degC //bit_is_set(lm73_temp,10))
    lm73_precision |= 0x01;

  itoa(lm73_data, tempStr, 10);

  strcat(tempStr, ".");

  if(lm73_precision == 0x03)
    strcat(tempStr, "75");
  else if (lm73_precision == 0x02)
    strcat(tempStr, "50");
  else if (lm73_precision == 0x01)
    strcat(tempStr, "25");
  else
    strcat(tempStr, "00");

  strcpy(tempCelString, tempStr);
}


//Main function call
int main()
{
//set port bits 4-7 B as outputs
while(1){
  configureIO();
  configureTimers();
  configureSPI();
  configureADC();
  uart_init();
  init_twi();
  lcd_init();
  clear_display();
  configureRadio();   //Must be after init_twi();
  sei();

  fm_pwr_up(); //powerup the radio as appropriate
  desired_fm_freq = current_fm_freq = 10630; //arg2, arg3: 99.9Mhz, 200khz steps
  fm_tune_freq(); //tune radio to frequency in current_fm_freq

  RADIO_OFF();       //Start the radio as "off"

  strcpy(finalBuffer, " ");

  uart_puts("[128 init]\n\r");

  int16_t timeBuf = 0;
  remoteTemp[5] = '\0';

  int j, k;

  string2lcd("Nick McComb     ");
  line2_col1();
  string2lcd(" ECE 473        ");

  _delay_ms(500);

  strcpy(lcdOutput, "Hello, friend :)11234567890123459");
  strcpy(lcd_string_array, "                                ");

  strcpy(lcd_string_array, "Nick McComb      ECE473          ");

  _delay_us(300);

  ENABLE_LED_CONTROL();

  setLEDBrightness(0x10);

  uint8_t z;

  while(1){  //Main control loop
    for(k = 0; k < 3; ++k){
      for(j = 0; j < 5; ++j){
        //Check the buttons for input
        checkButtons(); 
	setDigit(j);  //At last measure takes ~9uS to run (varies 400nS)
        global_targetDigit = j;	

	for(z = 0; z < 10; ++z){_delay_us(100);}
	
        //Update everything on the SPI bus (minus the LCD)
	//This means we're reading the encoders and writing to the bar graph
	if(j != 0){
	updateSPI();
	processEncoders();
	}
	//_delay_us(100);

        clearSegment();
//DEBUG_LOW();
//	_delay_us(5);
	NOP();
      }
      if(k == 2){
        //while(twi_busy()){}
        //fm_rsq_status();
        //bargraphOutput = si4734_tune_status_buf[4];
      }
    }

//    fm_rsq_status();
    //processRadioTune();
    processCounterOutput();  //Doesn't have to happen all of the time, so it's called here.
    processAlarm();          //This processes the alarm outputs (incl the LCD)
//    processVolume();         //Outputs the volume we want
    processRadioTune();      //Tunes the radio if necessary
    processVolume();         //Outputs the volume we want (NOTE: has to be after radio tune)

    //Refresh the LCD and when the string has been outputted, copy the queued string into
    //the string to be outputted. This prevents weird artifacts from appearing on the screen.
    if(!refresh_lcd(lcd_final))
      strcpy(lcd_final, lcd_string_array);

//DEBUG_HIGH();

    for(z = 0; z < 10; ++z){_delay_us(100);}

    if(inputFlag){ //We need to parse the input from the remote sensor
      inputFlag = 0;  //Reset flag

#ifdef USE_GPS_TIME
      //Check if we're receiving time info
      if(strlen(finalBuffer) > 5){

        //Calculate hours from GPS
        timeBuf = 0;
	timeBuf = (finalBuffer[6] - 48) * 10 + (finalBuffer[7] - 48);
	//Daylight savings time
        timeBuf -= 8;    //DST Hack
	if(timeBuf < 0)  //Check for overflow
	  timeBuf += 24; //Overflow hack
        hours = timeBuf;

	//Calculate minutes from GPS
	timeBuf = (finalBuffer[8] - 48) * 10 + (finalBuffer[9] - 48);
        minutes = timeBuf;
	
	//This next line disables the counting that the
	//mega would be doing on it's own, so that if 
	//we have a GPS lock, that is what does the
	//coutning, but if we don't (e.g. this point is
	//never reached), then the mega will count on it's
	//own

	//I chose 30 seconds because the colon uses the 
	//seconds counter, and this should invisibly
	//make the jump back to 0, while never getting
	//high enough to trigger a minute jump
	if(seconds >= 30)
	  seconds = 0;
//	seconds = 0; 
      }
#endif 
      if(finalBuffer[6] == ' ') {
        //uart_puts("No time info found...\r\n");
      }
      //while(twi_busy()){}
      //fm_rsq_status();
      //bargraphOutput = si4734_tune_status_buf[4];
    }

    //Process the remote temperature
    static uint8_t k;
    for(k = 0; k < 5; ++k)
      remoteTemp[k] = finalBuffer[k];

    //Process the local temperature
    lm73Read();

    }
  
  }

}
