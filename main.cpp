/******************************************************************************

******************************************************************************/

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <errno.h>
#include <wiringPiSPI.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

using namespace std;

bool debug_flag = false;

// SPI Speed as per the datasheet of the accelerometer : 1,2 MHz
#define SPI_SPEED 1200000

// Minimum square value for triggering an output
#define MIN_TRIGGER 0.8 // in G

// Coefficient for 10bit/16bit ADC --> G
#define G_COEF_ADXL (3300.0/1024.0)/330.0
#define G_COEF_MMA (3300.0/1024.0)/800.0
#define G_COEF_LIS (12.0/65535.0) //  (6g fullscale range)/(2^16bits) = SCALE

// Offsets are calculated here for faaaaastness (heuristics)

// These are value against 10bit
#define X_OFFSET_ADXL -512 + 4
#define Y_OFFSET_ADXL -512 + 1
#define Z_OFFSET_ADXL -512 - 102 + 55 // Account for gravity (ADXL 1G = 330mV/g)  // 330mV/(3,3V/1023)

// These are value against 10bit
#define X_OFFSET_MMA -512
#define Y_OFFSET_MMA -512
#define Z_OFFSET_MMA -512 - 248// Account for gravity (ADXL 1G = 800mV/g)  // 800mV/(3,3V/1023)

// These are value against 16bit
#define X_OFFSET_LIS -678
#define Y_OFFSET_LIS +17
#define Z_OFFSET_LIS -5359

// Time
#define ONE_OVER_CPS (1000000/CLOCKS_PER_SEC)

// Sampling rate for the ADC
unsigned int sampling_rate_in_us = 1000000; // 1 second

#define FINE_SAMPLING_COUNT 10

// Type of accelerometer
unsigned int type;

// Types of accelerometer
#define TYPE_MMA 1
#define TYPE_ADXL 2
#define TYPE_LIS331 3

// We declare globals for faster reads
unsigned int channel_select;
unsigned char buffer[10];

// channel = 0, 1, 2 <=> X, Y and Z
unsigned int readADC(int channel)
{

   if (type == TYPE_ADXL || type == TYPE_MMA) {
          
      // From https://github.com/WiringPi/WiringPi/blob/master/wiringPi/mcp3002.c
      if (channel == 0) {
         buffer[0] = 0b11010000;
         buffer[1] = 0x00;
         channel_select = 0;
      } else if (channel == 1) {
         buffer[0] = 0b11110000;
         buffer[1] = 0x00;
         channel_select = 0;
      } else {
         buffer[0] = 0b11010000;
         buffer[1] = 0x00;
         channel_select = 1;
      }

      // We write two bits
      wiringPiSPIDataRW(channel_select, buffer, 2);

      return ((buffer [0] << 7) | (buffer [1] >> 1)) & 0x3FF;
   
   } else if (type == TYPE_LIS331) {

      if (channel == 0) {
         buffer[0] = 0b11101000;
      } else if (channel == 1) {
         buffer[0] = 0b11101010;
      } else {
         buffer[0] = 0b11101100;
      }
      buffer[1] = 0x00;
      buffer[2] = 0x00;
      channel_select = 0;

      // We write three bits here — the sensor works a bit differently
      wiringPiSPIDataRW(channel_select, buffer, 3);
      int result = buffer[1] | (buffer[2] << 8);

      // LS331 is in Two's complement for its 16bit
      // https://en.wikipedia.org/wiki/Two%27s_complement
      if (result >= 32768) {
         result -= 65535;
      }

      return result;

   }

   return 0;

}

void parse_args(int argc, char *argv[])
{

   // Check arguments
   if (argc == 1) {
   
      printf("Caribe Wave Sensor Pusher — pushes sensor data to stdout for next stage.\n");
      printf("Usage :\n");
      printf("  ./main [sampling rate] [debug]\n");
      printf("\n");
      printf("Arguments :\n");
      printf("  - type: type of accelerometer : ADXL, LIS or MMA (string).\n");
      printf("  - sampling rate: in milliseconds, between 0 and 1000.\n");
      printf("  - debug: 1 or 0.\n");
      printf("\n");
      exit(0);
   
   } else if (argc == 3) {
   
      sampling_rate_in_us = max(0, min(1000, atoi(argv[2]))) * 1000;
   
   } else if (argc == 4) {

      sampling_rate_in_us = max(0, min(1000, atoi(argv[2]))) * 1000;
      debug_flag = argv[3][0] == '1' ? true : false;

      if (debug_flag) printf("Debug flag is TRUE.\n");
   
   } else if (argc > 4) {
   
      printf("Too many arguments supplied..\n");
      exit(0);
   
   }

   if (strcmp(argv[1],"LIS")==0) {
      type = TYPE_LIS331;
      if (debug_flag) printf("Accel. type is : LIS331 (SPI).\n");
   } else if (strcmp(argv[1],"MMA")==0) {
      type = TYPE_MMA;
      if (debug_flag) printf("Accel. type is : MMA (Analog).\n");
   } else if (strcmp(argv[1],"ADXL")==0) {
      type = TYPE_ADXL;
      if (debug_flag) printf("Accel. type is : ADXL337 (Analog).\n");
   } else {
      printf("Accelerometer type not recognized.\n");
      exit(0);
   }

}

int main(int argc, char *argv[])
{
   // File descriptors for SPI
   int fd_mpc3002_1, fd_mpc3002_2;

   // Data values
   int x, y, z;
   double xg, yg, zg;

   // Running an average on the whole sim
   long x_avg, y_avg, z_avg, count_avg;
   x_avg = 0; y_avg = 0; z_avg = 0; count_avg = 0;

   // For clocking and precision
   clock_t tic, toc;
   unsigned int elapsed_time_in_us;

   parse_args(argc, argv);

   if (debug_flag) printf("Starting up.\n");
   if (debug_flag) printf("Sampling rate will be %d µs (%d ms).\n", sampling_rate_in_us, sampling_rate_in_us / 1000);

   // Configure the interface.
   if (debug_flag) printf("Enabling first MPC3002 ...");
   fd_mpc3002_1 = wiringPiSPISetup(0, SPI_SPEED);
   if (debug_flag) printf(" Done (%d).\n", fd_mpc3002_1);
   usleep(10000);
   
   if (type==TYPE_ADXL || type == TYPE_MMA) {

      if (debug_flag) printf("Enabling second MCP3002 ...");
      fd_mpc3002_2 = wiringPiSPISetup(1, SPI_SPEED);
      if (debug_flag) printf(" Done (%d).\n", fd_mpc3002_2);
      usleep(10000);

   } else if (type == TYPE_LIS331) {

      if (debug_flag) printf("Writing LIS331 registers ...");

      // Enable axis, normal mode (REG_1)
      buffer[0] = 0b00100000;
      buffer[1] = 0b00100111;
      wiringPiSPIDataRW(0, buffer, 2);
      usleep(10000);

      // HP off (REG_2)
      buffer[0] = 0b00100001;
      buffer[1] = 0b00000000;
      wiringPiSPIDataRW(0, buffer, 2);
      usleep(10000);

      // Scale at 6g (REG_4)
      buffer[0] = 0b00100011;
      buffer[1] = 0b00000000;
      wiringPiSPIDataRW(0, buffer, 2);
      usleep(10000);

      if (debug_flag) printf(" Done.\n");

   }

   if (debug_flag) printf("SPI setup ... OK.\n\n");

   do {

      tic = clock();

      x = 0; y = 0; z = 0;
      for (int i = 0; i < FINE_SAMPLING_COUNT; ++i)
      {
         if (type==TYPE_ADXL) {
            x += readADC(0) + X_OFFSET_ADXL;
            y += readADC(1) + Y_OFFSET_ADXL;
            z += readADC(2) + Z_OFFSET_ADXL;
         } else if (type == TYPE_MMA) {
            x += readADC(0) + X_OFFSET_MMA;
            y += readADC(1) + Y_OFFSET_MMA;
            z += readADC(2) + Z_OFFSET_MMA;
         } else if (type == TYPE_LIS331) {
            x += readADC(0) + X_OFFSET_LIS;
            y += readADC(1) + Y_OFFSET_LIS;
            z += readADC(2) + Z_OFFSET_LIS;
         }
      }

      // Average on 1ms approx.
      x = x/FINE_SAMPLING_COUNT;
      y = y/FINE_SAMPLING_COUNT;
      z = z/FINE_SAMPLING_COUNT;

      if (type==TYPE_ADXL) {
         xg = (double) x*G_COEF_ADXL;
         yg = (double) y*G_COEF_ADXL;
         zg = (double) z*G_COEF_ADXL;
      } else if (type == TYPE_MMA) {
         xg = (double) x*G_COEF_MMA;
         yg = (double) y*G_COEF_MMA;
         zg = (double) z*G_COEF_MMA;
      } else if (type == TYPE_LIS331) {
         xg = (double) x*G_COEF_LIS;
         yg = (double) y*G_COEF_LIS;
         zg = (double) z*G_COEF_LIS;
      }

      // 1G on at least one axis triggers the output
      if ( max(xg, max(yg, zg)) > MIN_TRIGGER || debug_flag) {
         // Format : X_10bit X_G Y_10bit Y_G Z_1Obit Z_G
         printf("%d %f %d %f %d %f\n", x, xg, y, yg, z, zg);

         if (debug_flag) {
            x_avg += x; y_avg += y; z_avg += z; 
            count_avg++;
            printf("  [Average since launch %ld %ld %ld]\n", x_avg/count_avg, y_avg/count_avg, z_avg/count_avg);
         }
      }

      toc = clock();
      elapsed_time_in_us = (toc - tic) * ONE_OVER_CPS; // in microsecs
      if (debug_flag) printf("  [Sampling took %d us]\n", elapsed_time_in_us);

      usleep(sampling_rate_in_us - max(0, static_cast<int>(min(elapsed_time_in_us, sampling_rate_in_us))));

   } while(true);

   if (debug_flag) printf("Exiting");

}
