# Home Assignment 1 - LCD and SD Card Example

This repository contains the submission for Home Assignment 1. The main application logic is contained within `main.cpp`.

## Instructions on How to Compile the Code

The project is built using a provided Docker environment to ensure all dependencies are met.

1. Open your terminal and start the Docker environment by running:
   ```bash
   ./build_in_docker.sh
   ```
2. Once inside the container, navigate to the project directory.
3. Create a build directory, configure the project with CMake, and compile it:
   ```bash
   mkdir build
   cd build
   cmake ..
   make -j$(nproc)
   ```
This process will generate a `.uf2` executable file inside the `build` directory.

## Instructions on How to Flash and Run the Code

1. Connect your Raspberry Pi Pico to your computer while holding the **BOOTSEL** button so it mounts as a mass storage device.
2. Using the Linux Mint file manager, drag and drop the compiled `.uf2` file from your `build` directory directly into the mounted Raspberry Pi Pico drive. The Pico will automatically reboot and run the flashed code.
3. To view the serial output, open a new terminal on your host machine and connect via `minicom`:
   ```bash
   minicom -D /dev/ttyACM0 -b 115200
   ```

## Expected Output Structure

* **Hardware Interaction:** The program displays an interface on the connected LCD. You can interact with it by drawing directly on the screen.
* **Saving Data:** When you press the "save" button on the LCD screen, the application processes the input.
* **Serial Output:** After saving, the output times and related execution data will be printed to the terminal via the serial connection (visible in `minicom`).

## The visualizer script
I used this code to visualize the data you will have to extract the data from the SD card using an SD card reader
https://colab.research.google.com/drive/1U6FMriQCytrHC-NzbCYpi5QTAUX-D-fs?usp=sharing
