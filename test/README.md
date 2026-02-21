
This directory contains the test suite for the `flaputils` library.

### How to run the test on Native Platform

#### Option A: PlatformIO (Recommended)
1. Ensure you have PlatformIO installed (`pip install platformio`).
2. Build the native environment:
   ```bash
   cd ..
   pio run -e native
   ```
3. Run the compiled program:
   ```bash
   cd ..
   .pio/build/native/program
   ```
   *Note: Ensure you are in the project root directory so the program can find `spiffs_data/flapDescriptor.json`.*

#### Option B: Plain g++
1. Install `cJSON` (e.g., `sudo apt-get install libcjson-dev` on Ubuntu).
2. Compile from the project root:
   ```bash
   cd ..
   g++ -std=c++17 -DNATIVE_TEST_BUILD -Isrc \
       test/test_flaputils.cpp src/flaputils.cpp \
       -lcjson -o test_flaputils
   ```
3. Run the executable:
   ```bash
   cd ..
   ./test_flaputils
   ```

### Notes
- The test loads data from `spiffs_data/flapDescriptor.json`.
- It verifies empty mass, flap symbol lookup, and optimal flap interpolation.
- The same test file can also be run on ESP-IDF targets.
