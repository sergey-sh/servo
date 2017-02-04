// avr-gcc -std=c99 -Wall -Os -mmcu=attiny13 -o led.out led.c
// sudo avrdude -c usbtiny -p t13 -U lfuse:w:0x7A:m -U hfuse:w:0xff:m
// CLOCK8 без него был 1M тактовая частота,теперь 9.6M
#include <avr/io.h>

#ifndef SERVOCTRLSIGN 
#define SERVOCTRLSIGN 0xF1
#endif

#define F_CPU 9600000UL
#define RxBit PB4
#define Rx _BV(RxBit)

#define UART_SPEED 19200
#define ONECHAR_TICK F_CPU*10/UART_SPEED
#define Rx_ALL_TICK_PACKET F_CPU*13*10/UART_SPEED
#define SERVO_DATA_TIME 3/1000
#define RX_DATA_TIME 17/1000
#define SERVO_DATA_20TICK F_CPU*SERVO_DATA_TIME/20
#define RX_DATA_20TICK F_CPU*RX_DATA_TIME/20
#define SMALL_RX_DATA_TICK20 Rx_ALL_TICK_PACKET/20

typedef struct { uint8_t lo, hi; } bytePair;
#define LoByte(x) ((*(bytePair*) &(x)).lo)
#define HiByte(x) ((*(bytePair*) &(x)).hi)

#define COUNT_DATA 4
#define COUNT_SERVO 4
#define SERVO_BIT (1<<COUNT_SERVO)-1
#define US_TO_4TICK F_CPU/1000000/4

uint8_t readedchar = 0;
uint8_t status_readedchar = 0;
uint16_t readed_buffer[COUNT_DATA];
uint8_t status_servo_data = 0;
uint16_t servo_data[COUNT_DATA];
uint8_t servo_databit[COUNT_SERVO];
uint16_t servo_delay[COUNT_SERVO];

// занимает tick*4T+4+4
void delay_tick(uint16_t tick) {
	asm volatile (
	"L_dl1%=:" "\n\t"
	"sbiw %A0, 1" "\n\t"
	"brne L_dl1%=" "\n\t"
	: "=r" (tick)
	);
}

// RCALL 3 ret 4 + 3nop = 10
void delay_10tick() {
	asm volatile ("nop");
	asm volatile ("nop");
	asm volatile ("nop");
}

// ждать 20 тактов вызов 4, возврат 4, ожидание 12
void delay_20tick() {
	delay_10tick();
	asm volatile ("nop");
	asm volatile ("nop");
	asm volatile ("nop");
}

// N*(1+2)-1+4+3
void delay_250tick() {
	delay_tick(61);
}

// N*(1+2)-1+4+3
void delay_500tick() {
	delay_tick(122);
}

void delay_w20tick(uint16_t w) {
	while(w) {
		delay_10tick();
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		asm volatile ("nop");
		w--;
	}
}

// w*20+20Тактов
uint8_t waitstartpack(uint8_t w) {
	uint8_t r = 0;	
	// В данной реализации 1 тик занимает 20 тактов + 6 тактов (установки) + 9тактов(вызов)
	while((PINB & Rx) && r<w) {
		delay_10tick();
		r++;
	}
	// выровнять вызов до w*20+20Тактов
	asm volatile ("nop");
	asm volatile ("nop");
	asm volatile ("nop");
	asm volatile ("nop");
	asm volatile ("nop");
	return r;
}

// 500 Тактов на бит 5000 на байт 5000/20=250
uint16_t readchar(uint16_t w) {
	status_readedchar = 0;
	// жду стартбита и пока есть возможность прочитать байт
	while((PINB & Rx) && w>250) {
		delay_10tick();
		w--;
	}
	if(w>250) {
		// 1/2 Bit
		delay_tick(59);
		w-=13;
		// все еще стоп бит в середине
		if((PINB & Rx)==0) {
			readedchar = 0;
			uint8_t n = 0;
			while(n<8) {
				delay_tick(119);
				readedchar |= (((PINB>>RxBit)&1) << n);
				n++;
			}
			delay_tick(119);
			w-=225;
			// stop bit
			if((PINB & Rx)) {
				status_readedchar = 1;
			}
		}
	}
	return w;
}

// кратно 20 ожидания
uint16_t readpack(uint16_t w) {
	uint8_t waited;
	status_servo_data = 0;
	// жду 5120 тактов, чтобы была 1, 1.5 символа делать паузу перед посылкой пакета
	waited = waitstartpack(0xFF);
	w-=waited;
	// на вызовы и установки
	w--;
	// пауза соблюдена
	if(waited==0xFF && w>SMALL_RX_DATA_TICK20) {
		// пытаться читать символы
		w = readchar(w);
		// прочитан символ и он 0х10 начало пакета
		if(status_readedchar && readedchar==0x10) {
			w = readchar(w);
			if(status_readedchar && readedchar==SERVOCTRLSIGN) {
				uint8_t i = 0;
				uint8_t crc = 0;
				while(status_readedchar && i<COUNT_DATA) {
					w = readchar(w);
					if(status_readedchar) {
						LoByte(readed_buffer[i]) = readedchar;
						crc+=readedchar;
						w = readchar(w);
						if(status_readedchar) {
							HiByte(readed_buffer[i]) = readedchar;
							crc+=readedchar;
						}
					}
					i++;
				}
				if(status_readedchar) {
					w = readchar(w);
					if(crc==readedchar) {
						for(i = 0;i<COUNT_DATA;i++) {
							servo_data[i] = readed_buffer[i];
						}
						status_servo_data = 1;
					}
				}
			}
		}
	}
	return w;
}

void perform_servobit() {
	// подготовить шаблоны для серво бит c таймингом
	uint8_t i,j,cbit;
	uint16_t c,n;
	c = 0;
	for(i = 0;i<COUNT_SERVO;i++) {
		cbit = 0;
		n = 0xFFFF;
		for(j = 0;j<COUNT_SERVO;j++) {
			if(servo_data[j]>c) {
				cbit|=(1<<j);
				if(servo_data[j]<n) {
					n = servo_data[j];
				}
			}
		}
		if(cbit) {
			servo_delay[i] = (n-c);
			servo_delay[i]*=US_TO_4TICK;
			c = n;
		} else {
			servo_delay[i] = 0;
		}
		servo_databit[i] = cbit;
	}
}
//uint8_t servo_databit[COUNT_SERVO];
//uint8_t servo_delay[COUNT_SERVO];
uint16_t do_servo(uint16_t w) {
	uint8_t i,c;
	c = 0;
	for(i = 0;i<COUNT_SERVO && servo_delay[i];i++) {
		PORTB=(PORTB&(~SERVO_BIT)) | servo_databit[i];
		delay_tick(servo_delay[i]);
		c+=servo_delay[i];
	}
	PORTB=(PORTB&(~SERVO_BIT));
	// 4->20tick
	w-=c*5;
	return w;
}

void main(void) {
    DDRB |= SERVO_BIT;
	uint16_t w;
	while(1) {
		w = RX_DATA_20TICK;
		while(w>SMALL_RX_DATA_TICK20) {
   			PORTB &=  ~_BV( PB3 );
			w = readpack(w);
		}
		delay_w20tick(w);
		perform_servobit();
		w = do_servo(SERVO_DATA_20TICK);
		delay_w20tick(w);
	}
}
