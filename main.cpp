/******************************************************************************

******************************************************************************/

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <errno.h>
#include <wiringPiSPI.h>
#include <stdio.h>
#include <unistd.h>

using namespace std;

#define SPI_SPEED 1200000
#define MIN_TRIGGER 10*10

unsigned int sampling_rate_in_ms = 1000;

// channel = 0, 1, 2
unsigned int readADC(int channel) {

   int cs = 0;
   unsigned char buffer[2];

   if (channel == 0) {
      buffer[0] = 0x60;
      buffer[1] = 0x00;
      cs = 0;
   } else if (channel == 1) {
      buffer[0] = 0x70;
      buffer[1] = 0x00;
      cs = 0;
   } else {
      buffer[0] = 0x60;
      buffer[1] = 0x00;
      cs = 1;
   }

   // We write two bits
   /*
      Note that the SPI protocol "returns" the same number of bits as sent,
      and in this case the 10-bit ADC data is encoded in the last two bits
      of the first byte and the eight bits of the second byte. I chose to
      retrieve the data in MSB-first order, so the result is the sum of
      (the bottom two bits of the first byte shifted up by eight bits) + the second byte.
   */
   wiringPiSPIDataRW(cs, buffer, 2);

   unsigned int msb = buffer[1] + ((buffer[0] & 0x03) << 8);
   //unsigned int lsb = buffer[0] + ((buffer[1] & 0x03) << 8);

   return msb;
}

int main(int argc, char *argv[])
{
   int fd, fd2;
   int x,y,z;

   if (argc == 1) {
      printf("Sampling rate is %s\n", argv[1]);
      sampling_rate_in_ms = min(0, max(1000, atoi(argv[1])));
   } else if (argc > 1) {
      printf("Too many arguments supplied. Default sampling rate used.\n");
   }

   cout << "Initializing" << endl;

   // Configure the interface.
   fd = wiringPiSPISetup(0, SPI_SPEED);
   fd2 = wiringPiSPISetup(1, SPI_SPEED);

   cout << "Init first MPC3002 : " << fd << endl;
   cout << "Init second MPC3002 : " << fd2 << endl;

   do {
      x = readADC(0);
      y = readADC(1);
      z = readADC(2);

      // Offset for X/Y (due to MMA7260Q)
      x = x + 9;
      y = y + 467; // 150mV

      // Offset Z for gravity (1G = 800mV at 1.5 precision)
      x = x - 512;
      y = y - 512;
      z = z - 248; // 800mV/(3,3V/1023)

      if (x*x + y*y + z*z > MIN_TRIGGER) {
         printf("%d %d %d\n", x, y, z);
      }

      usleep(sampling_rate_in_ms * 1000);

   } while(true);

   cout << "Exiting" << endl;

}
