# Build sml2influx.   Raspberry Pi:  make SML_LIB="/usr/lib/libsml.a -luuid"      (libsml of vzlogger, headers in /usr/include/sml)
#                     elsewhere:     make SML_INC=-I<libsml>/sml/include SML_LIB=<objects or libsml.a>
CXX ?= g++
SML_INC ?=
SML_LIB ?= -lsml
CXXFLAGS ?= -std=gnu++11 -O2 -Wall

sml2influx: sml2influx.cpp FrameAssembler.h ../../src/MeterData.h ../../src/SampleAggregator.h
	$(CXX) $(CXXFLAGS) -I../../src $(SML_INC) -o $@ sml2influx.cpp $(SML_LIB) -lm

clean:
	rm -f sml2influx
