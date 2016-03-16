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
bool first_loop = true;

// SPI Speed as per the datasheet of the accelerometer : 1,2 MHz
#define SPI_SPEED 1200000

// Minimum g value for triggering an output
double min_trigger_in_square_g = 0.64; // default : 800mg^2 = 0,64g

// Coefficient for 10bit/16bit ADC --> G
#define G_COEF_ADXL (3300.0/1024.0)/330.0 // (VCC/10bit)/330mv_per_g
#define G_COEF_MMA (3300.0/1024.0)/800.0 // (VCC/10bit)/800mv_per_g
#define G_COEF_LIS (12.0/65535.0) //  (6g fullscale range)/(2^16bits) = SCALE

// Divide in advance for faster loops
#define ONE_OVER_CPS (1000000.0/CLOCKS_PER_SEC)

// Sampling rate for the ADC, in microsecs
unsigned int sampling_rate_in_us = 1000000; // 1 second

#define FINE_SAMPLING_COUNT 10

unsigned int samples_count = 1; // Number of samples to output after a trigger, default 1
bool keep_outputting = false;

// Global type of accelerometer
unsigned int type;

// Types of supported accelerometers
#define TYPE_MMA 1
#define TYPE_ADXL 2
#define TYPE_LIS331 3

// We declare globals for faster reads in readADC
unsigned int channel_select;
unsigned char buffer[10];
int spi_result;

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
      spi_result = buffer[1] | (buffer[2] << 8);

      // LS331 is in Two's complement for its 16bit
      // https://en.wikipedia.org/wiki/Two%27s_complement
      if (spi_result >= 32768) {
         spi_result -= 65535;
      }

      return spi_result;

   }

   return 0;

}

void parse_args(int argc, char *argv[])
{

   // Check arguments
   if (argc != 6) {
   
      fprintf(stdout, "Caribe Wave Sensor Pusher — pushes sensor data to stdout for next stage.\n");
      fprintf(stdout, "Usage :\n");
      fprintf(stdout, "  ./main [type] [sampling rate] [trigger] [time span] [debug]\n");
      fprintf(stdout, "\n");
      fprintf(stdout, "Arguments :\n");
      fprintf(stdout, "  - type: type of accelerometer : ADXL, LIS or MMA (string).\n");
      fprintf(stdout, "  - sampling rate: in milliseconds, between 0 and 1000.\n");
      fprintf(stdout, "  - trigger: trigger value in millig (max 1sec).\n");
      fprintf(stdout, "  - trigger time span: span time during which we keep outputting data after a trigger (max 2min).\n");
      fprintf(stdout, "  - debug: 1 or 0.\n");
      fprintf(stdout, "\n");
      exit(0);
   
   }

   debug_flag = argv[5][0] == '1' ? true : false;
   if (debug_flag) fprintf(stdout, "Debug flag is TRUE.\n");

   if (strcmp(argv[1],"LIS")==0) {
      type = TYPE_LIS331;
      if (debug_flag) fprintf(stdout, "Accel. type is : LIS331 (SPI).\n");
   } else if (strcmp(argv[1],"MMA")==0) {
      type = TYPE_MMA;
      if (debug_flag) fprintf(stdout, "Accel. type is : MMA (Analog).\n");
   } else if (strcmp(argv[1],"ADXL")==0) {
      type = TYPE_ADXL;
      if (debug_flag) fprintf(stdout, "Accel. type is : ADXL337 (Analog).\n");
   } else {
      fprintf(stdout, "Accelerometer type not recognized.\n");
      exit(0);
   }

   sampling_rate_in_us = max(0, min(1000, atoi(argv[2]))) * 1000;
   if (debug_flag) fprintf(stdout, "Sampling rate will be %d µs (%d ms).\n", sampling_rate_in_us, sampling_rate_in_us / 1000);

   int min_trigger_in_millig = max(0, min(6000, atoi(argv[3])));
   min_trigger_in_square_g = (min_trigger_in_millig/1000.0)*(min_trigger_in_millig/1000.0);
   if (debug_flag) fprintf(stdout, "Minimum trigger is %d mg.\n", min_trigger_in_millig);

   int time_span_in_ms = max(0, min(120000, atoi(argv[4])));
   samples_count = 1000 * time_span_in_ms / sampling_rate_in_us;
   if (debug_flag) fprintf(stdout, "Time span is %d ms (%d samples per trigger).\n", time_span_in_ms, samples_count);
}

int main(int argc, char *argv[])
{
   // File descriptors for SPI
   int fd_mpc3002_1, fd_mpc3002_2;

   // Data values
   int x, y, z;
   x = 0; y = 0; z = 0;
   int delta_x, delta_y, delta_z;
   double delta_xg, delta_yg, delta_zg;

   // Triggered or not
   bool triggered = false;
   unsigned int sample_number = 0;

   // For clocking and precision
   clock_t tic, toc;
   unsigned int elapsed_time_in_us;

   parse_args(argc, argv);

   if (debug_flag) fprintf(stdout, "Starting up.\n");

   // Configure the interface.
   if (debug_flag) fprintf(stdout, "Enabling first MPC3002 ...");
   fd_mpc3002_1 = wiringPiSPISetup(0, SPI_SPEED);
   if (debug_flag) fprintf(stdout, " Done (%d).\n", fd_mpc3002_1);
   usleep(10000);
   
   if (type==TYPE_ADXL || type == TYPE_MMA) {

      if (debug_flag) fprintf(stdout, "Enabling second MCP3002 ...");
      fd_mpc3002_2 = wiringPiSPISetup(1, SPI_SPEED);
      if (debug_flag) fprintf(stdout, " Done (%d).\n", fd_mpc3002_2);
      usleep(10000);

   } else if (type == TYPE_LIS331) {

      if (debug_flag) fprintf(stdout, "Writing LIS331 registers ...");

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

      if (debug_flag) fprintf(stdout, " Done.\n");

   }

   if (debug_flag) fprintf(stdout, "SPI setup ... Done.\n\n");

   fflush(stdout);

   do {

      tic = clock();

      delta_x = x;
      delta_y = y;
      delta_z = z;

      x = 0; y = 0; z = 0;
      for (int i = 0; i < FINE_SAMPLING_COUNT; ++i)
      {
         x += readADC(0);
         y += readADC(1);
         z += readADC(2);
      }

      // Average on 1ms approx.
      x = x/FINE_SAMPLING_COUNT;
      y = y/FINE_SAMPLING_COUNT;
      z = z/FINE_SAMPLING_COUNT;

      // Calculate deltas
      delta_x = x - delta_x;
      delta_y = y - delta_y;
      delta_z = z - delta_z;

      if (type==TYPE_ADXL) {
         delta_xg = (double) delta_x*G_COEF_ADXL;
         delta_yg = (double) delta_y*G_COEF_ADXL;
         delta_zg = (double) delta_z*G_COEF_ADXL;
      } else if (type == TYPE_MMA) {
         delta_xg = (double) delta_x*G_COEF_MMA;
         delta_yg = (double) delta_y*G_COEF_MMA;
         delta_zg = (double) delta_z*G_COEF_MMA;
      } else if (type == TYPE_LIS331) {
         delta_xg = (double) delta_x*G_COEF_LIS;
         delta_yg = (double) delta_y*G_COEF_LIS;
         delta_zg = (double) delta_z*G_COEF_LIS;
      }

      triggered = delta_xg*delta_xg + delta_yg*delta_yg + delta_zg*delta_zg > min_trigger_in_square_g;

      if (debug_flag) {
         fprintf(stdout, "[%s] [%s] 𝝙X %05d %1.4f 𝝙Y %05d %1.4f 𝝙Z %05d %1.4f \n", triggered ? "X" : "_", keep_outputting? "X" : "_", delta_x, delta_xg, delta_y, delta_yg, delta_z, delta_zg);
      }

      // 1G on at least one axis triggers the output,
      // or if we are outputting a burst (keep_outputting = true)
      if ((triggered && !first_loop) || keep_outputting) {
      
         if (triggered) {
            sample_number = 1; // Reset for each trigger, to have a rolling behavior
         } else {
            sample_number++;
         }

         fprintf(stdout, "%1.4f %1.4f %1.4f\n", delta_xg, delta_yg, delta_zg);

      } else {
         // Sample = 0 => we have not triggered yet
         sample_number = 0;
      }

      first_loop = false;
      fflush(stdout);

      toc = clock();

      // Keep a first clock to know if we need to keep outputting
      // the data or not
      keep_outputting = (sample_number != 0) && (sample_number < samples_count);

      // Keep a clock to know how much deviation we need to apply to 
      // the standard sampling_rate
      elapsed_time_in_us = (toc - tic) * ONE_OVER_CPS; // in microsecs

      if (debug_flag) fprintf(stdout, "        [Sampling took %d us]\n", elapsed_time_in_us);

      usleep(sampling_rate_in_us - max(0, static_cast<int>(min(elapsed_time_in_us, sampling_rate_in_us))));

   } while(true);

   if (debug_flag) fprintf(stdout, "Exiting");

}
