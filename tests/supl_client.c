/*
 * supl_client.c - SUPL 2.0 test client
 *
 * Connects to a SUPL server (local or supl.google.com), performs a
 * complete SET-initiated A-GNSS session, and dumps the assistance data.
 *
 * Usage:
 *   supl_client [--tls] [--no-tls] [-h HOST] [-p PORT]
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "ULP-PDU.h"
#include "UlpMessage.h"
#include "Version.h"
#include "SessionID.h"
#include "SetSessionID.h"
#include "SlpSessionID.h"
#include "SETId.h"
#include "IPAddress.h"
#include "SETCapabilities.h"
#include "LocationId.h"
#include "Status.h"
#include "CellInfo.h"
#include "GsmCellInformation.h"
#include "Position.h"
#include "PositionEstimate.h"
#include "SUPLSTART.h"
#include "SUPLRESPONSE.h"
#include "SUPLPOSINIT.h"
#include "SUPLPOS.h"
#include "SUPLEND.h"
#include "PosPayLoad.h"
#include "PosMethod.h"
#include "PosTechnology.h"
#include "PosProtocol.h"
#include "PrefMethod.h"
#include "Ver2-PosPayLoad-extension.h"
#include "Ver2-PosProtocol-extension.h"
#include "PosProtocolVersion3GPP.h"
#include "Ver2-PosTechnology-extension.h"
#include "GANSSPositionMethods.h"
#include "GANSSPositionMethod.h"
#include "GANSSPositioningMethodTypes.h"
#include "GANSSSignals.h"

/* LPP headers for payload decoding */
#include "LPP-Message.h"
#include "LPP-MessageBody.h"
#include "ProvideAssistanceData.h"
#include "ProvideAssistanceData-r9-IEs.h"
#include "A-GNSS-ProvideAssistanceData.h"
#include "GNSS-CommonAssistData.h"
#include "GNSS-GenericAssistData.h"
#include "GNSS-GenericAssistDataElement.h"
#include "GNSS-ID.h"
#include "GNSS-NavigationModel.h"
#include "GNSS-NavModelSatelliteList.h"
#include "GNSS-NavModelSatelliteElement.h"
#include "GNSS-ClockModel.h"
#include "GNSS-OrbitModel.h"
#include "NAV-ClockModel.h"
#include "NavModelNAV-KeplerianSet.h"
#include "SV-ID.h"
#include "GNSS-Almanac.h"
#include "GNSS-AlmanacList.h"
#include "GNSS-AlmanacElement.h"
#include "AlmanacNAV-KeplerianSet.h"
#include "GNSS-UTC-Model.h"
#include "UTC-ModelSet1.h"
#include "GNSS-RealTimeIntegrity.h"
#include "GNSS-BadSignalList.h"
#include "BadSignalElement.h"
#include "GNSS-IonosphericModel.h"
#include "KlobucharModelParameter.h"
#include "GNSS-ReferenceTime.h"
#include "GNSS-SystemTime.h"
#include "GNSS-ReferenceLocation.h"
#include "EllipsoidPointWithAltitudeAndUncertaintyEllipsoid.h"
#include "GNSS-EarthOrientationParameters.h"

#include <cjson/cJSON.h>

/* Encode a ULP-PDU to UPER, return malloc'd buffer + length */
static int encode_pdu(ULP_PDU_t *pdu, uint8_t **out_buf, size_t *out_len)
{
	void *buf_ptr = NULL;
	ssize_t enc_len;

	pdu->length = 0;
	enc_len = uper_encode_to_new_buffer(&asn_DEF_ULP_PDU, NULL,
					    pdu, &buf_ptr);
	if (enc_len < 0)
		return -1;
	free(buf_ptr);
	buf_ptr = NULL;

	pdu->length = (long)enc_len;
	enc_len = uper_encode_to_new_buffer(&asn_DEF_ULP_PDU, NULL,
					    pdu, &buf_ptr);
	if (enc_len < 0)
		return -1;

	*out_buf = buf_ptr;
	*out_len = (size_t)enc_len;
	return 0;
}

/* Build SET capabilities with LPP + GPS/QZSS support */
static SETCapabilities_t *make_set_capabilities(void)
{
	SETCapabilities_t *caps = calloc(1, sizeof(*caps));

	if (!caps)
		return NULL;

	/* Position technology: GPS SET-based */
	PosTechnology_t *postech = calloc(1, sizeof(*postech));

	if (!postech) {
		free(caps);
		return NULL;
	}
	postech->agpsSETBased = 1;
	postech->agpsSETassisted = 0;
	postech->autonomousGPS = 1;

	/* Ver2 extension: GANSS position methods (GPS + QZSS) */
	Ver2_PosTechnology_extension_t *pt_ext =
		calloc(1, sizeof(*pt_ext));

	if (pt_ext) {
		GANSSPositionMethods_t *methods =
			calloc(1, sizeof(*methods));

		if (methods) {
			/* GPS: ganssId=0 */
			GANSSPositionMethod_t *gps_m =
				calloc(1, sizeof(*gps_m));

			if (gps_m) {
				gps_m->ganssId = 0; /* GPS */
				GANSSPositioningMethodTypes_t *gps_mt =
					calloc(1, sizeof(*gps_mt));

				if (gps_mt) {
					gps_mt->setBased = 1;
					gps_mt->setAssisted = 0;
					gps_mt->autonomous = 1;
					gps_m->gANSSPositioningMethodTypes = gps_mt;
				}
				/* GANSSSignals: BIT STRING SIZE(8), bit 0 = L1 */
				uint8_t sig = 0x80; /* bit 0 (MSB) = GPS L1 */

				gps_m->gANSSSignals.buf = malloc(1);
				if (gps_m->gANSSSignals.buf) {
					gps_m->gANSSSignals.buf[0] = sig;
					gps_m->gANSSSignals.size = 1;
					gps_m->gANSSSignals.bits_unused = 0;
				}
				ASN_SEQUENCE_ADD(&methods->list, gps_m);
			}

			/* QZSS: ganssId=4 */
			GANSSPositionMethod_t *qzss_m =
				calloc(1, sizeof(*qzss_m));

			if (qzss_m) {
				qzss_m->ganssId = 4; /* QZSS */
				GANSSPositioningMethodTypes_t *qzss_mt =
					calloc(1, sizeof(*qzss_mt));

				if (qzss_mt) {
					qzss_mt->setBased = 1;
					qzss_mt->setAssisted = 0;
					qzss_mt->autonomous = 1;
					qzss_m->gANSSPositioningMethodTypes = qzss_mt;
				}
				uint8_t sig = 0x80;

				qzss_m->gANSSSignals.buf = malloc(1);
				if (qzss_m->gANSSSignals.buf) {
					qzss_m->gANSSSignals.buf[0] = sig;
					qzss_m->gANSSSignals.size = 1;
					qzss_m->gANSSSignals.bits_unused = 0;
				}
				ASN_SEQUENCE_ADD(&methods->list, qzss_m);
			}

			pt_ext->gANSSPositionMethods = methods;
		}
		postech->ver2_PosTechnology_extension = pt_ext;
	}

	caps->posTechnology = postech;
	caps->prefMethod = PrefMethod_agpsSETBasedPreferred;

	/* Position protocol: LPP via Ver2 extension */
	PosProtocol_t *proto = calloc(1, sizeof(*proto));

	if (!proto) {
		free(caps);
		return NULL;
	}
	proto->tia801 = 0;
	proto->rrlp = 0;
	proto->rrc = 0;

	Ver2_PosProtocol_extension_t *pp_ext =
		calloc(1, sizeof(*pp_ext));

	if (pp_ext) {
		pp_ext->lpp = 1;

		PosProtocolVersion3GPP_t *lpp_ver =
			calloc(1, sizeof(*lpp_ver));

		if (lpp_ver) {
			lpp_ver->majorVersionField = 16;
			lpp_ver->technicalVersionField = 4;
			lpp_ver->editorialVersionField = 0;
			pp_ext->posProtocolVersionLPP = lpp_ver;
		}

		proto->ver2_PosProtocol_extension = pp_ext;
	}

	caps->posProtocol = proto;
	return caps;
}

/* Build a dummy GSM LocationId (France/Free Mobile) */
static LocationId_t *make_location_id(void)
{
	LocationId_t *loc = calloc(1, sizeof(*loc));

	if (!loc)
		return NULL;

	CellInfo_t *cell = calloc(1, sizeof(*cell));

	if (!cell) {
		free(loc);
		return NULL;
	}

	GsmCellInformation_t *gsm = calloc(1, sizeof(*gsm));

	if (!gsm) {
		free(cell);
		free(loc);
		return NULL;
	}
	gsm->refMCC = 208;  /* France */
	gsm->refMNC = 15;   /* Free Mobile */
	gsm->refLAC = 1;
	gsm->refCI = 1;

	cell->present = CellInfo_PR_gsmCell;
	cell->choice.gsmCell = gsm;
	loc->cellInfo = cell;
	loc->status = Status_current;
	return loc;
}

/* Build an approximate Position (Paris Notre Dame: 48.8530N, 2.3498E) */
static Position_t *make_position(void)
{
	Position_t *pos = calloc(1, sizeof(*pos));

	if (!pos)
		return NULL;

	/* UTCTime from current time */
	time_t now = time(NULL);
	struct tm *tm = gmtime(&now);
	char utc_str[20];

	strftime(utc_str, sizeof(utc_str), "%Y%m%d%H%M%SZ", tm);
	OCTET_STRING_fromBuf(&pos->timestamp, utc_str,
			     (int)strlen(utc_str));

	PositionEstimate_t *pe = calloc(1, sizeof(*pe));

	if (!pe) {
		ASN_STRUCT_FREE(asn_DEF_Position, pos);
		return NULL;
	}

	/* Paris Notre Dame: 48.8530N, 2.3498E */
	pe->latitudeSign = PositionEstimate__latitudeSign_north;
	pe->latitude = (long)(48.8530 / 90.0 * 8388607.0);
	pe->longitude = (long)(2.3498 / 360.0 * 16777216.0);
	pos->positionEstimate = pe;

	return pos;
}

/* Build SUPL START message */
static int build_supl_start(uint8_t **out_buf, size_t *out_len)
{
	ULP_PDU_t *pdu = calloc(1, sizeof(*pdu));

	if (!pdu)
		return -1;

	int ret = -1;

	/* Version 2.0.0 */
	Version_t *ver = calloc(1, sizeof(*ver));

	if (!ver)
		goto cleanup;
	ver->maj = 2;
	ver->min = 0;
	ver->servind = 0;
	pdu->version = ver;

	/* Session ID: SET side only */
	SessionID_t *sid = calloc(1, sizeof(*sid));

	if (!sid)
		goto cleanup;
	pdu->sessionID = sid;

	SetSessionID_t *set_sid = calloc(1, sizeof(*set_sid));

	if (!set_sid)
		goto cleanup;
	set_sid->sessionId = 1;

	SETId_t *set_id = calloc(1, sizeof(*set_id));

	if (!set_id) {
		free(set_sid);
		goto cleanup;
	}
	set_id->present = SETId_PR_imsi;
	/* Dummy IMSI: 001010123456789 (8 bytes BCD) */
	uint8_t imsi[] = {0x00, 0x10, 0x10, 0x12, 0x34, 0x56, 0x78, 0x90};

	OCTET_STRING_fromBuf(&set_id->choice.imsi, (char *)imsi, 8);
	set_sid->setId = set_id;
	sid->setSessionID = set_sid;

	/* ULP message: SUPL START */
	UlpMessage_t *msg = calloc(1, sizeof(*msg));

	if (!msg)
		goto cleanup;
	pdu->message = msg;

	SUPLSTART_t *start = calloc(1, sizeof(*start));

	if (!start)
		goto cleanup;

	/* SET capabilities: GPS/QZSS SET-based, LPP protocol */
	start->sETCapabilities = make_set_capabilities();
	if (!start->sETCapabilities) {
		free(start);
		goto cleanup;
	}

	/* Location ID: dummy GSM cell */
	start->locationId = make_location_id();
	if (!start->locationId) {
		free(start);
		goto cleanup;
	}

	msg->present = UlpMessage_PR_msSUPLSTART;
	msg->choice.msSUPLSTART = start;

	ret = encode_pdu(pdu, out_buf, out_len);

cleanup:
	ASN_STRUCT_FREE(asn_DEF_ULP_PDU, pdu);
	return ret;
}

/* Build SUPL POS INIT message, echoing back server's session info */
static int build_supl_pos_init(long set_session_id,
			       const SlpSessionID_t *slp_orig,
			       uint8_t **out_buf, size_t *out_len)
{
	ULP_PDU_t *pdu = calloc(1, sizeof(*pdu));

	if (!pdu)
		return -1;

	int ret = -1;

	Version_t *ver = calloc(1, sizeof(*ver));

	if (!ver)
		goto cleanup;
	ver->maj = 2;
	ver->min = 0;
	ver->servind = 0;
	pdu->version = ver;

	/* Session ID: SET + SLP */
	SessionID_t *sid = calloc(1, sizeof(*sid));

	if (!sid)
		goto cleanup;
	pdu->sessionID = sid;

	SetSessionID_t *set_sid = calloc(1, sizeof(*set_sid));

	if (!set_sid)
		goto cleanup;
	set_sid->sessionId = set_session_id;

	SETId_t *set_id = calloc(1, sizeof(*set_id));

	if (!set_id) {
		free(set_sid);
		goto cleanup;
	}
	set_id->present = SETId_PR_imsi;
	uint8_t imsi[] = {0x00, 0x10, 0x10, 0x12, 0x34, 0x56, 0x78, 0x90};

	OCTET_STRING_fromBuf(&set_id->choice.imsi, (char *)imsi, 8);
	set_sid->setId = set_id;
	sid->setSessionID = set_sid;

	/* Deep-copy the SLP session ID from the RESPONSE */
	if (slp_orig) {
		void *copy = NULL;
		asn_dec_rval_t dc;
		void *enc_buf = NULL;
		ssize_t enc_len;

		/* Encode then decode to deep-copy */
		enc_len = uper_encode_to_new_buffer(
			&asn_DEF_SlpSessionID, NULL,
			slp_orig, &enc_buf);
		if (enc_len > 0 && enc_buf) {
			dc = uper_decode_complete(
				NULL, &asn_DEF_SlpSessionID,
				&copy, enc_buf, (size_t)enc_len);
			free(enc_buf);
			if (dc.code == RC_OK)
				sid->slpSessionID = copy;
			else
				ASN_STRUCT_FREE(asn_DEF_SlpSessionID, copy);
		} else {
			free(enc_buf);
		}
	}

	/* SUPL POS INIT message */
	UlpMessage_t *msg = calloc(1, sizeof(*msg));

	if (!msg)
		goto cleanup;
	pdu->message = msg;

	SUPLPOSINIT_t *posinit = calloc(1, sizeof(*posinit));

	if (!posinit)
		goto cleanup;

	/* SET capabilities: GPS/QZSS SET-based, LPP protocol */
	posinit->sETCapabilities = make_set_capabilities();
	if (!posinit->sETCapabilities) {
		free(posinit);
		goto cleanup;
	}

	/* Requested assistance data */
	RequestedAssistData_t *rad = calloc(1, sizeof(*rad));

	if (!rad) {
		free(posinit);
		goto cleanup;
	}
	rad->almanacRequested = 1;
	rad->utcModelRequested = 1;
	rad->ionosphericModelRequested = 1;
	rad->dgpsCorrectionsRequested = 0;
	rad->referenceLocationRequested = 1;
	rad->referenceTimeRequested = 1;
	rad->acquisitionAssistanceRequested = 0;
	rad->realTimeIntegrityRequested = 1;
	rad->navigationModelRequested = 1;
	posinit->requestedAssistData = rad;

	/* Location ID: dummy GSM cell */
	posinit->locationId = make_location_id();
	if (!posinit->locationId) {
		free(posinit);
		goto cleanup;
	}

	/* Approximate position: Paris Notre Dame */
	posinit->position = make_position();

	msg->present = UlpMessage_PR_msSUPLPOSINIT;
	msg->choice.msSUPLPOSINIT = posinit;

	ret = encode_pdu(pdu, out_buf, out_len);

cleanup:
	ASN_STRUCT_FREE(asn_DEF_ULP_PDU, pdu);
	return ret;
}

/* TCP/TLS I/O helpers */
struct connection {
	int fd;
	SSL *ssl;
};

static ssize_t conn_write(struct connection *c, const void *buf, size_t len)
{
	if (c->ssl)
		return SSL_write(c->ssl, buf, (int)len);
	return write(c->fd, buf, len);
}

static ssize_t conn_read(struct connection *c, void *buf, size_t len)
{
	if (c->ssl)
		return SSL_read(c->ssl, buf, (int)len);
	return read(c->fd, buf, len);
}

static int conn_read_exact(struct connection *c, uint8_t *buf, size_t len)
{
	size_t off = 0;

	while (off < len) {
		ssize_t n = conn_read(c, buf + off, len - off);

		if (n <= 0)
			return -1;
		off += (size_t)n;
	}
	return 0;
}

/* Read a complete ULP PDU (length-prefixed) */
static ULP_PDU_t *read_ulp_pdu(struct connection *c)
{
	uint8_t hdr[2];

	if (conn_read_exact(c, hdr, 2) != 0) {
		fprintf(stderr, "Failed to read ULP length header\n");
		return NULL;
	}

	size_t pdu_len = ((size_t)hdr[0] << 8) | hdr[1];

	if (pdu_len < 2 || pdu_len > 65536) {
		fprintf(stderr, "Invalid ULP length: %zu\n", pdu_len);
		return NULL;
	}

	uint8_t *buf = malloc(pdu_len);

	if (!buf)
		return NULL;

	/* First 2 bytes are the length we already read */
	buf[0] = hdr[0];
	buf[1] = hdr[1];

	if (pdu_len > 2) {
		if (conn_read_exact(c, buf + 2, pdu_len - 2) != 0) {
			fprintf(stderr, "Failed to read ULP PDU body\n");
			free(buf);
			return NULL;
		}
	}

	ULP_PDU_t *pdu = NULL;
	asn_dec_rval_t dec = uper_decode_complete(NULL, &asn_DEF_ULP_PDU,
						  (void **)&pdu, buf, pdu_len);
	free(buf);

	if (dec.code != RC_OK) {
		fprintf(stderr, "Failed to decode ULP PDU (code=%d)\n",
			dec.code);
		ASN_STRUCT_FREE(asn_DEF_ULP_PDU, pdu);
		return NULL;
	}

	return pdu;
}

static const char *msg_type_name(int type)
{
	switch (type) {
	case UlpMessage_PR_msSUPLINIT: return "SUPL INIT";
	case UlpMessage_PR_msSUPLSTART: return "SUPL START";
	case UlpMessage_PR_msSUPLRESPONSE: return "SUPL RESPONSE";
	case UlpMessage_PR_msSUPLPOSINIT: return "SUPL POS INIT";
	case UlpMessage_PR_msSUPLPOS: return "SUPL POS";
	case UlpMessage_PR_msSUPLEND: return "SUPL END";
	default: return "UNKNOWN";
	}
}

static const char *gnss_id_name(long id)
{
	switch (id) {
	case 0: return "GPS";
	case 1: return "SBAS";
	case 2: return "QZSS";
	case 3: return "Galileo";
	case 4: return "GLONASS";
	case 5: return "BDS";
	default: return "unknown";
	}
}

static const char *clock_model_name(int pr)
{
	switch (pr) {
	case GNSS_ClockModel_PR_standardClockModelList: return "standard";
	case GNSS_ClockModel_PR_nav_ClockModel: return "nav";
	case GNSS_ClockModel_PR_cnav_ClockModel: return "cnav";
	case GNSS_ClockModel_PR_glonass_ClockModel: return "glonass";
	case GNSS_ClockModel_PR_sbas_ClockModel: return "sbas";
	default: return "other";
	}
}

static const char *orbit_model_name(int pr)
{
	switch (pr) {
	case GNSS_OrbitModel_PR_keplerianSet: return "keplerian";
	case GNSS_OrbitModel_PR_nav_KeplerianSet: return "nav";
	case GNSS_OrbitModel_PR_cnav_KeplerianSet: return "cnav";
	case GNSS_OrbitModel_PR_glonass_ECEF: return "glonass";
	case GNSS_OrbitModel_PR_sbas_ECEF: return "sbas";
	default: return "other";
	}
}

static const char *almanac_model_name(int pr)
{
	switch (pr) {
	case GNSS_AlmanacElement_PR_keplerianAlmanacSet: return "keplerian";
	case GNSS_AlmanacElement_PR_keplerianNAV_Almanac: return "nav";
	case GNSS_AlmanacElement_PR_keplerianReducedAlmanac: return "reduced";
	case GNSS_AlmanacElement_PR_keplerianMidiAlmanac: return "midi";
	case GNSS_AlmanacElement_PR_keplerianGLONASS: return "glonass";
	case GNSS_AlmanacElement_PR_ecef_SBAS_Almanac: return "sbas";
	default: return "other";
	}
}

static cJSON *json_common(const GNSS_CommonAssistData_t *c)
{
	cJSON *obj = cJSON_CreateObject();

	if (c->gnss_ReferenceTime) {
		cJSON *rt = cJSON_CreateObject();
		GNSS_SystemTime_t *st =
			c->gnss_ReferenceTime->gnss_SystemTime;

		if (st) {
			if (st->gnss_TimeID)
				cJSON_AddStringToObject(rt, "gnss",
					gnss_id_name(st->gnss_TimeID->gnss_id));
			cJSON_AddNumberToObject(rt, "day",
						st->gnss_DayNumber);
			cJSON_AddNumberToObject(rt, "tod",
						st->gnss_TimeOfDay);
		}
		cJSON_AddItemToObject(obj, "referenceTime", rt);
	}

	if (c->gnss_ReferenceLocation) {
		cJSON *rl = cJSON_CreateObject();
		EllipsoidPointWithAltitudeAndUncertaintyEllipsoid_t *p =
			c->gnss_ReferenceLocation->threeDlocation;

		if (p) {
			double lat = p->degreesLatitude / 8388607.0 * 90.0;

			if (p->latitudeSign == 1)
				lat = -lat;
			double lon = p->degreesLongitude / 16777216.0 * 360.0;

			cJSON_AddNumberToObject(rl, "latitude", lat);
			cJSON_AddNumberToObject(rl, "longitude", lon);
			cJSON_AddNumberToObject(rl, "altitude",
						(double)p->altitude);
		}
		cJSON_AddItemToObject(obj, "referenceLocation", rl);
	}

	if (c->gnss_IonosphericModel &&
	    c->gnss_IonosphericModel->klobucharModel) {
		cJSON *ion = cJSON_CreateObject();
		KlobucharModelParameter_t *k =
			c->gnss_IonosphericModel->klobucharModel;

		cJSON_AddNumberToObject(ion, "alfa0", k->alfa0);
		cJSON_AddNumberToObject(ion, "alfa1", k->alfa1);
		cJSON_AddNumberToObject(ion, "alfa2", k->alfa2);
		cJSON_AddNumberToObject(ion, "alfa3", k->alfa3);
		cJSON_AddNumberToObject(ion, "beta0", k->beta0);
		cJSON_AddNumberToObject(ion, "beta1", k->beta1);
		cJSON_AddNumberToObject(ion, "beta2", k->beta2);
		cJSON_AddNumberToObject(ion, "beta3", k->beta3);
		cJSON_AddItemToObject(obj, "ionosphericModel", ion);
	}

	if (c->gnss_EarthOrientationParameters)
		cJSON_AddTrueToObject(obj, "earthOrientationParameters");

	return obj;
}

static cJSON *json_nav_model(const GNSS_NavigationModel_t *nm)
{
	cJSON *obj = cJSON_CreateObject();
	GNSS_NavModelSatelliteList_t *sl = nm->gnss_SatelliteList;

	cJSON_AddNumberToObject(obj, "count", sl->list.count);

	cJSON *svs = cJSON_CreateArray();

	for (int i = 0; i < sl->list.count; i++) {
		GNSS_NavModelSatelliteElement_t *e = sl->list.array[i];
		cJSON *sv = cJSON_CreateObject();

		cJSON_AddNumberToObject(sv, "svId",
					e->svID->satellite_id);
		if (e->gnss_ClockModel)
			cJSON_AddStringToObject(sv, "clock",
				clock_model_name(
					e->gnss_ClockModel->present));
		if (e->gnss_OrbitModel)
			cJSON_AddStringToObject(sv, "orbit",
				orbit_model_name(
					e->gnss_OrbitModel->present));
		cJSON_AddItemToArray(svs, sv);
	}
	cJSON_AddItemToObject(obj, "satellites", svs);
	return obj;
}

static cJSON *json_almanac(const GNSS_Almanac_t *alm)
{
	cJSON *obj = cJSON_CreateObject();

	if (alm->weekNumber)
		cJSON_AddNumberToObject(obj, "weekNumber",
					*alm->weekNumber);
	if (alm->toa)
		cJSON_AddNumberToObject(obj, "toa", *alm->toa);

	if (alm->gnss_AlmanacList) {
		int cnt = alm->gnss_AlmanacList->list.count;

		cJSON_AddNumberToObject(obj, "count", cnt);

		cJSON *svs = cJSON_CreateArray();

		for (int i = 0; i < cnt; i++) {
			GNSS_AlmanacElement_t *e =
				alm->gnss_AlmanacList->list.array[i];
			cJSON *sv = cJSON_CreateObject();

			cJSON_AddStringToObject(sv, "model",
				almanac_model_name(e->present));

			/* Extract svId from the chosen model */
			long svid = -1;

			if (e->present ==
			    GNSS_AlmanacElement_PR_keplerianNAV_Almanac &&
			    e->choice.keplerianNAV_Almanac &&
			    e->choice.keplerianNAV_Almanac->svID)
				svid = e->choice.keplerianNAV_Almanac
					->svID->satellite_id;
			else if (e->present ==
				 GNSS_AlmanacElement_PR_keplerianAlmanacSet &&
				 e->choice.keplerianAlmanacSet &&
				 e->choice.keplerianAlmanacSet->svID)
				svid = e->choice.keplerianAlmanacSet
					->svID->satellite_id;

			if (svid >= 0)
				cJSON_AddNumberToObject(sv, "svId", svid);
			cJSON_AddItemToArray(svs, sv);
		}
		cJSON_AddItemToObject(obj, "satellites", svs);
	}
	return obj;
}

static cJSON *json_utc(const GNSS_UTC_Model_t *utc)
{
	cJSON *obj = cJSON_CreateObject();

	switch (utc->present) {
	case GNSS_UTC_Model_PR_utcModel1:
		cJSON_AddStringToObject(obj, "model", "set1");
		if (utc->choice.utcModel1) {
			UTC_ModelSet1_t *m = utc->choice.utcModel1;

			cJSON_AddNumberToObject(obj, "a0", m->gnss_Utc_A0);
			cJSON_AddNumberToObject(obj, "a1", m->gnss_Utc_A1);
			cJSON_AddNumberToObject(obj, "tot", m->gnss_Utc_Tot);
			cJSON_AddNumberToObject(obj, "wnt", m->gnss_Utc_WNt);
			cJSON_AddNumberToObject(obj, "deltaTls",
						m->gnss_Utc_DeltaTls);
			cJSON_AddNumberToObject(obj, "deltaTlsf",
						m->gnss_Utc_DeltaTlsf);
		}
		break;
	case GNSS_UTC_Model_PR_utcModel2:
		cJSON_AddStringToObject(obj, "model", "set2");
		break;
	case GNSS_UTC_Model_PR_utcModel3:
		cJSON_AddStringToObject(obj, "model", "set3");
		break;
	case GNSS_UTC_Model_PR_utcModel4:
		cJSON_AddStringToObject(obj, "model", "set4");
		break;
	default:
		cJSON_AddStringToObject(obj, "model", "unknown");
		break;
	}
	return obj;
}

static cJSON *json_integrity(const GNSS_RealTimeIntegrity_t *rti)
{
	cJSON *obj = cJSON_CreateObject();

	if (rti->gnss_BadSignalList) {
		int cnt = rti->gnss_BadSignalList->list.count;

		cJSON_AddNumberToObject(obj, "badSignals", cnt);

		cJSON *svs = cJSON_CreateArray();

		for (int i = 0; i < cnt; i++) {
			BadSignalElement_t *b =
				rti->gnss_BadSignalList->list.array[i];
			if (b->badSVID)
				cJSON_AddItemToArray(svs,
					cJSON_CreateNumber(
						b->badSVID->satellite_id));
		}
		cJSON_AddItemToObject(obj, "badSvIds", svs);
	}
	return obj;
}

static cJSON *json_generic_element(const GNSS_GenericAssistDataElement_t *e)
{
	cJSON *obj = cJSON_CreateObject();

	long gid = e->gnss_ID ? e->gnss_ID->gnss_id : -1;

	cJSON_AddStringToObject(obj, "gnss", gnss_id_name(gid));

	if (e->gnss_NavigationModel)
		cJSON_AddItemToObject(obj, "navigationModel",
				      json_nav_model(e->gnss_NavigationModel));

	if (e->gnss_Almanac)
		cJSON_AddItemToObject(obj, "almanac",
				      json_almanac(e->gnss_Almanac));

	if (e->gnss_UTC_Model)
		cJSON_AddItemToObject(obj, "utcModel",
				      json_utc(e->gnss_UTC_Model));

	if (e->gnss_RealTimeIntegrity)
		cJSON_AddItemToObject(obj, "realTimeIntegrity",
				      json_integrity(
					e->gnss_RealTimeIntegrity));

	if (e->gnss_AuxiliaryInformation)
		cJSON_AddTrueToObject(obj, "auxiliaryInformation");

	if (e->gnss_DataBitAssistance)
		cJSON_AddTrueToObject(obj, "dataBitAssistance");

	if (e->gnss_AcquisitionAssistance)
		cJSON_AddTrueToObject(obj, "acquisitionAssistance");

	return obj;
}

static cJSON *decode_lpp_json(const uint8_t *buf, size_t len)
{
	LPP_Message_t *lpp = NULL;
	asn_dec_rval_t dec = uper_decode_complete(
		NULL, &asn_DEF_LPP_Message,
		(void **)&lpp, buf, len);

	if (dec.code != RC_OK)
		return cJSON_CreateString("decode_failed");

	if (!lpp->lpp_MessageBody ||
	    lpp->lpp_MessageBody->present != LPP_MessageBody_PR_c1 ||
	    lpp->lpp_MessageBody->choice.c1.present !=
	    LPP_MessageBody__c1_PR_provideAssistanceData) {
		ASN_STRUCT_FREE(asn_DEF_LPP_Message, lpp);
		return cJSON_CreateString("not_assistance_data");
	}

	ProvideAssistanceData_t *pad =
		&lpp->lpp_MessageBody->choice.c1.choice.provideAssistanceData;

	if (pad->criticalExtensions.present !=
	    ProvideAssistanceData__criticalExtensions_PR_c1 ||
	    pad->criticalExtensions.choice.c1.present !=
	    ProvideAssistanceData__criticalExtensions__c1_PR_provideAssistanceData_r9) {
		ASN_STRUCT_FREE(asn_DEF_LPP_Message, lpp);
		return cJSON_CreateString("unexpected_extensions");
	}

	ProvideAssistanceData_r9_IEs_t *r9 =
		pad->criticalExtensions.choice.c1.choice.provideAssistanceData_r9;
	A_GNSS_ProvideAssistanceData_t *agnss =
		r9->a_gnss_ProvideAssistanceData;

	if (!agnss) {
		ASN_STRUCT_FREE(asn_DEF_LPP_Message, lpp);
		return cJSON_CreateString("no_agnss_data");
	}

	cJSON *root = cJSON_CreateObject();

	if (agnss->gnss_CommonAssistData)
		cJSON_AddItemToObject(root, "commonAssistData",
				      json_common(agnss->gnss_CommonAssistData));

	if (agnss->gnss_GenericAssistData) {
		cJSON *arr = cJSON_CreateArray();

		for (int i = 0;
		     i < agnss->gnss_GenericAssistData->list.count; i++)
			cJSON_AddItemToArray(arr,
				json_generic_element(
					agnss->gnss_GenericAssistData
					->list.array[i]));

		cJSON_AddItemToObject(root, "genericAssistData", arr);
	}

	ASN_STRUCT_FREE(asn_DEF_LPP_Message, lpp);
	return root;
}

static cJSON *build_session_json(const ULP_PDU_t *pdu)
{
	if (!pdu->message ||
	    pdu->message->present != UlpMessage_PR_msSUPLPOS)
		return NULL;

	SUPLPOS_t *pos = pdu->message->choice.msSUPLPOS;

	if (!pos || !pos->posPayLoad)
		return NULL;

	PosPayLoad_t *pl = pos->posPayLoad;

	if (pl->present == PosPayLoad_PR_ver2_PosPayLoad_extension) {
		Ver2_PosPayLoad_extension_t *ext =
			pl->choice.ver2_PosPayLoad_extension;

		if (ext && ext->lPPPayload && ext->lPPPayload->list.count > 0) {
			OCTET_STRING_t *os =
				ext->lPPPayload->list.array[0];
			return decode_lpp_json(os->buf, os->size);
		}
	}
	return NULL;
}

static int run_session(const char *host, int port, int use_tls,
		       const char *json_path)
{
	printf("Connecting to %s:%d (%s)...\n",
	       host, port, use_tls ? "TLS" : "plain TCP");

	/* Resolve hostname */
	struct addrinfo hints = {0}, *res;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	char port_str[16];

	snprintf(port_str, sizeof(port_str), "%d", port);

	int rc = getaddrinfo(host, port_str, &hints, &res);

	if (rc != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
		return -1;
	}

	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

	if (fd < 0) {
		perror("socket");
		freeaddrinfo(res);
		return -1;
	}

	if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
		perror("connect");
		close(fd);
		freeaddrinfo(res);
		return -1;
	}
	freeaddrinfo(res);
	printf("Connected.\n");

	struct connection conn = {.fd = fd, .ssl = NULL};

	SSL_CTX *ssl_ctx = NULL;

	if (use_tls) {
		SSL_library_init();
		SSL_load_error_strings();

		ssl_ctx = SSL_CTX_new(TLS_client_method());
		if (!ssl_ctx) {
			fprintf(stderr, "SSL_CTX_new failed\n");
			close(fd);
			return -1;
		}

		conn.ssl = SSL_new(ssl_ctx);
		SSL_set_fd(conn.ssl, fd);
		SSL_set_tlsext_host_name(conn.ssl, host);

		if (SSL_connect(conn.ssl) != 1) {
			fprintf(stderr, "SSL handshake failed\n");
			ERR_print_errors_fp(stderr);
			SSL_free(conn.ssl);
			SSL_CTX_free(ssl_ctx);
			close(fd);
			return -1;
		}
		printf("TLS established: %s\n", SSL_get_cipher(conn.ssl));
	}

	int ret = -1;

	/* Step 1: Send SUPL START */
	uint8_t *start_buf = NULL;
	size_t start_len = 0;

	if (build_supl_start(&start_buf, &start_len) != 0) {
		fprintf(stderr, "Failed to build SUPL START\n");
		goto done;
	}

	printf("Sending SUPL START (%zu bytes)...\n", start_len);
	if (conn_write(&conn, start_buf, start_len) != (ssize_t)start_len) {
		fprintf(stderr, "Failed to send SUPL START\n");
		free(start_buf);
		goto done;
	}
	free(start_buf);

	/* Step 2: Receive SUPL RESPONSE */
	printf("Waiting for SUPL RESPONSE...\n");
	ULP_PDU_t *resp_pdu = read_ulp_pdu(&conn);

	if (!resp_pdu) {
		fprintf(stderr, "Failed to receive response\n");
		goto done;
	}

	int msg_type = resp_pdu->message ? resp_pdu->message->present : 0;

	printf("Received: %s (length=%ld)\n",
	       msg_type_name(msg_type), resp_pdu->length);

	if (msg_type == UlpMessage_PR_msSUPLEND) {
		printf("Server sent SUPL END (session rejected)\n");
		if (resp_pdu->message->choice.msSUPLEND->statusCode)
			printf("  Status: %ld\n",
			       *resp_pdu->message->choice.msSUPLEND->statusCode);
		ASN_STRUCT_FREE(asn_DEF_ULP_PDU, resp_pdu);
		ret = 0;
		goto done;
	}

	if (msg_type != UlpMessage_PR_msSUPLRESPONSE) {
		fprintf(stderr, "Expected SUPL RESPONSE, got %s\n",
			msg_type_name(msg_type));
		ASN_STRUCT_FREE(asn_DEF_ULP_PDU, resp_pdu);
		goto done;
	}

	SUPLRESPONSE_t *resp = resp_pdu->message->choice.msSUPLRESPONSE;

	printf("  PosMethod: %ld\n", resp->posMethod);

	/* Extract SLP session ID for echo-back */
	const SlpSessionID_t *slp_sid_ref = NULL;

	if (resp_pdu->sessionID && resp_pdu->sessionID->slpSessionID) {
		slp_sid_ref = resp_pdu->sessionID->slpSessionID;
		printf("  SLP session ID: %zu bytes\n",
		       slp_sid_ref->sessionID.size);
	}

	/* Step 3: Send SUPL POS INIT (build before freeing resp_pdu) */
	uint8_t *pi_buf = NULL;
	size_t pi_len = 0;

	if (build_supl_pos_init(1, slp_sid_ref,
				&pi_buf, &pi_len) != 0) {
		fprintf(stderr, "Failed to build SUPL POS INIT\n");
		ASN_STRUCT_FREE(asn_DEF_ULP_PDU, resp_pdu);
		goto done;
	}
	ASN_STRUCT_FREE(asn_DEF_ULP_PDU, resp_pdu);

	printf("Sending SUPL POS INIT (%zu bytes)...\n", pi_len);
	if (conn_write(&conn, pi_buf, pi_len) != (ssize_t)pi_len) {
		fprintf(stderr, "Failed to send SUPL POS INIT\n");
		free(pi_buf);
		goto done;
	}
	free(pi_buf);

	/* Step 4: Receive messages until SUPL END */
	int got_pos = 0;
	int got_end = 0;
	cJSON *lpp_json = NULL;

	while (!got_end) {
		printf("Waiting for response...\n");
		ULP_PDU_t *pdu = read_ulp_pdu(&conn);

		if (!pdu) {
			fprintf(stderr, "Connection closed or decode error\n");
			break;
		}

		msg_type = pdu->message ? pdu->message->present : 0;
		printf("Received: %s (length=%ld)\n",
		       msg_type_name(msg_type), pdu->length);

		switch (msg_type) {
		case UlpMessage_PR_msSUPLPOS:
			got_pos = 1;
			if (!lpp_json)
				lpp_json = build_session_json(pdu);
			break;
		case UlpMessage_PR_msSUPLEND:
			got_end = 1;
			if (pdu->message->choice.msSUPLEND->statusCode)
				printf("  Status: %ld\n",
				       *pdu->message->choice.msSUPLEND->statusCode);
			else
				printf("  (no status code)\n");
			break;
		default:
			printf("  (unexpected message type)\n");
			break;
		}

		ASN_STRUCT_FREE(asn_DEF_ULP_PDU, pdu);
	}

	printf("\nSession complete: pos=%s end=%s\n",
	       got_pos ? "yes" : "no", got_end ? "yes" : "no");

	if (lpp_json && json_path) {
		char *str = cJSON_Print(lpp_json);

		if (str) {
			FILE *f = fopen(json_path, "w");

			if (f) {
				fputs(str, f);
				fputc('\n', f);
				fclose(f);
				printf("LPP JSON written to %s\n", json_path);
			}
			free(str);
		}
	} else if (lpp_json) {
		char *str = cJSON_Print(lpp_json);

		if (str) {
			printf("%s\n", str);
			free(str);
		}
	}
	cJSON_Delete(lpp_json);
	ret = 0;

done:
	if (conn.ssl) {
		SSL_shutdown(conn.ssl);
		SSL_free(conn.ssl);
	}
	if (ssl_ctx)
		SSL_CTX_free(ssl_ctx);
	close(fd);
	return ret;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"\n"
		"Options:\n"
		"  -h HOST    Server hostname (default: 127.0.0.1)\n"
		"  -p PORT    Server port (default: 7276 plain, 7275 TLS)\n"
		"  -o FILE    Write LPP JSON to file (default: stdout)\n"
		"  --tls      Use TLS (required for supl.google.com:7275)\n"
		"  --no-tls   Plain TCP (default)\n"
		"  --help     Show this help\n"
		"\n"
		"Examples:\n"
		"  %s -o local.json                          # local server\n"
		"  %s --tls -h supl.google.com -o google.json\n",
		prog, prog, prog);
}

int main(int argc, char *argv[])
{
	const char *host = "127.0.0.1";
	const char *json_path = NULL;
	int port = 0;
	int use_tls = 0;

	static struct option long_opts[] = {
		{"tls",    no_argument, NULL, 't'},
		{"no-tls", no_argument, NULL, 'n'},
		{"help",   no_argument, NULL, 'H'},
		{NULL, 0, NULL, 0}
	};

	int opt;

	while ((opt = getopt_long(argc, argv, "h:p:o:",
				  long_opts, NULL)) != -1) {
		switch (opt) {
		case 'h':
			host = optarg;
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'o':
			json_path = optarg;
			break;
		case 't':
			use_tls = 1;
			break;
		case 'n':
			use_tls = 0;
			break;
		case 'H':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	if (port == 0)
		port = use_tls ? 7275 : 7276;

	return run_session(host, port, use_tls, json_path) == 0 ? 0 : 1;
}
