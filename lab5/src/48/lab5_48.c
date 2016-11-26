/*
MEGA 48 Code for ECE473

Nick McComb | www.nickmccomb.net
Written November 2016


*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "uart_functions.h"
#include <string.h>
#include "lm73_functions_skel.h"
#include "twi_master.h"

int main(){

  PORTD = 0x00;
  DDRD = 0xFF;

  uart_init();

  while(1){
//    PORTD ^= 0x01;
    PORTD = 0x01;
    PORTD = 0x00;

    _delay_ms(50);

    uart_puts("Hello, world :)\n\r");

  }
}
