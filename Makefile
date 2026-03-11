CC      ?= gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2 -D_GNU_SOURCE \
          $(shell pkg-config --cflags libcurl sqlite3)
LDFLAGS = $(shell pkg-config --libs libcurl sqlite3) -lz -lm

SRCS = main.c rinex.c codegen.c almanac.c sqlitedb.c
OBJS = $(SRCS:.c=.o)
BIN  = rinex_dl

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c gps_assist.h rinex.h codegen.h almanac.h sqlitedb.h
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

tests/test_sqlitedb: tests/test_sqlitedb.c sqlitedb.c gps_assist.h sqlitedb.h
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_sqlitedb.c sqlitedb.c \
		$(shell pkg-config --libs sqlite3)

test: tests/test_rinex tests/test_nrf_convert tests/test_almanac tests/test_sqlitedb
	@echo
	./tests/test_rinex
	@echo
	./tests/test_nrf_convert
	@echo
	./tests/test_almanac
	@echo
	./tests/test_sqlitedb
	@echo
	@if command -v php >/dev/null 2>&1; then \
		php tests/test_php_api.php; \
	else \
		echo "(skip PHP API test, php not installed)"; \
	fi

test-integration: tests/test_rinex tests/test_nrf_convert
	@echo
	./tests/test_rinex --integration
	@echo
	./tests/test_nrf_convert

clean:
	rm -f $(OBJS) $(BIN) gps_assist_data.c
	rm -f tests/test_rinex tests/test_nrf_convert tests/test_almanac tests/test_sqlitedb

lint-php:
	@if command -v php >/dev/null 2>&1; then \
		vendor/bin/parallel-lint php/ tests/test_php_api.php && \
		vendor/bin/phpstan analyse; \
	else \
		echo "(skip PHP lint, php not installed)"; \
	fi

.PHONY: all clean test test-integration lint-php
