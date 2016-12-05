/*
MEGA 48 Code for ECE473

This performs the functions of an external temperature sensor, and eventually a GPS parser.

Nick McComb | www.nickmccomb.net
Written November 2016

Basic UART test setup: https://goo.gl/JcuxMc

GPS input looks for GPGLL (latitute and time info)

*/

/*

TODO: Test Bootup (no GPS lock case)
TODO: Add T/C so that it outputs at a regular interval

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
char     inputBuf[155];  //Massive UART RX Input buffer
uint8_t  inputBufCnt = 0;
uint8_t  inputFlag = 0;
char gpgll[60];  //Should be larger than it can be
uint8_t gpgllCnt = 0;
//char lineBuf[100];
uint8_t lineBufCnt = 0;

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
ISR(USART_RX_vect){
  uint8_t max = 140; //Maximum number of characters in the buffer

  //Get character
  inputBuf[inputBufCnt++] = UDR0;

//  if(inputBuf[inputBufCnt - 1] == '\n';

//  if(inputBuf[

  if(inputBufCnt >= max || inputBuf[inputBufCnt-1] == '\n'){
    inputBuf[inputBufCnt + 1] = '\r';
    inputBuf[inputBufCnt + 2] = '\0';

    if(inputBuf[0] == '$' && inputBuf[3] == 'G' && inputBuf[4] == 'L' && inputBuf[5] == 'L'){
      strcpy(gpgll, inputBuf);
    }

    inputBufCnt = 0;
    inputFlag = 0x01;
  }
}


int main(){
//  configureIO();
//  uart_init();
//  init_twi();
  char outputString[50];
  uint16_t loopCounter = 0;

  char timeStr[12]; //holds the time in string form
  char tempStr[12]; //holds the temp in string form
//  DDRD = 0xFE; //Set the RX pin as an input

  uart_init();
  init_lm73();
  sei();
//  uart_puts("[Init 48 remote]\n\r");

  while(1){
    
    _delay_ms(250);

    lm73Read();

    /* //Debug loop counter
    itoa(++loopCounter, outputString, 10);
    uart_puts(outputString);
    uart_puts(": ");
    */

    itoa(lm73_data, tempStr, 10);

    strcat(tempStr, ".");
    if(lm73_precision == 0x03)
      strcat(tempStr, "75");  //uart_puts("75");
    else if (lm73_precision == 0x02)
      strcat(tempStr, "50"); //uart_puts("50");
    else if (lm73_precision == 0x01)
      strcat(tempStr, "25"); //uart_puts("25");
    else
      strcat(tempStr, "00"); //uart_puts("00");

    if(inputFlag){   //Then we're ready to process input data from GPGLL
      inputFlag = 0; //Reset input flag
      if(strlen(gpgll) > 15){ //Then we probably have a valid GPGLL string to process
        char * pnt;
        uint8_t j;
        pnt = strtok(gpgll, ",");
	for(j = 0; j < 5; ++j){
	  pnt = strtok(NULL, ",");
	}
        strcpy(timeStr, pnt); 
      }

    }

    //Assemble output string:
    strcpy(outputString, "");
    strcat(outputString, tempStr);
    strcat(outputString, "|");
    strcat(outputString, timeStr);
    strcat(outputString, "\n");

    uart_puts(outputString (char*));
    //Only use the following line for computer terminal usage!
    //uart_puts("\r");

  }
}
