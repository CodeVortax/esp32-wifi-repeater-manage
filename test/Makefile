# Host tests for the logic that has no Arduino dependency.
#   make -C test        build and run
CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O1 -I../src

run: host_tests
	./host_tests

host_tests: host_tests.cpp ../src/apps.cpp ../src/dns_parse.cpp ../src/shaper.h
	$(CXX) $(CXXFLAGS) -o $@ host_tests.cpp ../src/apps.cpp ../src/dns_parse.cpp

clean:
	rm -f host_tests
