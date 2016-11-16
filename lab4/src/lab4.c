// ECE 473 Lab 3
// R. Traylor
// 9.12.08
// Modified in October 2016 by Nick McComb | www.nickmccomb.net

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

fix dimming logic (is the ADC even working?!)
make volume go all the way to zero

*/



//#define F_CPU 16000000 // cpu speed in hertz 
#define TRUE 1
#define FALSE 0
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stddef.h>
#include "hd44780.h"

//Program controls
//#define LEADING_0  //Whether or not you want leading zeros

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


//Function Prototypes
void inline incrementCounter( void );
void inline decrementCounter( void );
void setDigit( uint8_t targetDigit );
void configureIO( void );
void configureTimers( void );
void configureSPI( void );
void configureADC( void );
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

void inline ENC_PARALLEL_ENABLE(void)  {PORTE &= ~(0x80);}
void inline ENC_PARALLEL_DISABLE(void) {PORTE |=   0x80 ;}


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
//Volume control (OCR3A needs to range from 85 to 430 to be within working parameters)
#define ALARM_VOLUME 60 //Alarm volume in percentage
void inline SET_VOLUME(uint8_t volumePercentage){ OCR3A = volumePercentage * 3.45 + 85; }
void inline SET_HZ(uint16_t targetHz) {OCR1A = targetHz * 50;}
uint16_t musicCounter = 0;
#define NUM_MUSIC_NOTES 15
//Supposed to be the super mario theme... credit: www.mikrotik.com/wiki/Super_Mario_Theme
uint16_t music[25] = {660, 660, 660, 510, 660, 770, 380, 510, 380, 320, 440, 480, 450, 430, 380, 660, 760, 860, 700, 760, 660, 520, 580, 480};

//LED Mangement
uint8_t volatile global_targetDigit = 0;

//time management
uint8_t  seconds = 0;
uint8_t  minutes = 0;
uint8_t  hours   = 1;  //TODO change start time

//alarm management (alarm is in 24 hours)
uint8_t  alarmMinutes = 20;
uint8_t  alarmHours   = 0;
uint8_t  currentlyAlarming = 0;
uint16_t snoozeCount = 0;
#define SNOOZE_SECONDS 10


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

//Debugging
#define DEBUG_PIN 0x02

#define DEBUG_HIGH() {PORTF |=  DEBUG_PIN;}
#define DEBUG_LOW()  {PORTF &= ~DEBUG_PIN;}


//NOT USED
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

  DDRE |= 0xC0;  //Enable Clk inhibit pin and async pin as outputs
  ENC_CLK_DISABLE();
  ENC_PARALLEL_ENABLE();


  DDRF |=   DEBUG_PIN; //Enable PORTF PINX as a debug output
  DEBUG_LOW();  //Set the pin low to start
}

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

  }
 //Exectued 128Hz
  if (secondsCounter % 1 == 0){
//DEBUG_HIGH();
    //Check the buttons for input
    checkButtons();
    NOP();
    NOP();
    
    //Reset the outputs to be what they should be
    setDigit(global_targetDigit);

    processOutputBrightness();
    START_ADC_READ();

    processEncoders();
  }

  //Executed 4Hz
  if(secondsCounter % 32 == 0){  //Fast cycle

    //START_ADC_READ(); 

    quickToggle ^= 1;
    
    ++musicCounter;

    if(musicCounter >= NUM_MUSIC_NOTES)
      musicCounter = 0;
  }

  if(secondsCounter == 128)
    secondsCounter = 0;
}


//Audio generation interrupt
ISR(TIMER1_COMPA_vect){
   //Toggle audio output bit
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
}

//This function is called when a button is pressed, and handles processing the press, as well as
//changing tne numbers that are to be outputted.
void processButtonPress( void ){
  
  uint8_t temp = 0xFF - PINA;

  switch(temp){
    case 0x01: //Add to alarm minutes
      alarmMinutes += 5;
      if(alarmMinutes >= 60){
        alarmMinutes = 0;
	++alarmHours;
	if(alarmHours >= 24)
	  alarmHours = 0;
      }
      break;
    case 0x02: //Add to alarm hours
      ++alarmHours;
      if(alarmHours >= 24)
        alarmHours = 0;
      break;
    case 0x04: //Kill alarm
      currentlyAlarming = 0;
      snoozeCount = 0;      //We also want to get rid of snooze
      break;
    case 0x08: //Snooze alarm
      if(currentlyAlarming){
        currentlyAlarming = 0; //Kill the alarm
        settings &= ~(ALARM_ARMED); //Disarm the alarm
        snoozeCount = 1;       //This starts the snooze counter
      }
      break;
    case 0x10: //Arm alarm button
      settings ^= ALARM_ARMED;
      break;
    case 0x20: //Set military time button
      settings ^= TIME24;
      if(settings & TIME24)
        upperDot = TRUE;
      else
        upperDot = FALSE;
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


  //Detecting Alarm output
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
  }
  else{  //The alarm isn't armed, so we want to output that fact
    dot[4] = 0;
    lcd_string_array[0] = 'n';
    lcd_string_array[1] = 'o';
    lcd_string_array[2] = ' ';
    lcd_string_array[3] = 'a';
    lcd_string_array[4] = 'l';
    lcd_string_array[5] = 'a';
    lcd_string_array[6] = 'r';
    lcd_string_array[7] = 'm';

    uint8_t i;
 
    for(i = 0; i < 20; ++i)
      lcd_string_array[i+8] = ' ';
  }

  //lcd_string_array[5] is blank
  if(!currentlyAlarming){
    SET_VOLUME(0);
  }
  else {
    //SET_HZ(400);
    SET_VOLUME(ALARM_VOLUME);
    SET_HZ(music[musicCounter]);
    //if(quickToggle)
    //  SET_HZ(440);
    //else
    //  SET_HZ(880);
  }
  
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
  ENC_R_COUNTUP();
}
void inline ENC_L_COUNTDOWN(void){
  ENC_R_COUNTDOWN();
}
void inline ENC_R_COUNTUP(void){
  
  if(settings & SET_MIN){
      minutes = (minutes + 1) % 60;
      seconds = 0;
  }
  if(settings & SET_HR){
    hours = (hours + 1) % 24;
    seconds = 0;
  }
}
void inline ENC_R_COUNTDOWN(void){
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
  lcd_init();
  clear_display();
  sei();

  uint8_t temp_counter = 1;
  uint8_t tempBool = 0x01;

  int j, k;

  uint16_t temp_adcResult = 0;
  char lcd_str_l[16];

  string2lcd("Nick McComb     ");
  line2_col1();
  string2lcd(" ECE 473        ");

  _delay_ms(500);

  strcpy(lcdOutput, "Hello, friend :)11234567890123459");
  strcpy(lcd_string_array, "                                ");

  strcpy(lcd_string_array, "Nick McComb      ECE473          ");
//  strcpy(lcd_string_array, "Hello, friend :)|123456789");
//uint8_t counter = 0;

  _delay_us(300);

/*
  uint8_t z = 0;
  for(z = 0; z < 45; z++){
    refresh_lcd(lcd_string_array);
    _delay_us(50);
  }

  updateSPI();
*/ 
  ENABLE_LED_CONTROL();

  setLEDBrightness(0x10);

  while(1){  //Main control loop
    for(k = 0; k < 2; ++k){
      for(j = 0; j < 5; ++j){
        //clearSegment();
        //_delay_us(100);

//DEBUG_HIGH();
	setDigit(j);  //At last measure takes ~9uS to run (varies 400nS)
//DEBUG_LOW();
        global_targetDigit = j;	

        //Update everything on the SPI bus (minus the LCD)
	//This means we're reading the encoders and writing to the bar graph
        //updateSPI();
	//refresh_lcd(lcd_string_array);

        //We do an ADC read around the existing delay, because it should take 
	//~104us to preform the ADC read anyway (in theory (*fingers crossed*))
//	DEBUG_HIGH();
        //START_ADC_READ(); 
//	_delay_ms(2);
        uint8_t z;
	for(z = 0; z < 10; ++z){_delay_us(100);}
//        _delay_us(2000); //Lowest tested to be 750uS because of light bleed, can recomfirm
	//FINISH_ADC_READ();
//	DEBUG_LOW();
        
	//refresh_lcd(lcd_string_array);
	updateSPI();
	//_delay_us(100);

        clearSegment();
//DEBUG_LOW();
//	_delay_us(5);
	NOP();
      }
    }

    processCounterOutput();  //Doesn't have to happen all of the time, so it's called here.
    processAlarm();          //This processes the alarm outputs (incl the LCD)
    //processOutputBrightness();

    //Refresh the LCD and when the string has been outputted, copy the queued string into
    //the string to be outputted. This prevents weird artifacts from appearing on the screen.
    if(!refresh_lcd(lcd_final))
      strcpy(lcd_final, lcd_string_array);

DEBUG_HIGH();

    //refresh_lcd(lcd_string_array);

//DEBUG_LOW();


    _delay_us(50); 

  }
  
  }

}
