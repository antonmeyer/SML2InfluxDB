# sml2influx - SML meter on a serial port of a Raspberry Pi to InfluxDB 2

Same decoder (`src/MeterData.h`) and same data reduction (`src/SampleAggregator.h`) as the ESP32 firmware, so a meter
on the Pi writes the same data as a meter on an ESP32: tag `eMeter`, fields `actW` / `actWmin` / `actWmax` (if the meter
sends the power), `sumWh`, `expWh`, time stamp from the clock of the meter. See `CHANGELOG.md` of the project.

    cd tools/pi
    make SML_LIB="/usr/lib/libsml.a -luuid"      # Raspberry Pi with the libsml of vzlogger (headers in /usr/include/sml)

    ./sml2influx --replay ../../test/smlGHWP_261011.hex     # test with a capture, prints the lines, writes nothing
    ./sml2influx -c /etc/sml2influx.conf --dry-run          # live on the serial port, prints the lines
    ./sml2influx -c /etc/sml2influx.conf                    # live, writes to InfluxDB

Configuration: `sml2influx.conf.example` (token inside: `chmod 600`). Service: `sml2influx.service`.

Tests: `pio test -e native` (frame assembler, decoder, aggregation) and `python3 test_e2e.py ./sml2influx <capture>`
(pseudo terminal as serial port, fake InfluxDB that answers 500 and 400 once).

The service reads only; the serial port must not be used by another program (vzlogger) at the same time.
