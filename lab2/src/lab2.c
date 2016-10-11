// lab2_skel.c 
// R. Traylor
// 9.12.08
//Modified in October 2016 by Nick McComb | www.nickmccomb.net

//  HARDWARE SETUP:
//  PORTA is connected to the segments of the LED display. and to the pushbuttons.
//  PORTA.0 corresponds to segment a, PORTA.1 corresponds to segement b, etc.
//  PORTB bits 4-6 go to a,b,c inputs of the 74HC138.
//  PORTB bit 7 goes to the PWM transistor base.

//#define F_CPU 16000000 // cpu speed in hertz 
#define TRUE 1
#define FALSE 0
#include <avr/io.h>
#include <util/delay.h>
#include <stddef.h>


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

//holds data to be sent to the segments. logic zero turns segment on
uint8_t segment_data[5];

//decimal to 7-segment LED display encodings, logic "0" turns on segment
uint8_t dec_to_7seg[12];

//Digit control low-level code
void inline SET_DIGIT_ONE(void)   {PORTB |= DIG_SEL_3; PORTB = PORTB & ~(DIG_SEL_1 | DIG_SEL_2);}
void inline SET_DIGIT_TWO(void)   {PORTB |= DIG_SEL_1 | DIG_SEL_2; PORTB = PORTB & ~(DIG_SEL_3);}
void inline SET_DIGIT_THREE(void) {PORTB |= DIG_SEL_1; PORTB = PORTB & ~(DIG_SEL_2 | DIG_SEL_3);}
void inline SET_DIGIT_FOUR(void)  {PORTB = PORTB & ~(DIG_SEL_1 | DIG_SEL_2 | DIG_SEL_3);}

//Tri-State Buffer Enable
void inline ENABLE_BUFFER(void)   {PORTB |= DIG_SEL_1 | DIG_SEL_2 | DIG_SEL_3;}

//Port A Control
void inline ENABLE_LED_CONTROL(void) {DDRA = 0xFF; SET_DIGIT_ONE();} //Enables PORTA as an output, while also ensuring the Tri-state buffer is disabled by selecting digit one
void inline ENABLE_BUTTON_READ(void) {DDRA = 0x00; PORTA = 0xFF;}  //Enable inputs/pullups on PORTA

void configureIO( void );
void setSegment( uint16_t targetOutput );
void setDigit( uint8_t targetDigit );
void processButtonPress( void );

//Global Variables
uint16_t counter = 0;
uint32_t  output[5]; //Note, this is zero indexed for digits!!! The 0 index is for the colon

//******************************************************************************
//                            chk_buttons                                      
//Checks the state of the button number passed to it. It shifts in ones till   
//the button is pushed. Function returns a 1 only once per debounced button    
//push so a debounce and toggle function can be implemented at the same time.  
//Adapted to check all buttons from Ganssel's "Guide to Debouncing"            
//Expects active low pushbuttons on PINA port.  Debounce time is determined by 
//external loop delay times 12. 
//
uint8_t chk_buttons(uint8_t button) {
//******************************************************************************

//***********************************************************************************
//                                   segment_sum                                    
//takes a 16-bit binary input value and places the appropriate equivalent 4 digit 
//BCD segment code in the array segment_data for display.                       
//array is loaded at exit as:  |digit3|digit2|colon|digit1|digit0|

  return 0;
}

void segsum(uint16_t sum) {
  //determine how many digits there are 
  //break up decimal sum into 4 digit-segments
  //blank out leading zero digits 
  //now move data to right place for misplaced colon position
}//segment_sum
//***********************************************************************************

void configureIO( void ){

  ENABLE_LED_CONTROL(); 

  //DDRA = 0xFF; //Initialize DDRA as if we want to control the LEDs
  DDRB = 0xF0; //Upper nibble of the B register is for controlling the decoder / PWM Transistor

  //For this lab, we are just driving the PWM_CTRL line low always
  //PORTB |= PWM_CTRL;  
  //(it defaults to low)

  uint8_t i;


  //Init output to 0
  for(i = 0; i < 5; ++i){
    output[i] = 0;
  }
	
}


void setSegment( uint16_t targetOutput ){
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
      
  }
 
}

void setDigit( uint8_t targetDigit ){ 
  switch(targetDigit){
    case 1:
      setSegment(output[1]);
      SET_DIGIT_ONE();
      break;
    case 2:
      setSegment(output[2]);
      SET_DIGIT_TWO();
      break;
    case 3:
      setSegment(output[3]);
      SET_DIGIT_THREE();
      break;
    case 4:
      setSegment(output[4]);
      SET_DIGIT_FOUR();
      break;
  }

}

void processButtonPress( void ) {
  counter += 0xFF - PINA;

  if(counter >= 1024){
    counter -= 1023;
  }

  uint16_t tempCounter = counter;
  //calculate new output values
  output[4] = tempCounter % 10;
  tempCounter /= 10;
  output[3] = tempCounter % 10;
  tempCounter /= 10;
  output[2] = tempCounter % 10;
  tempCounter /= 10;
  output[1] = tempCounter % 10;

}



//***********************************************************************************
int main()
{
//set port bits 4-7 B as outputs
while(1){
  configureIO();

  int i, j, k;

  ENABLE_LED_CONTROL();

  int16_t lastEntered;
  int16_t debounceCounter = 0;
  uint8_t unpressed = 1;

  while(1){
    //setSegment(counter);
    //_delay_ms(250);
    for(k = 0; k < 100; ++k){
      for(j = 1; j < 5; ++j){
        setDigit(j);
        _delay_us(100); //Lowest tested to be 100uS because of light bleed, can recomfirm

      }
    }

    ENABLE_BUTTON_READ();
    ENABLE_BUFFER();
    _delay_us(5); //Essentially a nop? No way. Not a nop. Dear god not at all. Same principle, though.


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

    _delay_us(20);

    //This loop will take 40mS

  }
  
  //insert loop delay for debounce
  //make PORTA an input port with pullups 
  //enable tristate buffer for pushbutton switches
  //now check each button and increment the count as needed
  //disable tristate buffer for pushbutton switches
  //bound the count to 0 - 1023
  //break up the disp_value to 4, BCD digits in the array: call (segsum)
  //bound a counter (0-4) to keep track of digit to display 
  //make PORTA an output
  //send 7 segment code to LED segments
  //send PORTB the digit to display
  //update digit to display
  }//while
}//main
