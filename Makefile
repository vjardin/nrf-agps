CC      ?= gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2 -D_GNU_SOURCE \
          $(shell pkg-config --cflags libcurl sqlite3)
LDFLAGS = $(shell pkg-config --libs libcurl sqlite3) -lz -lm

LIB_CFLAGS = $(CFLAGS) -Ilib

LIB_SRCS = lib/rinex.c lib/codegen.c lib/almanac.c lib/sqlitedb.c
LIB_OBJS = $(LIB_SRCS:.c=.o)
BIN      = rinex_dl/rinex_dl

all: $(BIN) supl_server tests/supl_client

$(BIN): rinex_dl/rinex_main.o $(LIB_OBJS)
	$(CC) $(LIB_CFLAGS) -o $@ $^ $(LDFLAGS)

rinex_dl/rinex_main.o: rinex_dl/rinex_main.c lib/gps_assist.h lib/rinex.h \
                        lib/codegen.h lib/almanac.h lib/sqlitedb.h
	$(CC) $(LIB_CFLAGS) -c -o $@ $<

lib/%.o: lib/%.c lib/gps_assist.h lib/rinex.h lib/codegen.h lib/almanac.h \
         lib/sqlitedb.h
	$(CC) $(LIB_CFLAGS) -c -o $@ $<

# --- Tests ---

TEST_CFLAGS = $(CFLAGS) -Ilib -Itests/

tests/test_rinex: tests/test_rinex.c lib/rinex.c lib/gps_assist.h lib/rinex.h
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_rinex.c lib/rinex.c $(LDFLAGS)

tests/test_nrf_convert: tests/test_nrf_convert.c lib/gps_assist_nrf.c \
                        lib/gps_assist.h lib/gps_assist_nrf.h tests/nrf_modem_gnss.h
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_nrf_convert.c lib/gps_assist_nrf.c -lm

tests/test_almanac: tests/test_almanac.c lib/almanac.c lib/gps_assist.h lib/almanac.h
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_almanac.c lib/almanac.c $(LDFLAGS)

tests/test_sqlitedb: tests/test_sqlitedb.c lib/sqlitedb.c lib/gps_assist.h lib/sqlitedb.h
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_sqlitedb.c lib/sqlitedb.c \
		$(shell pkg-config --libs sqlite3)

tests/test_nrf_cloud_cross: tests/test_nrf_cloud_cross.c lib/gps_assist_nrf.c \
                            lib/gps_assist.h lib/gps_assist_nrf.h tests/nrf_modem_gnss.h
	$(CC) $(TEST_CFLAGS) -o $@ tests/test_nrf_cloud_cross.c lib/gps_assist_nrf.c -lm

asn1/liblpp_asn1.so asn1/libulp_asn1.so:
	$(MAKE) -C asn1

LPP_CFLAGS = $(TEST_CFLAGS) -Iasn1/generated -DASN_DISABLE_OER_SUPPORT
LPP_LDFLAGS = -Lasn1 -llpp_asn1 -lm

ULP_CFLAGS = $(TEST_CFLAGS) -Iasn1/generated-ulp -DASN_DISABLE_OER_SUPPORT
ULP_LDFLAGS = -Lasn1 -lulp_asn1
SUPL_LDFLAGS = $(LPP_LDFLAGS) $(ULP_LDFLAGS) \
	$(shell pkg-config --libs sqlite3 libevent libevent_openssl openssl)

tests/test_lpp: tests/test_lpp.c lib/lpp_builder.c lib/lpp_builder.h lib/gps_assist.h \
                asn1/liblpp_asn1.so
	$(CC) $(LPP_CFLAGS) -o $@ tests/test_lpp.c lib/lpp_builder.c $(LPP_LDFLAGS)

# --- SUPL server ---

supl_server: supl/supl_main.c supl/supl_server.c lib/supl_codec.c lib/lpp_builder.c \
             lib/sqlitedb.c supl/supl_server.h lib/supl_codec.h lib/lpp_builder.h \
             lib/sqlitedb.h lib/gps_assist.h asn1/liblpp_asn1.so asn1/libulp_asn1.so
	$(CC) $(LPP_CFLAGS) $(ULP_CFLAGS) -Isupl -o $@ \
		supl/supl_main.c supl/supl_server.c lib/supl_codec.c lib/lpp_builder.c \
		lib/sqlitedb.c $(SUPL_LDFLAGS)

tests/supl_client: tests/supl_client.c asn1/libulp_asn1.so asn1/liblpp_asn1.so
	$(CC) $(LPP_CFLAGS) $(ULP_CFLAGS) -o $@ tests/supl_client.c \
		$(ULP_LDFLAGS) $(LPP_LDFLAGS) $(shell pkg-config --libs openssl libcjson)

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

test-supl: rinex_dl supl_server tests/supl_client
	@echo
	@echo "=== SUPL structural comparison test ==="
	@sh tests/test_supl_compare.sh

test-integration: tests/test_rinex tests/test_nrf_convert
	@echo
	./tests/test_rinex --integration
	@echo
	./tests/test_nrf_convert

clean:
	rm -f lib/*.o rinex_dl/*.o
	rm -f rinex_dl/rinex_dl supl_server gps_assist_data.c
	rm -f tests/test_rinex tests/test_nrf_convert tests/test_almanac tests/test_sqlitedb \
	     tests/test_nrf_cloud_cross tests/test_lpp tests/supl_client
	$(MAKE) -C asn1 distclean

lint-php:
	@if command -v php >/dev/null 2>&1; then \
		php/vendor/bin/parallel-lint php/ tests/test_php_api.php tests/test_nrf_cloud.php && \
		cd php && vendor/bin/phpstan analyse; \
	else \
		echo "(skip PHP lint, php not installed)"; \
	fi

.PHONY: all clean test test-integration test-supl lint-php
