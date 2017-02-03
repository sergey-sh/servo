# servo
Servo controller at atiny13a

Control 4 servo at 1 chip, receive data from UART RX Line at 19200 8N1 on PB4. 

Servo connect on PB0-PB3.

Data pack from UART 0x10 SERVOCTRLSIGN (0xF1) DATA (4 unit16_t) CRC (SUM all byte from DATA).

DATA - is unit16_t = usec*1.2. 

Example:
0x10
0xF1
0xDC
0x05
0x00
0x00
0x00
0x00
0x00
0x00
0xE1

First servo conected to PB0 receive 1250usec pulse avery 20000usec. Other 3 servo receive 0.

I am use avrdude usbtiny for program attiny13.

Fuses: h:FF l:7A
avrdude -p attiny13 -P usb -c usbtiny -e -U lfuse:w:0x7A:m -U hfuse:w:0xff:m


