Caribe Wave Sensor pusher
---
 
Retrieves data from different sensors on a Raspberry Pi, and output them to stdout for consumption for the next stage (pheromon, see [this repo](https://github.com/caribewave/pheromon)).


#### Requirements

The [wiringPi](http://wiringpi.com/) library must be installed before hand and the **spi_bcm2835** module must be enabled (and not blacklisted).

To compile the code, we use `g++` **(Raspbian 4.9.2-10) 4.9.2**. A Makefile is provided.

#### Hardware

We use prototyped boards with MPC3002 + analog accelerometers, or SPI enabled accelerometers directly, connected to the SPI pins of a Raspberry Pi B+

#### Supported accelerometers 

Nearly all analog accelerometers should be supported out of the box since they are connected through a **MCP3002** 10bit ADC.

The code has been tested and optimized for the following analog sensors :
 
  - ADXL33 (See https://www.sparkfun.com/products/12803)
  - MMA7260Q (See https://www.sparkfun.com/products/retired/308)

16bit SPI accelerometers should be supported too, if they comply to the LIS331 set of instructions (_which is pretty obscure if you don't read the datasheet carefully_).

The code has been tested and optimized for the following SPI sensors :
  - LIS331 (See https://www.sparkfun.com/products/10345)

#### Operation

    ./main [accelerometer_type] [sampling_delay_in_ms] [trigger] [time span] [debug flag]


###### Arguments

All arguments are mandatory.

  * Accelerometer type : Either "LIS", "MMA" or "ADXL". No default. Uppercase only.
  * Sampling rate : in milliseconds, between **0** and **1000**.
  * Trigger: minimum trigger value in milli _g_.
  * Trigger time span: the time span during which we continue to output data after a trigger, in millis. Must be a multiple of the sampling rate.
  * Debug flag : Either 1 or 0.

Example :

    # Starts the main process in debug mode, with a sampling rate of 1000 milliseconds, for a LIS331 type accelerometer
    ./main LIS 1000 800 1

The sampling rate is ajusted to the time taken to do the actual sampling via a call to `clock()`. The precision is sub-ms.

#### Output format

We output delta values to take into account the fact that the physical device is not a real seismograph (not bolted to the ground, always in the same direction).

We calculate an  <em>l<sup>2</sup>-norm</em> and compare it against the squared trigger value in g<sup>2</sup> to decide if we output the values or not (so as not to clutter the output with irrelevant data).



### Licence

 MIT.