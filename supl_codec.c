/*
 * supl_codec.c - SUPL/ULP message encode/decode for SUPL 2.0
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <stdlib.h>
#include <string.h>

#include "supl_codec.h"

#include "ULP-PDU.h"
#include "UlpMessage.h"
#include "Version.h"
#include "SessionID.h"
#include "SetSessionID.h"
#include "SlpSessionID.h"
#include "SETId.h"
#include "SLPAddress.h"
#include "IPAddress.h"
#include "SUPLSTART.h"
#include "SUPLRESPONSE.h"
#include "SUPLPOSINIT.h"
#include "SUPLPOS.h"
#include "SUPLEND.h"
#include "PosPayLoad.h"
#include "Ver2-PosPayLoad-extension.h"
#include "PosMethod.h"
#include "StatusCode.h"

/* SUPL 2.0 version */
#define ULP_VER_MAJ 2
#define ULP_VER_MIN 0
#define ULP_VER_SERVIND 0

/*
 * Encode a ULP PDU to UPER with 2-byte big-endian length prefix.
 * The length field in the PDU is set to the total encoded size.
 */
static int encode_ulp_pdu(ULP_PDU_t *pdu, uint8_t **out_buf, size_t *out_len)
{
	/*
	 * First encode to get the size, then set the length field and
	 * re-encode. The length includes itself (the full PDU byte count).
	 */
	void *buf_ptr = NULL;
	ssize_t enc_len;

	/* Encode with placeholder length to measure */
	pdu->length = 0;
	enc_len = uper_encode_to_new_buffer(&asn_DEF_ULP_PDU, NULL,
					    pdu, &buf_ptr);
	if (enc_len < 0)
		return -1;
	free(buf_ptr);
	buf_ptr = NULL;

	/* Set actual length and re-encode */
	pdu->length = (long)enc_len;
	enc_len = uper_encode_to_new_buffer(&asn_DEF_ULP_PDU, NULL,
					    pdu, &buf_ptr);
	if (enc_len < 0)
		return -1;

	*out_buf = buf_ptr;
	*out_len = (size_t)enc_len;
	return 0;
}

static Version_t *make_version(void)
{
	Version_t *v = calloc(1, sizeof(*v));

	if (!v)
		return NULL;
	v->maj = ULP_VER_MAJ;
	v->min = ULP_VER_MIN;
	v->servind = ULP_VER_SERVIND;
	return v;
}

static SessionID_t *make_session_id(long set_session_id,
				    const uint8_t slp_session_id[4],
				    const uint8_t *set_id_buf,
				    size_t set_id_len,
				    int set_id_type)
{
	SessionID_t *sid = calloc(1, sizeof(*sid));

	if (!sid)
		return NULL;

	/* SET session ID */
	SetSessionID_t *set_sid = calloc(1, sizeof(*set_sid));

	if (!set_sid) {
		free(sid);
		return NULL;
	}
	set_sid->sessionId = set_session_id;

	SETId_t *set_id = calloc(1, sizeof(*set_id));

	if (!set_id) {
		free(set_sid);
		free(sid);
		return NULL;
	}

	/* Copy the SET ID from the client's message */
	set_id->present = set_id_type;
	switch (set_id_type) {
	case SETId_PR_msisdn:
		OCTET_STRING_fromBuf(&set_id->choice.msisdn,
				     (const char *)set_id_buf,
				     (int)set_id_len);
		break;
	case SETId_PR_imsi:
		OCTET_STRING_fromBuf(&set_id->choice.imsi,
				     (const char *)set_id_buf,
				     (int)set_id_len);
		break;
	case SETId_PR_iPAddress: {
		IPAddress_t *ip = calloc(1, sizeof(*ip));

		if (!ip) {
			free(set_id);
			free(set_sid);
			free(sid);
			return NULL;
		}
		if (set_id_len == 4) {
			ip->present = IPAddress_PR_ipv4Address;
			OCTET_STRING_fromBuf(&ip->choice.ipv4Address,
					     (const char *)set_id_buf, 4);
		} else {
			ip->present = IPAddress_PR_ipv6Address;
			OCTET_STRING_fromBuf(&ip->choice.ipv6Address,
					     (const char *)set_id_buf, 16);
		}
		set_id->choice.iPAddress = ip;
		break;
	}
	default:
		/* Fallback: use IMSI with provided bytes */
		set_id->present = SETId_PR_imsi;
		OCTET_STRING_fromBuf(&set_id->choice.imsi,
				     (const char *)set_id_buf,
				     (int)set_id_len);
		break;
	}

	set_sid->setId = set_id;
	sid->setSessionID = set_sid;

	/* SLP session ID */
	SlpSessionID_t *slp_sid = calloc(1, sizeof(*slp_sid));

	if (!slp_sid) {
		/* sid will be freed by caller via ASN_STRUCT_FREE */
		return sid;
	}
	OCTET_STRING_fromBuf(&slp_sid->sessionID,
			     (const char *)slp_session_id, 4);

	/* SLP address: use IPv4 loopback as placeholder */
	SLPAddress_t *slp_addr = calloc(1, sizeof(*slp_addr));

	if (slp_addr) {
		IPAddress_t *slp_ip = calloc(1, sizeof(*slp_ip));

		if (slp_ip) {
			slp_ip->present = IPAddress_PR_ipv4Address;
			uint8_t lo[] = {127, 0, 0, 1};

			OCTET_STRING_fromBuf(&slp_ip->choice.ipv4Address,
					     (const char *)lo, 4);
			slp_addr->present = SLPAddress_PR_iPAddress;
			slp_addr->choice.iPAddress = slp_ip;
			slp_sid->slpId = slp_addr;
		} else {
			free(slp_addr);
		}
	}

	sid->slpSessionID = slp_sid;
	return sid;
}

ULP_PDU_t *supl_decode_pdu(const uint8_t *buf, size_t len)
{
	ULP_PDU_t *pdu = NULL;
	asn_dec_rval_t dec;

	dec = uper_decode_complete(NULL, &asn_DEF_ULP_PDU,
				   (void **)&pdu, buf, len);
	if (dec.code != RC_OK) {
		ASN_STRUCT_FREE(asn_DEF_ULP_PDU, pdu);
		return NULL;
	}
	return pdu;
}

void supl_pdu_free(ULP_PDU_t *pdu)
{
	if (pdu)
		ASN_STRUCT_FREE(asn_DEF_ULP_PDU, pdu);
}

int supl_get_message_type(const ULP_PDU_t *pdu)
{
	if (!pdu || !pdu->message)
		return 0;
	return pdu->message->present;
}

long supl_get_set_session_id(const ULP_PDU_t *pdu)
{
	if (!pdu || !pdu->sessionID || !pdu->sessionID->setSessionID)
		return -1;
	return pdu->sessionID->setSessionID->sessionId;
}

int supl_build_response(long set_session_id,
			const uint8_t slp_session_id[4],
			const uint8_t *set_id_buf, size_t set_id_len,
			int set_id_type,
			uint8_t **out_buf, size_t *out_len)
{
	ULP_PDU_t *pdu = calloc(1, sizeof(*pdu));

	if (!pdu)
		return -1;

	int ret = -1;

	pdu->version = make_version();
	pdu->sessionID = make_session_id(set_session_id, slp_session_id,
					 set_id_buf, set_id_len, set_id_type);

	UlpMessage_t *msg = calloc(1, sizeof(*msg));

	if (!msg)
		goto cleanup;
	pdu->message = msg;

	SUPLRESPONSE_t *resp = calloc(1, sizeof(*resp));

	if (!resp)
		goto cleanup;

	/* Use agnssSETbased: we provide assistance data, SET computes pos */
	resp->posMethod = PosMethod_ver2_agnssSETbased;

	msg->present = UlpMessage_PR_msSUPLRESPONSE;
	msg->choice.msSUPLRESPONSE = resp;

	ret = encode_ulp_pdu(pdu, out_buf, out_len);

cleanup:
	ASN_STRUCT_FREE(asn_DEF_ULP_PDU, pdu);
	return ret;
}

int supl_build_pos(long set_session_id,
		   const uint8_t slp_session_id[4],
		   const uint8_t *set_id_buf, size_t set_id_len,
		   int set_id_type,
		   const uint8_t *lpp_buf, size_t lpp_len,
		   uint8_t **out_buf, size_t *out_len)
{
	ULP_PDU_t *pdu = calloc(1, sizeof(*pdu));

	if (!pdu)
		return -1;

	int ret = -1;

	pdu->version = make_version();
	pdu->sessionID = make_session_id(set_session_id, slp_session_id,
					 set_id_buf, set_id_len, set_id_type);

	UlpMessage_t *msg = calloc(1, sizeof(*msg));

	if (!msg)
		goto cleanup;
	pdu->message = msg;

	SUPLPOS_t *pos = calloc(1, sizeof(*pos));

	if (!pos)
		goto cleanup;

	PosPayLoad_t *payload = calloc(1, sizeof(*payload));

	if (!payload) {
		free(pos);
		goto cleanup;
	}

	/* LPP goes in Ver2-PosPayLoad-extension.lPPPayload */
	Ver2_PosPayLoad_extension_t *ext = calloc(1, sizeof(*ext));

	if (!ext) {
		free(payload);
		free(pos);
		goto cleanup;
	}

	struct Ver2_PosPayLoad_extension__lPPPayload *lpp_list =
		calloc(1, sizeof(*lpp_list));

	if (!lpp_list) {
		free(ext);
		free(payload);
		free(pos);
		goto cleanup;
	}
	ext->lPPPayload = lpp_list;

	OCTET_STRING_t *lpp_octet = calloc(1, sizeof(*lpp_octet));

	if (!lpp_octet) {
		free(ext);
		free(payload);
		free(pos);
		goto cleanup;
	}
	OCTET_STRING_fromBuf(lpp_octet, (const char *)lpp_buf, (int)lpp_len);

	if (ASN_SEQUENCE_ADD(&lpp_list->list, lpp_octet) != 0) {
		free(lpp_octet);
		free(ext);
		free(payload);
		free(pos);
		goto cleanup;
	}

	payload->present = PosPayLoad_PR_ver2_PosPayLoad_extension;
	payload->choice.ver2_PosPayLoad_extension = ext;

	pos->posPayLoad = payload;

	msg->present = UlpMessage_PR_msSUPLPOS;
	msg->choice.msSUPLPOS = pos;

	ret = encode_ulp_pdu(pdu, out_buf, out_len);

cleanup:
	ASN_STRUCT_FREE(asn_DEF_ULP_PDU, pdu);
	return ret;
}

int supl_build_end(long set_session_id,
		   const uint8_t slp_session_id[4],
		   const uint8_t *set_id_buf, size_t set_id_len,
		   int set_id_type,
		   uint8_t **out_buf, size_t *out_len)
{
	ULP_PDU_t *pdu = calloc(1, sizeof(*pdu));

	if (!pdu)
		return -1;

	int ret = -1;

	pdu->version = make_version();
	pdu->sessionID = make_session_id(set_session_id, slp_session_id,
					 set_id_buf, set_id_len, set_id_type);

	UlpMessage_t *msg = calloc(1, sizeof(*msg));

	if (!msg)
		goto cleanup;
	pdu->message = msg;

	SUPLEND_t *end = calloc(1, sizeof(*end));

	if (!end)
		goto cleanup;

	msg->present = UlpMessage_PR_msSUPLEND;
	msg->choice.msSUPLEND = end;

	ret = encode_ulp_pdu(pdu, out_buf, out_len);

cleanup:
	ASN_STRUCT_FREE(asn_DEF_ULP_PDU, pdu);
	return ret;
}
