# Host tests for the logic that has no Arduino dependency.
#   make -C test        build and run
CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O1 -I../src

run: host_tests
	./host_tests

host_tests: host_tests.cpp ../src/apps.cpp ../src/dns_parse.cpp ../src/adblock.cpp ../src/shaper.h ../src/blocklist_data.h
	$(CXX) $(CXXFLAGS) -o $@ host_tests.cpp ../src/apps.cpp ../src/dns_parse.cpp ../src/adblock.cpp

clean:
	rm -f host_tests
