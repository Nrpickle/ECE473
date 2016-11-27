/*
MEGA 48 Code for ECE473

This performs the functions of an external temperature sensor, and eventually a GPS parser.

Nick McComb | www.nickmccomb.net
Written November 2016

Basic UART test setup: https://goo.gl/JcuxMc


*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "uart_functions.h"
#include <string.h>
#include "lm73_functions_skel.h"
#include "twi_master.h"

//Global Variables

/*

//Delclare the 2 byte TWI read and write buffers (lm73_functions_skel.c)
extern uint8_t lm73_wr_buf[2];
extern uint8_t lm73_rd_buf[2];
uint16_t lm73_data;

//Prototypes
void configureIO(void);  //conffigures GPIO
void init_lm73(void);
void lm73Read(void);

//Configures the inputs/outputs
void configureIO(void){

}

void init_lm73(void){
  //delclare the 2 byte TWI read and write buffers (lm73_functions_skel.c)
//  extern uint8_t lm73_wr_buf[2];
//  extern uint8_t lm73_rd_buf[2];

  twi_start_wr(LM73_ADDRESS, lm73_wr_buf, 1);   //start the TWI write process (twi_start_wr())
}

void lm73Read(void){
  uint16_t lm73_temp;  //a place to assemble the temperature from the lm73

  twi_start_rd(LM73_ADDRESS, lm73_rd_buf, 2); //read temperature data from LM73 (2 bytes)  (twi_start_rd())
  _delay_ms(2);    //wait for it to finish
  lm73_temp = lm73_rd_buf[0];  //save high temperature byte into lm73_temp
  lm73_temp = lm73_temp << 8;  //shift it into upper byte 
  lm73_temp |= lm73_rd_buf[1]; //"OR" in the low temp byte to lm73_temp 
 
  lm73_data = lm73_temp;
}

*/

int main(){
//  configureIO();
//  uart_init();

//  init_twi();
  
  //sei(); 
 
//  PORTD = 0x00;
  char outputString[50];
  uint16_t loopCounter = 0;

  DDRD = 0xFD; //Set the RX pin as an input

  uart_init();

  sei();

  uart_puts("[Init 48 remote]\n\r");

  while(1){
    
//    PORTD ^= 0x01;
//    PORTD = 0x01;
//    PORTD = 0x00;
//    _delay_us(2);
//    PORTD = 0x01;

    _delay_ms(50);
    //while(1);
    //_delay_ms(50);
    
    itoa(++loopCounter, outputString, 10);
    uart_puts(outputString);
    uart_puts(": ");
    uart_puts("Hello, world :)\n\r\0");

  }
}
