# NB : this library part is the lab_1_2 part basically.
# Through this lib, the flash will be able to put the data through all the data treatment processes seen in class and produced in the lab.
# It avoids to add direclty the produced c scripts (quantization, FFT, sobel...).
# Instead lab12_filters.c, lab12_sd.c are called by the main.cpp of the flash.
#
# lab12_filters.c implement data treatment, especially all the data processing given by the lab.
# lab12_sd.c work on the writing into files and then into sd card. Since this process imply to create several files (raw file, quantisize files, sobel )
# 
############ Special action on the PC to have all the data treatments outputs : ################
# - The canny python script is as produced. It's just needed to call it (with the required library) on the image_*_raw.bmp.
# - The FFT python script is also as produced. Neverthless, the pico output doesn't fit exactly the fft script and then requires three actions :
# a/ transform image_*_fft_raw.txt into raw_imu_mcu.txt so it can be read by the FFT script. This file corresponds to raw data printed be the pico and will be analysed through the python script.
# b/ transform image_*_fft_mcu.txt into out_fft_mcu.txt so it can be compared by the FFT python script. This second file corresponds to the results of FFT done by the pico. The python script needs it to compare to make his own FFT calculation and then compare it to the pico results.
# c/ delete raw_imu_mcu.txt 1st row (usually "sample_count=256,sample_spacing_px=1") that is not read by the FFT python script.
# The script can then be called in the folder contianing these two files. It will produces imu_fft_spectrum.png, imu_signal.png.
# these two images are graphic representation. imu_signal.png is the image spectrum before FFT and imu_fft_spectrum.png after. It helps to check what FFT graphically done to the image.

