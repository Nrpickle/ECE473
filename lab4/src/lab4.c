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

Enable audio PWM
enable dimming PWM
dimming PWM logic



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

//PORTB Definitions
#define DIG_SEL_1 0x10
#define DIG_SEL_2 0x20
#define DIG_SEL_3 0x40
#define PWM_CTRL 0x80

//PORTD Definitions
#define AUDIO_OUT 0x10

//Segment definitions
#define SEG_OFF 0xFF 

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
void processButtonPress( void );
void processCounterOutput( void );
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
uint32_t output[5]; //Note, this is zero indexed for digits!!! The 0 index is for the colon
int16_t  lastEntered = 0;
int16_t  debounceCounter = 0;
uint8_t  unpressed = 1;
uint8_t  lastEncoderValue = 0x13;
uint8_t  upToDateEncoderValue = 0;  //Holds whether the encoder value is a newly measured value
uint8_t  bargraphOutput = 0;
uint8_t  secondsCounter = 0; //When counted is 255, a second has past
uint8_t  quickToggle = 0;
uint16_t lastADCread = 200;  //Last ADC reading, default to a realistic value 


//time management
uint8_t  seconds = 0;
uint8_t  minutes = 0;
uint8_t  hours   = 0;

//alarm management
uint8_t  alarmMinutes = 0;
uint8_t  alarmHours   = 0;

//Digit points
uint8_t  dot[5] = {0,0,0,0,0};
uint8_t  upperDot = 0;
uint8_t  colon = 0;

//Brightness management
uint8_t  lux[10] = { 0x01, 0x20, 0x70, 0xA0, 0xC0, 0xD0, 0xD8, 0xDF, 0xE0, 0xEF };
uint8_t  brightnessControl = 0;
void inline setLEDBrightness(uint8_t targetBrightness){OCR2 = targetBrightness;}
void inline START_ADC_READ(void){ADCSRA |= (1<<ADSC);}  //Starts the read from the ADC
void inline FINISH_ADC_READ(void){while(bit_is_clear(ADCSRA, ADIF)); ADCSRA |= (1<<ADIF); lastADCread = ADC;}

//LCD Output
char lcdOutput[40];
uint8_t lcdCounter = 0;

//Editing settings
uint16_t settings = 0;

enum settings_t {SET_MIN = 0x01, SET_HR = 0x02, TIME24 = 0x10};

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

  //For this lab, we are just driving the PWM_CTRL line low always
  //PORTB |= PWM_CTRL;  
  //(it defaults to low)
  uint8_t i;

  //Init output to 0
  for(i = 0; i < 5; ++i){
    output[i] = 0;
  }

  DDRE |= 0xC0;  //Enable Clk inhibit pin and async pin as outputs
  ENC_CLK_DISABLE();
  ENC_PARALLEL_ENABLE();

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
  OCR3A = 200;

  //Eat a potato
}

//Timer 0 overflow vector
//Polls the buttons / interfaces with SPI
//Counts seconds
//Updates values
//This ISR is invoked every 255 clock cycles of the 32.768kHz oscillator (~128Hz)
ISR(TIMER0_OVF_vect){
  //Begin ADC reading

  //Poke ADC and start conversion
//  ADCSRA |= (1<< ADSC);

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
  }
  //Exectued 128Hz
  if (secondsCounter % 1 == 0){
    checkButtons();

    updateSPI();
    
    processEncoders();

    //Output to LCD
    if(++lcdCounter == 16){
      //lcdCounter = 0;
      line2_col1();
      //cursor_home();
    }
    else if(lcdCounter == 32){
      line1_col1();
      lcdCounter = 0;
    }
    
    char2lcd(lcdOutput[lcdCounter]);

  }
  //Executed 4Hz
  if(secondsCounter % 32 == 0){  //Fast cycle
        //OCR2 += lux[brightnessControl];
        //brightnessControl = (++brightnessControl) % 10;

  //PORTD ^= AUDIO_OUT; 

    //Poke ADC and start conversion
//    ADCSRA |= (1 << ADSC);
    

    quickToggle ^= 1;


    //Wait for ADC read to be complete
//    while(bit_is_clear(ADCSRA, ADIF));
    //When it's done, clear the interrupt flag by writing a one
//    ADCSRA |= (1<<ADIF);
    //Read the result (16 bits)
//    lastADCread = ADC;
  }

  //Read ADC value

  //Wait for ADC read to be complete
//  while(bit_is_clear(ADCSRA, ADIF));
  //When it's done, clear the interrupt flag by writing a one
//  ADCSRA |= (1<<ADIF);
  //Read the result (16 bits)
//  lastADCread = ADC;


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
  ADCSRA = (1<<ADEN) | (1<<ADPS0) | (1<<ADPS1) | (1<<ADPS2);

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
  switch(targetDigit){

    case 0: //colon control
      SET_DIGIT_DOT();  //Digit control
      _delay_us(50);
      
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
      _delay_us(50);
      if((settings & SET_HR) && quickToggle)
        clearSegment();
      else
        setSegment(output[1]);
      //Check if we want to remove a leading zero
      if((hours > 0 && hours < 10) || (hours > 12 && hours < 22) && !(settings & TIME24)){ //Then we want to remove 0
        clearSegment();
      }
      if(dot[1])
        PORTA = PORTA & ~(SEG_DP);
      break;
    case 2:
      SET_DIGIT_TWO();
      _delay_us(50);
      if((settings & SET_HR) && quickToggle)
        clearSegment();
      else
        setSegment(output[2]);
      if(dot[2])
        PORTA = PORTA & ~(SEG_DP);
      break;
    case 3:
      SET_DIGIT_THREE();
      _delay_us(50);
      if((settings & SET_MIN) && quickToggle)
        clearSegment();
      else
        setSegment(output[3]);
      if(dot[3])
        PORTA = PORTA & ~(SEG_DP);
      break;
    case 4:
      SET_DIGIT_FOUR();
      _delay_us(50);
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
    case 0x01:
      inc2Bool ^= 0x01;
      bargraphOutput ^= (1 << 0);
      break;
    case 0x02:
      inc4Bool ^= 0x01;
      bargraphOutput ^= (1 << 1);
      break;
    case 0x10: //Test button
      hours = 11;
      minutes = 44;
      seconds = 55;
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

//Checks the buttons when called, and calls a seperate processing function once the buttons have been debounced
//It will call it only once per button press, and resets upon button release.
void inline checkButtons( void ){
  ENABLE_BUTTON_READ();
  ENABLE_BUFFER();
  _delay_us(5); //Essentially a nop? No way. Not a nop. Dear god not at all. Same principle, though. Wait for voltages to settle.

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
  //_delay_us(20);  //Delay to allow voltages to settle
  
}

//Writes out to the bar graph AND reads in from the encoder!
void inline updateSPI( void ){
  
  ENC_CLK_ENABLE();        //Allow us to read in serial data
  ENC_PARALLEL_DISABLE();  //Allow us to read in serial data

  //NOPs required for electrical propogation
  NOP();
  NOP();

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

}
void inline ENC_L_COUNTDOWN(void){

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

  string2lcd(" Eat a potato! :D");

  strcpy(lcdOutput, "Hello, friend :)   Welcome to pr");
//uint8_t counter = 0;

  ENABLE_LED_CONTROL();

  setLEDBrightness(0x10);

  while(1){  //Main control loop
    for(k = 0; k < 15; ++k){
      for(j = 0; j < 5; ++j){
        //clearSegment();
        _delay_us(50);
	
	setDigit(j);  //Contains 100uS delay

        //We do an ADC read around the existing delay, because it should take 
	//~104us to preform the ADC read anyway (in theory (*fingers crossed*))
        START_ADC_READ(); 
        _delay_us(130); //Lowest tested to be 750uS because of light bleed, can recomfirm
	FINISH_ADC_READ();

        clearSegment();

      }
    }

    processCounterOutput();  //Doesn't have to happen all of the time, so it's called here.

/*
    //ADC test code
    ADCSRA |= (1<<ADSC);

    while(bit_is_clear(ADCSRA, ADIF));

    ADCSRA |= (1<<ADIF);

    temp_adcResult = ADC;
*/

/*
    itoa(lastADCread, lcd_str_l, 10);

    clear_display();
    NOP();
    NOP();
    string2lcd(" ");
    string2lcd(lcd_str_l);

    cursor_home();
*/ 

  }
  
  }
}
