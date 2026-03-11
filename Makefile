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

tests/test_nrf_cloud_cross: tests/test_nrf_cloud_cross.c gps_assist_nrf.c \
                            gps_assist.h gps_assist_nrf.h tests/nrf_modem_gnss.h
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_nrf_cloud_cross.c gps_assist_nrf.c -lm

asn1/liblpp_asn1.so:
	$(MAKE) -C asn1

LPP_CFLAGS = $(TEST_CFLAGS) -Iasn1/generated -DASN_DISABLE_OER_SUPPORT
LPP_LDFLAGS = -Lasn1 -llpp_asn1 -lm

tests/test_lpp: tests/test_lpp.c lpp_builder.c lpp_builder.h gps_assist.h \
                asn1/liblpp_asn1.so
	$(CC) $(LPP_CFLAGS) -o $@ tests/test_lpp.c lpp_builder.c $(LPP_LDFLAGS)

test: tests/test_rinex tests/test_nrf_convert tests/test_almanac tests/test_sqlitedb \
      tests/test_nrf_cloud_cross tests/test_lpp
	@echo
	./tests/test_rinex
	@echo
	./tests/test_nrf_convert
	@echo
	./tests/test_almanac
	@echo
	./tests/test_sqlitedb
	@echo
	LD_LIBRARY_PATH=asn1 ./tests/test_lpp
	@echo
	@if command -v php >/dev/null 2>&1; then \
		php tests/test_php_api.php; \
		echo; \
		php tests/test_nrf_cloud.php; \
		echo; \
		php tests/gen_nrf_cloud_binary.php /tmp/test_nrf_cloud_cross.bin && \
		./tests/test_nrf_cloud_cross /tmp/test_nrf_cloud_cross.bin && \
		rm -f /tmp/test_nrf_cloud_cross.bin; \
	else \
		echo "(skip PHP tests, php not installed)"; \
	fi

test-integration: tests/test_rinex tests/test_nrf_convert
	@echo
	./tests/test_rinex --integration
	@echo
	./tests/test_nrf_convert

clean:
	rm -f $(OBJS) $(BIN) gps_assist_data.c
	rm -f tests/test_rinex tests/test_nrf_convert tests/test_almanac tests/test_sqlitedb \
	     tests/test_nrf_cloud_cross tests/test_lpp
	$(MAKE) -C asn1 clean

lint-php:
	@if command -v php >/dev/null 2>&1; then \
		vendor/bin/parallel-lint php/ tests/test_php_api.php tests/test_nrf_cloud.php && \
		vendor/bin/phpstan analyse; \
	else \
		echo "(skip PHP lint, php not installed)"; \
	fi

.PHONY: all clean test test-integration lint-php
