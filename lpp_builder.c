/*
 * lpp_builder.c - Build LPP ProvideAssistanceData from GPS assist data
 *
 * Converts gps_assist_data (doubles / RINEX representation) into 3GPP
 * TS 37.355 ASN.1 structures using GPS ICD IS-GPS-200 scale factors,
 * then encodes to UPER via asn1c.
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lpp_builder.h"

#include "LPP-Message.h"
#include "LPP-MessageBody.h"
#include "ProvideAssistanceData.h"
#include "ProvideAssistanceData-r9-IEs.h"
#include "A-GNSS-ProvideAssistanceData.h"
#include "GNSS-CommonAssistData.h"
#include "GNSS-GenericAssistData.h"
#include "GNSS-GenericAssistDataElement.h"
#include "GNSS-ID.h"
#include "SV-ID.h"
#include "GNSS-NavigationModel.h"
#include "GNSS-NavModelSatelliteList.h"
#include "GNSS-NavModelSatelliteElement.h"
#include "GNSS-ClockModel.h"
#include "GNSS-OrbitModel.h"
#include "NAV-ClockModel.h"
#include "NavModelNAV-KeplerianSet.h"
#include "GNSS-Almanac.h"
#include "GNSS-AlmanacList.h"
#include "GNSS-AlmanacElement.h"
#include "AlmanacNAV-KeplerianSet.h"
#include "GNSS-IonosphericModel.h"
#include "KlobucharModelParameter.h"
#include "GNSS-UTC-Model.h"
#include "UTC-ModelSet1.h"
#include "GNSS-ReferenceTime.h"
#include "GNSS-SystemTime.h"
#include "GNSS-ReferenceLocation.h"
#include "EllipsoidPointWithAltitudeAndUncertaintyEllipsoid.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GPS_EPOCH_UNIX 315964800L
#define GPS_LEAP_SECONDS 18

static inline double rad2sc(double rad)
{
	return rad / M_PI;
}

static int set_bit_string(BIT_STRING_t *bs, unsigned long val, int nbits)
{
	int nbytes = (nbits + 7) / 8;
	uint8_t *buf = calloc(1, nbytes);

	if (!buf)
		return -1;

	/* Store value MSB-first into buf, right-aligned within nbits */
	for (int i = nbytes - 1; i >= 0; i--) {
		buf[i] = val & 0xFF;
		val >>= 8;
	}

	/* Shift left so the value occupies the MSB positions */
	int unused = nbytes * 8 - nbits;

	if (unused > 0) {
		for (int i = 0; i < nbytes; i++) {
			buf[i] <<= unused;
			if (i + 1 < nbytes)
				buf[i] |= buf[i + 1] >> (8 - unused);
		}
	}

	bs->buf = buf;
	bs->size = nbytes;
	bs->bits_unused = unused;
	return 0;
}

static GNSS_ID_t *make_gnss_id(long id)
{
	GNSS_ID_t *gid = calloc(1, sizeof(*gid));

	if (!gid)
		return NULL;
	gid->gnss_id = id;
	return gid;
}

/*
 * LPP SV-ID is 0-based per GNSS system:
 *   GPS:  satellite_id = PRN - 1       (0..63)
 *   QZSS: satellite_id = PRN - 193     (0..9)
 */
static SV_ID_t *make_sv_id(long prn, long gnss_id)
{
	SV_ID_t *sid = calloc(1, sizeof(*sid));

	if (!sid)
		return NULL;

	if (gnss_id == GNSS_ID__gnss_id_qzss)
		sid->satellite_id = prn - QZSS_PRN_OFFSET - 1;
	else
		sid->satellite_id = prn - 1;

	return sid;
}

static int build_nav_ephemeris(const struct gps_ephemeris *sv, int count,
			       long gnss_id,
			       GNSS_GenericAssistDataElement_t *elem)
{
	GNSS_NavigationModel_t *nav = calloc(1, sizeof(*nav));

	if (!nav)
		return -1;
	nav->nonBroadcastIndFlag = 0;

	GNSS_NavModelSatelliteList_t *satlist = calloc(1, sizeof(*satlist));

	if (!satlist) {
		free(nav);
		return -1;
	}
	nav->gnss_SatelliteList = satlist;

	for (int i = 0; i < count; i++) {
		const struct gps_ephemeris *s = &sv[i];
		GNSS_NavModelSatelliteElement_t *sat = calloc(1, sizeof(*sat));

		if (!sat)
			return -1;

		sat->svID = make_sv_id(s->prn, gnss_id);
		if (!sat->svID) {
			free(sat);
			return -1;
		}

		/* svHealth: 8-bit BIT STRING for GPS */
		if (set_bit_string(&sat->svHealth, s->health, 8) != 0) {
			free(sat);
			return -1;
		}

		/* IOD: 11-bit BIT STRING, GPS IODC is 10-bit → zero-pad MSB */
		if (set_bit_string(&sat->iod, s->iodc & 0x3FF, 11) != 0) {
			free(sat);
			return -1;
		}

		/* NAV clock model */
		GNSS_ClockModel_t *clk = calloc(1, sizeof(*clk));
		NAV_ClockModel_t *navclk = calloc(1, sizeof(*navclk));

		if (!clk || !navclk) {
			free(clk);
			free(navclk);
			free(sat);
			return -1;
		}
		navclk->navToc = (long)(s->toc / 16);
		navclk->navaf2 = (long)round(s->af2 / 2.77555756156289135106e-17);
		navclk->navaf1 = (long)round(s->af1 / 1.13686837721616029739e-13);
		navclk->navaf0 = (long)round(s->af0 / 4.65661287307739257812e-10);
		navclk->navTgd = (long)round(s->tgd / 4.65661287307739257812e-10);

		clk->present = GNSS_ClockModel_PR_nav_ClockModel;
		clk->choice.nav_ClockModel = navclk;
		sat->gnss_ClockModel = clk;

		/* NAV orbit model */
		GNSS_OrbitModel_t *orb = calloc(1, sizeof(*orb));
		NavModelNAV_KeplerianSet_t *kep = calloc(1, sizeof(*kep));

		if (!orb || !kep) {
			free(orb);
			free(kep);
			free(sat);
			return -1;
		}
		kep->navURA      = 0;
		kep->navFitFlag  = 0;
		kep->navToe      = (long)(s->toe / 16);
		kep->navOmega    = (long)round(rad2sc(s->omega) / 4.65661287307739257812e-10);
		kep->navDeltaN   = (long)round(rad2sc(s->delta_n) / 1.13686837721616029739e-13);
		kep->navM0       = (long)round(rad2sc(s->m0) / 4.65661287307739257812e-10);
		kep->navOmegaADot = (long)round(rad2sc(s->omega_dot) / 1.13686837721616029739e-13);
		kep->navE        = (unsigned long)round(s->e / 1.16415321826934814453e-10);
		kep->navIDot     = (long)round(rad2sc(s->idot) / 1.13686837721616029739e-13);
		kep->navAPowerHalf = (unsigned long)round(s->sqrt_a / 1.90734863281250000000e-06);
		kep->navI0       = (long)round(rad2sc(s->i0) / 4.65661287307739257812e-10);
		kep->navOmegaA0  = (long)round(rad2sc(s->omega0) / 4.65661287307739257812e-10);
		kep->navCrs      = (long)round(s->crs / 3.12500000000000000000e-02);
		kep->navCis      = (long)round(s->cis / 1.86264514923095703125e-09);
		kep->navCus      = (long)round(s->cus / 1.86264514923095703125e-09);
		kep->navCrc      = (long)round(s->crc / 3.12500000000000000000e-02);
		kep->navCic      = (long)round(s->cic / 1.86264514923095703125e-09);
		kep->navCuc      = (long)round(s->cuc / 1.86264514923095703125e-09);

		orb->present = GNSS_OrbitModel_PR_nav_KeplerianSet;
		orb->choice.nav_KeplerianSet = kep;
		sat->gnss_OrbitModel = orb;

		if (ASN_SEQUENCE_ADD(&satlist->list, sat) != 0) {
			free(sat);
			return -1;
		}
	}

	elem->gnss_ID = make_gnss_id(gnss_id);
	elem->gnss_NavigationModel = nav;
	return 0;
}

static int build_nav_almanac(const struct gps_almanac *alm, int count,
			     uint16_t week,
			     GNSS_GenericAssistDataElement_t *elem)
{
	GNSS_Almanac_t *almanac = calloc(1, sizeof(*almanac));

	if (!almanac)
		return -1;

	long *wn = calloc(1, sizeof(long));

	if (!wn) {
		free(almanac);
		return -1;
	}
	*wn = week & 0xFF;
	almanac->weekNumber = wn;
	almanac->completeAlmanacProvided = 1;

	GNSS_AlmanacList_t *alist = calloc(1, sizeof(*alist));

	if (!alist) {
		free(almanac);
		return -1;
	}
	almanac->gnss_AlmanacList = alist;

	for (int i = 0; i < count; i++) {
		const struct gps_almanac *a = &alm[i];
		GNSS_AlmanacElement_t *ae = calloc(1, sizeof(*ae));
		AlmanacNAV_KeplerianSet_t *nav = calloc(1, sizeof(*nav));

		if (!ae || !nav) {
			free(ae);
			free(nav);
			return -1;
		}

		nav->svID = make_sv_id(a->prn, GNSS_ID__gnss_id_gps);
		if (!nav->svID) {
			free(nav);
			free(ae);
			return -1;
		}

		/* Almanac angles are already in semi-circles in DB */
		nav->navAlmE         = (long)round(a->e / 4.76837158203125e-07);
		nav->navAlmDeltaI    = (long)round(a->delta_i / 1.90734863281250000000e-06);
		nav->navAlmOMEGADOT  = (long)round(a->omega_dot / 3.63797880709171295166e-12);
		nav->navAlmSVHealth  = a->health;
		nav->navAlmSqrtA     = (long)round(a->sqrt_a / 4.88281250000000000000e-04);
		nav->navAlmOMEGAo    = (long)round(a->omega0 / 1.19209289550781250000e-07);
		nav->navAlmOmega     = (long)round(a->omega / 1.19209289550781250000e-07);
		nav->navAlmMo        = (long)round(a->m0 / 1.19209289550781250000e-07);
		nav->navAlmaf0       = (long)round(a->af0 / 9.53674316406250000000e-07);
		nav->navAlmaf1       = (long)round(a->af1 / 3.63797880709171295166e-12);

		ae->present = GNSS_AlmanacElement_PR_keplerianNAV_Almanac;
		ae->choice.keplerianNAV_Almanac = nav;

		if (ASN_SEQUENCE_ADD(&alist->list, ae) != 0) {
			free(ae);
			return -1;
		}
	}

	elem->gnss_Almanac = almanac;
	return 0;
}

static int build_utc_model(const struct gps_utc *utc,
			   GNSS_GenericAssistDataElement_t *elem)
{
	GNSS_UTC_Model_t *model = calloc(1, sizeof(*model));
	UTC_ModelSet1_t *m1 = calloc(1, sizeof(*m1));

	if (!model || !m1) {
		free(model);
		free(m1);
		return -1;
	}

	m1->gnss_Utc_A1       = (long)round(utc->a1 / 8.88178419700125232339e-16);
	m1->gnss_Utc_A0       = (long)round(utc->a0 / 9.31322574615478515625e-10);
	m1->gnss_Utc_Tot      = (long)(utc->tot / 4096);
	m1->gnss_Utc_WNt      = utc->wnt & 0xFF;
	m1->gnss_Utc_DeltaTls = utc->dt_ls;
	m1->gnss_Utc_WNlsf    = utc->wnt & 0xFF;
	m1->gnss_Utc_DN       = 0;
	m1->gnss_Utc_DeltaTlsf = utc->dt_ls;

	model->present = GNSS_UTC_Model_PR_utcModel1;
	model->choice.utcModel1 = m1;
	elem->gnss_UTC_Model = model;
	return 0;
}

static int build_klobuchar(const struct gps_iono *iono,
			   GNSS_IonosphericModel_t *iono_model)
{
	KlobucharModelParameter_t *klob = calloc(1, sizeof(*klob));

	if (!klob)
		return -1;

	/* dataID = 00 for GPS (2-bit BIT STRING) */
	if (set_bit_string(&klob->dataID, 0, 2) != 0) {
		free(klob);
		return -1;
	}

	klob->alfa0 = (long)round(iono->alpha[0] / 9.31322574615478515625e-10);
	klob->alfa1 = (long)round(iono->alpha[1] / 7.45058059692382812500e-09);
	klob->alfa2 = (long)round(iono->alpha[2] / 5.96046447753906250000e-08);
	klob->alfa3 = (long)round(iono->alpha[3] / 5.96046447753906250000e-08);
	klob->beta0 = (long)round(iono->beta[0] / 2048.0);
	klob->beta1 = (long)round(iono->beta[1] / 16384.0);
	klob->beta2 = (long)round(iono->beta[2] / 65536.0);
	klob->beta3 = (long)round(iono->beta[3] / 65536.0);

	iono_model->klobucharModel = klob;
	return 0;
}

static int build_reference_time(const struct gps_assist_data *data,
				GNSS_ReferenceTime_t *ref_time)
{
	GNSS_SystemTime_t *systime = calloc(1, sizeof(*systime));

	if (!systime)
		return -1;

	systime->gnss_TimeID = make_gnss_id(GNSS_ID__gnss_id_gps);
	if (!systime->gnss_TimeID) {
		free(systime);
		return -1;
	}

	time_t unix_t = (time_t)data->timestamp;
	long gps_t = (long)(unix_t - GPS_EPOCH_UNIX) + GPS_LEAP_SECONDS;

	systime->gnss_DayNumber = (long)(gps_t / 86400);
	systime->gnss_TimeOfDay = (long)(gps_t % 86400);

	ref_time->gnss_SystemTime = systime;
	return 0;
}

static int build_reference_location(const struct gps_location *loc,
				    GNSS_ReferenceLocation_t *ref_loc)
{
	EllipsoidPointWithAltitudeAndUncertaintyEllipsoid_t *pt =
		calloc(1, sizeof(*pt));

	if (!pt)
		return -1;

	if (loc->latitude >= 0) {
		pt->latitudeSign = EllipsoidPointWithAltitudeAndUncertaintyEllipsoid__latitudeSign_north;
		pt->degreesLatitude = (long)(fabs(loc->latitude) * (double)(1 << 23) / 90.0);
	} else {
		pt->latitudeSign = EllipsoidPointWithAltitudeAndUncertaintyEllipsoid__latitudeSign_south;
		pt->degreesLatitude = (long)(fabs(loc->latitude) * (double)(1 << 23) / 90.0);
	}

	pt->degreesLongitude = (long)(loc->longitude * (double)(1 << 24) / 360.0);

	if (loc->altitude >= 0) {
		pt->altitudeDirection = EllipsoidPointWithAltitudeAndUncertaintyEllipsoid__altitudeDirection_height;
		pt->altitude = loc->altitude;
	} else {
		pt->altitudeDirection = EllipsoidPointWithAltitudeAndUncertaintyEllipsoid__altitudeDirection_depth;
		pt->altitude = -loc->altitude;
	}

	pt->uncertaintySemiMajor = 18;
	pt->uncertaintySemiMinor = 18;
	pt->orientationMajorAxis = 0;
	pt->uncertaintyAltitude = 127;
	pt->confidence = 68;

	ref_loc->threeDlocation = pt;
	return 0;
}

int lpp_build_assistance_data(const struct gps_assist_data *data,
			      uint8_t **out_buf, size_t *out_len)
{
	LPP_Message_t *msg = calloc(1, sizeof(*msg));

	if (!msg)
		return -1;

	int ret = -1;

	msg->endTransaction = 1;

	/* MessageBody → c1 → provideAssistanceData */
	LPP_MessageBody_t *body = calloc(1, sizeof(*body));

	if (!body)
		goto cleanup;
	msg->lpp_MessageBody = body;

	body->present = LPP_MessageBody_PR_c1;
	body->choice.c1.present = LPP_MessageBody__c1_PR_provideAssistanceData;

	ProvideAssistanceData_t *pad =
		&body->choice.c1.choice.provideAssistanceData;
	pad->criticalExtensions.present =
		ProvideAssistanceData__criticalExtensions_PR_c1;
	pad->criticalExtensions.choice.c1.present =
		ProvideAssistanceData__criticalExtensions__c1_PR_provideAssistanceData_r9;

	ProvideAssistanceData_r9_IEs_t *r9 = calloc(1, sizeof(*r9));

	if (!r9)
		goto cleanup;
	pad->criticalExtensions.choice.c1.choice.provideAssistanceData_r9 = r9;

	/* A-GNSS provide assistance data */
	A_GNSS_ProvideAssistanceData_t *agnss = calloc(1, sizeof(*agnss));

	if (!agnss)
		goto cleanup;
	r9->a_gnss_ProvideAssistanceData = agnss;

	/* Common assist data: reference time, location, ionosphere */
	GNSS_CommonAssistData_t *common = calloc(1, sizeof(*common));

	if (!common)
		goto cleanup;
	agnss->gnss_CommonAssistData = common;

	GNSS_ReferenceTime_t *ref_time = calloc(1, sizeof(*ref_time));

	if (!ref_time)
		goto cleanup;
	if (build_reference_time(data, ref_time) != 0)
		goto cleanup;
	common->gnss_ReferenceTime = ref_time;

	if (data->location.valid) {
		GNSS_ReferenceLocation_t *ref_loc =
			calloc(1, sizeof(*ref_loc));

		if (!ref_loc)
			goto cleanup;
		if (build_reference_location(&data->location, ref_loc) != 0)
			goto cleanup;
		common->gnss_ReferenceLocation = ref_loc;
	}

	GNSS_IonosphericModel_t *iono_model = calloc(1, sizeof(*iono_model));

	if (!iono_model)
		goto cleanup;
	if (build_klobuchar(&data->iono, iono_model) != 0)
		goto cleanup;
	common->gnss_IonosphericModel = iono_model;

	/* Generic assist data: per-GNSS elements (SIZE 1..16, skip if empty) */
	GNSS_GenericAssistData_t *generic = NULL;

	if (data->num_sv > 0 || data->num_qzss > 0) {
		generic = calloc(1, sizeof(*generic));
		if (!generic)
			goto cleanup;
		agnss->gnss_GenericAssistData = generic;
	}

	/* GPS element: ephemeris + almanac + UTC */
	if (data->num_sv > 0) {
		GNSS_GenericAssistDataElement_t *gps_elem =
			calloc(1, sizeof(*gps_elem));

		if (!gps_elem)
			goto cleanup;

		if (build_nav_ephemeris(data->sv, data->num_sv,
				       GNSS_ID__gnss_id_gps,
				       gps_elem) != 0)
			goto cleanup;

		if (data->num_alm > 0) {
			if (build_nav_almanac(data->alm, data->num_alm,
					     data->gps_week,
					     gps_elem) != 0)
				goto cleanup;
		}

		if (build_utc_model(&data->utc, gps_elem) != 0)
			goto cleanup;

		if (ASN_SEQUENCE_ADD(&generic->list, gps_elem) != 0)
			goto cleanup;
	}

	/* QZSS element: ephemeris only */
	if (data->num_qzss > 0) {
		GNSS_GenericAssistDataElement_t *qzss_elem =
			calloc(1, sizeof(*qzss_elem));

		if (!qzss_elem)
			goto cleanup;

		if (build_nav_ephemeris(data->qzss, data->num_qzss,
				       GNSS_ID__gnss_id_qzss,
				       qzss_elem) != 0)
			goto cleanup;

		if (ASN_SEQUENCE_ADD(&generic->list, qzss_elem) != 0)
			goto cleanup;
	}

	/* Validate before encoding */
	char errbuf[256];
	size_t errlen = sizeof(errbuf);

	if (asn_check_constraints(&asn_DEF_LPP_Message, msg,
				  errbuf, &errlen) != 0) {
		fprintf(stderr, "LPP constraint error: %s\n", errbuf);
		goto cleanup;
	}

	/* Encode to UPER */
	void *buf_ptr = NULL;
	ssize_t enc_len = uper_encode_to_new_buffer(
		&asn_DEF_LPP_Message, NULL, msg, &buf_ptr);

	if (enc_len < 0)
		goto cleanup;

	*out_buf = buf_ptr;
	*out_len = (size_t)enc_len;
	ret = 0;

cleanup:
	ASN_STRUCT_FREE(asn_DEF_LPP_Message, msg);
	return ret;
}
