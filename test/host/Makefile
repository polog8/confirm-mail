# Host-side unit tests for the portable core (no Arduino needed).
#   make -C test/host
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined
SRC = ../../src/core/civil.cpp ../../src/core/tz.cpp ../../src/core/ics.cpp ../../src/core/caldav.cpp

all: run

test_core: test_core.cpp $(SRC) ../../src/core/*.h
	$(CXX) $(CXXFLAGS) -o $@ test_core.cpp $(SRC)

test_caldav: test_caldav.cpp $(SRC) ../../src/core/*.h
	$(CXX) $(CXXFLAGS) -o $@ test_caldav.cpp $(SRC)

caldav_live: caldav_live.cpp $(SRC) ../../src/core/*.h
	$(CXX) $(CXXFLAGS) -o $@ caldav_live.cpp $(SRC)

ics_dump: ics_dump.cpp $(SRC) ../../src/core/*.h
	$(CXX) $(CXXFLAGS) -o $@ ics_dump.cpp $(SRC)

run: test_core test_caldav
	./test_core
	./test_caldav

clean:
	rm -f test_core test_caldav caldav_live ics_dump

.PHONY: all run clean
