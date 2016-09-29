//This code shows how debouncing should and should not be done.
//TCNT0 is used with 16mhz clock to vary the sampling period.

//Vary the buttons and see which is worst. They can vary alot.
//Vary the sampling time and note any difference.
//Note the use of #ifdef statements

//R. Traylor 12.16.2013
                            
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>
#include <stdlib.h>
#include "lcd_functions.h"

#define DEBOUNCE

char lcd_str[16];                    //holds string to send to lcd
volatile uint16_t switch_count = 0;  //count switch closures
uint8_t i;                           //just a dumb counter

//******************************************************************************
//                            spi_init                               
//Initalizes the SPI port on the mega128. Does no further device specific inits
//******************************************************************************
void spi_init(void){
    DDRB |=  0x07;  //Turn on SS, MOSI, SCLK
    //mstr mode, sck=clk/2, cycle 1/2 phase, low polarity, MSB 1st, no interrupts 
    SPCR=(1<<SPE) | (1<<MSTR); //enable SPI, clk low initially, rising edge sample
    SPSR=(1<<SPI2X); //SPI at 2x speed (8 MHz)  
}//spi_init

ISR(TIMER0_COMP_vect){
#ifdef DEBOUNCE 
  static uint16_t state = 0; //holds present state
  state = (state << 1) | (! bit_is_clear(PIND, 7)) | 0xE000;
  if (state == 0xF000) {switch_count++;}
#else 
  //uses 3 state, state machne to make pushbutton code
  static enum button_state_type{NOTSET, SET, WAIT} button_state;
  switch(button_state){
    case(NOTSET): 
      if(bit_is_clear(PIND,1)){ 
        button_state=SET;
      } break;
    case(SET): 
      switch_count++; 
      button_state=WAIT; 
      break;
    case(WAIT): 
      if(bit_is_set(PIND,1)){ 
        button_state=NOTSET;
      } 
      break;
    default: break;
  } //switch                  
#endif
} //ISR

int main(void){
  spi_init();      //initalize SPI 
  lcd_init();      //initalize LCD 
  clear_display(); //manually clear LCD display 
  cursor_off();    //keep LCD cursor off
 
  //TCNT0 running in CTC mode
  TIMSK |= (1<<OCIE0);  //enable interrupts
  TCCR0 |= (1<<WGM01);  //CTC mode, no prescale
//  OCR0  |= 0x01;        //sample really, really fast 
OCR0  |= 0xFF;        //compare register set at 256

TCCR0 |= (1<<CS00);                     //no prescaling
//TCCR0 |= (1<<CS01)|(1<<CS00);           //prescale by 32
//TCCR0 |= (1<<CS02)|(1<<CS00);           //prescale by 128
//TCCR0 |= (1<<CS02)|(1<<CS01)|(1<<CS00); //prescale by 1024

//interrupt period = 62.5ns * (0CR0+1) * prescale
//0CR0=0xFF, prescale=1024   => period=32.7ms
//OCR0=0x01, prescale=none,  => 8Mhz (125ns)

  sei(); //enable global interrupts

  while(1){   //main while loop to make LCD visible
    for(i=0;i<5;i++){_delay_ms(2);} //0.01 second wait
    itoa(switch_count, lcd_str, 10);
    string2lcd(lcd_str);
    cursor_home();
  } //while
} //main
