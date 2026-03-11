# ASN.1 codec libraries for LPP (3GPP TS 37.355) and ULP (OMA SUPL 2.0)
#
# "make" builds both liblpp_asn1.so and libulp_asn1.so from pre-generated code
# "make regenerate" re-runs asn1c on all ASN.1 specs (requires asn1c v1.4+)

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -O2
AR      ?= ar

ASN1C      = asn1c
ASN1C_FLAGS = -fcompound-names -no-gen-OER -no-gen-CBOR -no-gen-APER \
              -no-gen-XER -no-gen-JER -no-gen-print -pdu=all -no-gen-example

WARN_FLAGS = -Wno-unused-parameter -Wno-missing-field-initializers \
             -Wno-unused-variable

# --- LPP (3GPP TS 37.355) ---
LPP_SPECS   = $(wildcard specs/LPP.asn)
LPP_GEN_DIR = generated
LPP_SRCS    = $(wildcard $(LPP_GEN_DIR)/*.c)
LPP_OBJS    = $(LPP_SRCS:.c=.o)
LPP_SONAME  = liblpp_asn1.so

# --- ULP (OMA SUPL 2.0) ---
ULP_SPECS   = $(wildcard specs/ulp/*.asn)
ULP_GEN_DIR = generated-ulp
ULP_SRCS    = $(wildcard $(ULP_GEN_DIR)/*.c)
ULP_OBJS    = $(ULP_SRCS:.c=.o)
ULP_SONAME  = libulp_asn1.so

all: $(LPP_SONAME) $(ULP_SONAME)

$(LPP_SONAME): $(LPP_OBJS)
	$(CC) -shared -Wl,-soname,$@ -o $@ $^

$(ULP_SONAME): $(ULP_OBJS)
	$(CC) -shared -Wl,-soname,$@ -o $@ $^

$(LPP_GEN_DIR)/%.o: $(LPP_GEN_DIR)/%.c
	$(CC) $(CFLAGS) -fPIC -I$(LPP_GEN_DIR) -DASN_DISABLE_OER_SUPPORT \
		$(WARN_FLAGS) -c -o $@ $<

$(ULP_GEN_DIR)/%.o: $(ULP_GEN_DIR)/%.c
	$(CC) $(CFLAGS) -fPIC -I$(ULP_GEN_DIR) -DASN_DISABLE_OER_SUPPORT \
		$(WARN_FLAGS) -c -o $@ $<

regenerate: regenerate-lpp regenerate-ulp

regenerate-lpp: $(LPP_SPECS)
	rm -rf $(LPP_GEN_DIR)
	mkdir -p $(LPP_GEN_DIR)
	cd $(LPP_GEN_DIR) && $(ASN1C) $(ASN1C_FLAGS) $(addprefix ../,$^)
	@echo "Regenerated $(LPP_GEN_DIR)/ from $(LPP_SPECS)"

regenerate-ulp: $(ULP_SPECS)
	rm -rf $(ULP_GEN_DIR)
	mkdir -p $(ULP_GEN_DIR)
	cd $(ULP_GEN_DIR) && $(ASN1C) $(ASN1C_FLAGS) $(addprefix ../,$^)
	@echo "Regenerated $(ULP_GEN_DIR)/ from $(ULP_SPECS)"

clean:
	rm -f $(LPP_GEN_DIR)/*.o $(LPP_SONAME)
	rm -f $(ULP_GEN_DIR)/*.o $(ULP_SONAME)

distclean: clean
	rm -rf $(LPP_GEN_DIR) $(ULP_GEN_DIR)

.PHONY: all clean distclean regenerate regenerate-lpp regenerate-ulp
