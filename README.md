Caribe Wave Sensor pusher
---
 
Retrieves data from different sensors on a Raspberry Pi, and output them to stdout for consumption for the next stage (pheromon, see [this repo](https://github.com/caribewave/pheromon)).


#### Requirements

The (http://wiringpi.com/)[wiringPi] library must be installed before hand and the **spi_bcm2835** module must be enabled (and not blacklisted).

#### Operation

    ./main [accelerometer_type] [sampling_delay_in_ms] [debug flag]

Example :

    # Starts the main process in debug mode, with a sampling rate of 1000 milliseconds, for a LIS331 type accelerometer
    ./main LIS 1000 1

The sampling rate is ajusted to the time taken to do the actual sampling via a call to `clock()`.

Gravity is already offset (_-1g_ on the Z axis).

#### Output format

The output format is as follows :

    (int)X_raw (double)X_accel (int)Y_raw (double)Y_accel (int)Z_raw (double)Z_accel


  * `*_raw` is the raw 10bit value from the accelerometer
  * `*_accel` is the computed acceleration value in G

The output will only trigger if one of the direction has a value over **1G**.

#### Arguments

  * Accelerometer type : Either "LIS", "MMA" or "ADXL". No default. Uppercase only.
  * Sampling rate : in milliseconds, between **0** and **1000**.
  * Debug flag : Either 1 or 0. 0 if omitted (_false_)

### Licence

 MIT.