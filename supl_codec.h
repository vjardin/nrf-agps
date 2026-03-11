/*
 * supl_codec.h - SUPL/ULP message encode/decode for SUPL 2.0
 *
 * Handles ULP PDU framing, SUPL message construction, and LPP payload
 * wrapping for SET-initiated A-GNSS sessions.
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Opaque ULP PDU handle */
typedef struct ULP_PDU ULP_PDU_t;

/*
 * Decode a UPER-encoded ULP PDU from a length-prefixed TCP buffer.
 * buf must start with the 2-byte big-endian length header.
 * Returns decoded PDU (caller must free with supl_pdu_free), or NULL.
 */
ULP_PDU_t *supl_decode_pdu(const uint8_t *buf, size_t len);

/*
 * Free a decoded ULP PDU.
 */
void supl_pdu_free(ULP_PDU_t *pdu);

/*
 * Get the ULP message type from a decoded PDU.
 * Returns UlpMessage_PR enum value, or 0 (NOTHING) on error.
 */
int supl_get_message_type(const ULP_PDU_t *pdu);

/*
 * Extract the SET session ID from a decoded SUPL START or POS INIT.
 * Returns the session ID integer, or -1 if not present.
 */
long supl_get_set_session_id(const ULP_PDU_t *pdu);

/*
 * Build a SUPL RESPONSE ULP PDU.
 * set_session_id: session ID from the client's SUPL START
 * slp_session_id: server-assigned session ID (4-byte OCTET STRING)
 * out_buf/out_len: receives malloc'd UPER-encoded PDU with length header
 * Returns 0 on success, -1 on error.
 */
int supl_build_response(long set_session_id,
			const uint8_t slp_session_id[4],
			const uint8_t *set_id_buf, size_t set_id_len,
			int set_id_type,
			uint8_t **out_buf, size_t *out_len);

/*
 * Build a SUPL POS ULP PDU containing an LPP payload.
 * lpp_buf/lpp_len: UPER-encoded LPP ProvideAssistanceData message
 * out_buf/out_len: receives malloc'd UPER-encoded PDU with length header
 * Returns 0 on success, -1 on error.
 */
int supl_build_pos(long set_session_id,
		   const uint8_t slp_session_id[4],
		   const uint8_t *set_id_buf, size_t set_id_len,
		   int set_id_type,
		   const uint8_t *lpp_buf, size_t lpp_len,
		   uint8_t **out_buf, size_t *out_len);

/*
 * Build a SUPL END ULP PDU.
 * out_buf/out_len: receives malloc'd UPER-encoded PDU with length header
 * Returns 0 on success, -1 on error.
 */
int supl_build_end(long set_session_id,
		   const uint8_t slp_session_id[4],
		   const uint8_t *set_id_buf, size_t set_id_len,
		   int set_id_type,
		   uint8_t **out_buf, size_t *out_len);
