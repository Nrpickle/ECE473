/*
MEGA 48 Code for ECE473

This performs the functions of an external temperature sensor, and eventually a GPS parser.

Nick McComb | www.nickmccomb.net
Written November 2016

Basic UART test setup: https://goo.gl/JcuxMc

GPS input looks for GPGLL (latitute and time info)

*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "uart_functions.h"
#include <string.h>
#include "lm73_functions_skel.h"
#include "twi_master.h"

//Global Variables



//Delclare the 2 byte TWI read and write buffers (lm73_functions_skel.c)
extern uint8_t lm73_wr_buf[2];
extern uint8_t lm73_rd_buf[2];
uint16_t lm73_data;
uint16_t lm73_precision;
char     inputBuf[255];  //Massive UART RX Input buffer
uint8_t  inputBufCnt = 0;
uint8_t  inputFlag = 0;

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
  lm73_data = lm73_temp >> 7;

//  lm73_data = lm73_temp;
 
  lm73_precision = 0;
 
  if(lm73_rd_buf[1] & 0b01000000) //Check for .5degC //bit_is_set(lm73_temp,9))
    lm73_precision |= 0x02;
  if(lm73_rd_buf[1] & 0b00100000) //Check for .25degC //bit_is_set(lm73_temp,10))
    lm73_precision |= 0x01;
}

//UART Rx Vector
ISR(USART0_RX_vect){
  //Get character
//  inputBuf[inputBufCnt++] = UDR0;

//  if(inputBufCnt == 100){
//    inputBufCnt = 0;
//    inputBuf[101] = '\0';
//
//    inputFlag = 0x01;
//  }

}


int main(){
//  configureIO();
//  uart_init();
//  init_twi();
  char outputString[50];
  uint16_t loopCounter = 0;

  DDRD = 0xFE; //Set the RX pin as an input

  uart_init();
  init_lm73();

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
    lm73Read();

    itoa(++loopCounter, outputString, 10);
    uart_puts(outputString);
    uart_puts(": ");
    
    itoa(lm73_data, outputString, 10);
    uart_puts(outputString);

    uart_puts(".");
    if(lm73_precision == 0x03)
      uart_puts("75");
    else if (lm73_precision == 0x02)
      uart_puts("50");
    else if (lm73_precision == 0x01)
      uart_puts("25");
    else
      uart_puts("00");


    uart_puts(" |\n\r\0");

/*    if(inputFlag){
      inputFlag = 0;
      uart_puts(inputBuf);

    }
*/
  }
}
