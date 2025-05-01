# To run ESP-IDF in docker container

The first time you use this you need to set a target (e.g., `esp32c3`):

	docker run -it --rm -v $(pwd):/project -w /project --device /dev/ttyUSB0 \
		espressif/idf:v5.4 idf.py --list-targets

	docker run -it --rm -v $(pwd):/project -w /project --device /dev/ttyUSB0 \
		espressif/idf:v5.4 idf.py set-target _your-target_

Then configure the project. You can do this anytime, but you should
take a look at the configuration options when you start to make sure
you're building what you want.

	docker run -it --rm -v $(pwd):/project -w /project --device /dev/ttyUSB0 \
		espressif/idf:v5.4 idf.py menuconfig

Then you can just build and flash and then "monitor" to connect to the
target to see its output and to interact with its serial console. You
can do this over and over if you make changes to the code.

	docker run -it --rm -v $(pwd):/project -w /project --device /dev/ttyUSB0 \
		espressif/idf:v5.4 idf.py build flash monitor


| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C6 | ESP32-C61 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | --------- | -------- | -------- |

# Hardware Required

* A development board with ESP32/ESP32-S2/ESP32-C3 SoC (e.g., ESP32-DevKitC, ESP-WROVER-KIT, etc.)
* A USB cable for power supply and programming
* WiFi interface
* A SI5351 I2C board cabled to the SCL/SDA pins identified via menuconfig (default is gpio0/gpio1).
