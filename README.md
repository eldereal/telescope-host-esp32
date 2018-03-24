# ESP32 controller for custom telescope hardware

This contoller will auto connect to a pre-configured WiFi AP. Provide UDP interfaces to control the equatorial mount.

With the [ASCOM driver](https://github.com/eldereal/telescope-driver-ascom), this controller can work with many widely-used astronomy softwares.

## Requirements
* ESP32 hardware with On-board WiFi support. (Tested on an ESP8266 based board, [Widora AIR](http://wiki.widora.cn/air)) 
* Stepper motors to control your equatorial mount. (Tested on Sky-Watcher EQ3 DMD Upgrade motors)
* Stepper driver board. (Tested on an [A4988 based board](https://detail.tmall.com/item.htm?id=531992529887&spm=a1z09.2.0.0.16442e8dUdhvcv&_u=o1l9lrs1642))
* Breadborards, wires, 12v DC adapter.
* [ESP-IDF](https://github.com/espressif/esp-idf) development environment