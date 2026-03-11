# ASN.1 codec library for 3GPP LPP (TS 37.355)
#
# "make" builds liblpp_asn1.so from pre-generated code in generated/
# "make regenerate" re-runs asn1c on the ASN.1 specs (requires asn1c v1.4+)

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -O2
AR      ?= ar

ASN1C      = asn1c
ASN1C_FLAGS = -fcompound-names -no-gen-OER -no-gen-CBOR -no-gen-APER \
              -no-gen-XER -no-gen-JER -no-gen-print -pdu=all -no-gen-example

SPECS   = $(wildcard specs/*.asn)
GEN_DIR = generated
SRCS    = $(wildcard $(GEN_DIR)/*.c)
OBJS    = $(SRCS:.c=.o)
SONAME  = liblpp_asn1.so
LIB     = $(SONAME)

all: $(LIB)

$(LIB): $(OBJS)
	$(CC) -shared -Wl,-soname,$(SONAME) -o $@ $^

$(GEN_DIR)/%.o: $(GEN_DIR)/%.c
	$(CC) $(CFLAGS) -fPIC -I$(GEN_DIR) -DASN_DISABLE_OER_SUPPORT \
		-Wno-unused-parameter -Wno-missing-field-initializers \
		-Wno-unused-variable -c -o $@ $<

regenerate: $(SPECS)
	rm -rf $(GEN_DIR)
	mkdir -p $(GEN_DIR)
	cd $(GEN_DIR) && $(ASN1C) $(ASN1C_FLAGS) $(addprefix ../,$^)
	@echo "Regenerated $(GEN_DIR)/ from $(SPECS)"

clean:
	rm -f $(GEN_DIR)/*.o $(LIB)

distclean: clean
	rm -rf $(GEN_DIR)

.PHONY: all clean distclean regenerate
