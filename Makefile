CC      ?= gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2 -D_GNU_SOURCE \
          $(shell pkg-config --cflags libcurl)
LDFLAGS = $(shell pkg-config --libs libcurl) -lz -lm

SRCS = main.c rinex.c codegen.c almanac.c
OBJS = $(SRCS:.c=.o)
BIN  = rinex_dl

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c gps_assist.h rinex.h codegen.h almanac.h
	$(CC) $(CFLAGS) -c $<

# --- Tests ---

TEST_CFLAGS = $(CFLAGS) -I . -I tests/

tests/test_rinex: tests/test_rinex.c rinex.c gps_assist.h rinex.h
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_rinex.c rinex.c $(LDFLAGS)

tests/test_nrf_convert: tests/test_nrf_convert.c gps_assist_nrf.c \
                        gps_assist.h gps_assist_nrf.h tests/nrf_modem_gnss.h
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_nrf_convert.c gps_assist_nrf.c -lm

tests/test_almanac: tests/test_almanac.c almanac.c gps_assist.h almanac.h
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_almanac.c almanac.c $(LDFLAGS)

test: tests/test_rinex tests/test_nrf_convert tests/test_almanac
	@echo
	./tests/test_rinex
	@echo
	./tests/test_nrf_convert
	@echo
	./tests/test_almanac

test-integration: tests/test_rinex tests/test_nrf_convert
	@echo
	./tests/test_rinex --integration
	@echo
	./tests/test_nrf_convert

clean:
	rm -f $(OBJS) $(BIN) gps_assist_data.c
	rm -f tests/test_rinex tests/test_nrf_convert tests/test_almanac

.PHONY: all clean test test-integration
