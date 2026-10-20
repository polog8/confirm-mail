# Host-side unit tests for the portable core (no Arduino needed).
#   make -C test/host
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined
SRC = ../../src/core/civil.cpp ../../src/core/tz.cpp ../../src/core/ics.cpp

all: run

test_core: test_core.cpp $(SRC) ../../src/core/*.h
	$(CXX) $(CXXFLAGS) -o $@ test_core.cpp $(SRC)

ics_dump: ics_dump.cpp $(SRC) ../../src/core/*.h
	$(CXX) $(CXXFLAGS) -o $@ ics_dump.cpp $(SRC)

run: test_core
	./test_core

clean:
	rm -f test_core ics_dump

.PHONY: all run clean
