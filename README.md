This program reads IMU sensor at the rate about 570 Hz and writes this raw accelerometer
and gyroscope data to SD-card with a timestamp in front of it. The statistics are printed out to terminal.

To achieve the required speed of data reading and transfering, the data is collected into a 2 kB buffer. Once this is filled, the whole buffer will be written together to the SD-card. 

The Statistics are only printed out once for every N = 64 samples (defined in the beginning of main.c).
Only accelerometer values are used to keep printouts shorter. For every 64 samples (a mean value for further calculations) variance and standard deviation is calculated. The printout also includes the maximum value. 

This kind of data could be used to detect patterns of movement like steps or falls. The reason not to write it to SD-card is that this data will be further processed and it can be recalculated from raw data. Alternatively only the calculated data could be written as well so this is just my choice really.

