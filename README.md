# dbus-ble-sensors

A daemon, part of [Venus](https://github.com/victronenergy/venus/), that reads
BLE (Bluetooth Low Energy) sensor advertisements and publishes it on dbus.  Currently supported sensors are:
- Ruuvi temperature sensors
- Mopeka Pro LPG tank sensors
- Mopeka Pro water tank sensors

## building

This is a [velib](https://github.com/victronenergy/velib/) project.

Besides on velib, it also depends on:

- dbus libs + headers
- libevent libs + headers

For cross-compiling for a Venus device, see
[here](https://www.victronenergy.com/live/open_source:ccgx:setup_development_environment).
And then especially the section about velib projects.

After cloning, and getting the velib submodule, run `make` to build the daemon.  If all goes well, you should have a new dbus-ble-sensors artifact in ./obj


## dbus paths

Tank:

```
com.victronenergy.tank


/Level              0 to 100%
/Remaining          m3
/Status             0=Ok; 1=Disconnected; 2=Short circuited; 3=Reverse polarity; 4=Unknown
/Capacity           m3
/FluidType          0=Fuel; 1=Fresh water; 2=Waste water; 3=Live well; 4=Oil; 5=Black water (sewage)
/Standard           0=European; 1=USA

Note that the FluidType enumeration is kept in sync with NMEA2000 definitions.

Mopeka-specific:
/BatteryVoltage     2.2 to 2.9 volts, usually
/Confidence         1 to 3; The level of confidence the Mopeka sensor has in its measurement
/LevelInMM          The tank level in millimeters
/MopekaSensorTypeId 3=Mopeka Pro LGP sensors; 5=Mopeka Pro water sensors
/RawValueFull       The tank level (mm) when the tank is full
/SyncButton         0 or 1; Whether the Mopeka sensor is in 'Sync' mode
/Temperature        Sensor temperature in degrees Celcius
```

Temperature:

```
com.victronenergy.temperature

/Temperature        degrees Celcius
/Status             0=Ok; 1=Disconnected; 2=Short circuited; 3=Reverse polarity; 4=Unknown
/TemperatureType    0=battery; 1=fridge; 2=generic

Ruuvi-specific:
/AccelX             X acceleration in G           
/AccelY             Y acceleration in G
/AccelZ             Z acceleration in G
/BatteryVoltage     2.2 to 2.9 volts, usually
/Humidity           Percent humidity
/Pressure           Pressure in hPa
/SeqNo              An incrementing ID for each BLE advertisement from this sensor
/TxPower            dBm
```
