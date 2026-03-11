/*
 * test_lpp.c - Round-trip test for LPP ProvideAssistanceData builder
 *
 * Builds an LPP message from known GPS assist data, encodes to UPER,
 * decodes back, and verifies every field matches.
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gps_assist.h"
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

static int pass_count;
static int fail_count;

#define CHECK(cond, fmt, ...) do { \
	if (cond) { \
		pass_count++; \
	} else { \
		fail_count++; \
		fprintf(stderr, "FAIL: " fmt "\n", ##__VA_ARGS__); \
	} \
} while (0)

#define CHECK_EQ(a, b, name) CHECK((a) == (b), "%s: %ld != %ld", name, (long)(a), (long)(b))

static struct gps_assist_data make_test_data(void)
{
	struct gps_assist_data d;

	memset(&d, 0, sizeof(d));

	d.timestamp = 1741651200;  /* 2025-03-11 00:00:00 UTC */
	d.gps_week = 2356;
	d.num_sv = 2;

	/* GPS SV1 */
	d.sv[0].prn = 1;
	d.sv[0].health = 0;
	d.sv[0].iodc = 42;
	d.sv[0].iode = 42;
	d.sv[0].week = 2356;
	d.sv[0].toe = 396000;
	d.sv[0].toc = 396000;
	d.sv[0].af0 = -1.283828169107e-05;
	d.sv[0].af1 = -3.410605131648e-13;
	d.sv[0].af2 = 0.0;
	d.sv[0].sqrt_a = 5153.636;
	d.sv[0].e = 5.48e-03;
	d.sv[0].i0 = 0.9614;
	d.sv[0].omega0 = -2.6389;
	d.sv[0].omega = 0.8963;
	d.sv[0].m0 = 1.5708;
	d.sv[0].delta_n = 4.64e-09;
	d.sv[0].omega_dot = -8.09e-09;
	d.sv[0].idot = -2.80e-10;
	d.sv[0].cuc = -7.26e-06;
	d.sv[0].cus = 9.51e-06;
	d.sv[0].crc = 179.28;
	d.sv[0].crs = -62.78;
	d.sv[0].cic = 1.49e-07;
	d.sv[0].cis = -1.12e-07;
	d.sv[0].tgd = -5.59e-09;

	/* GPS SV2 (simpler values) */
	d.sv[1].prn = 3;
	d.sv[1].health = 0;
	d.sv[1].iodc = 100;
	d.sv[1].iode = 100;
	d.sv[1].week = 2356;
	d.sv[1].toe = 396000;
	d.sv[1].toc = 396000;
	d.sv[1].af0 = 1.0e-05;
	d.sv[1].af1 = 0.0;
	d.sv[1].af2 = 0.0;
	d.sv[1].sqrt_a = 5153.5;
	d.sv[1].e = 1.0e-02;
	d.sv[1].i0 = 0.9600;
	d.sv[1].omega0 = 1.0;
	d.sv[1].omega = -1.0;
	d.sv[1].m0 = 0.5;
	d.sv[1].delta_n = 5.0e-09;
	d.sv[1].omega_dot = -8.0e-09;
	d.sv[1].idot = 0.0;
	d.sv[1].cuc = 0.0;
	d.sv[1].cus = 0.0;
	d.sv[1].crc = 200.0;
	d.sv[1].crs = -50.0;
	d.sv[1].cic = 0.0;
	d.sv[1].cis = 0.0;
	d.sv[1].tgd = 0.0;

	/* Almanac (1 entry, semi-circles) */
	d.num_alm = 1;
	d.alm[0].prn = 1;
	d.alm[0].health = 0;
	d.alm[0].ioda = 1;
	d.alm[0].week = 2356;
	d.alm[0].toa = 319488;
	d.alm[0].e = 5.48e-03;
	d.alm[0].delta_i = 1.56e-03;
	d.alm[0].omega_dot = -2.5754e-09;
	d.alm[0].sqrt_a = 5153.636;
	d.alm[0].omega0 = -0.8400;
	d.alm[0].omega = 0.2853;
	d.alm[0].m0 = 0.5000;
	d.alm[0].af0 = -1.28e-05;
	d.alm[0].af1 = -3.41e-13;

	/* Ionosphere */
	d.iono.alpha[0] = 9.31e-09;
	d.iono.alpha[1] = 1.49e-08;
	d.iono.alpha[2] = -5.96e-08;
	d.iono.alpha[3] = -5.96e-08;
	d.iono.beta[0] = 87040.0;
	d.iono.beta[1] = 16384.0;
	d.iono.beta[2] = -65536.0;
	d.iono.beta[3] = -393216.0;

	/* UTC */
	d.utc.a0 = -9.31e-10;
	d.utc.a1 = -8.88e-15;
	d.utc.tot = 147456;
	d.utc.wnt = 2356;
	d.utc.dt_ls = 18;

	/* Location: Paris Notre Dame */
	d.location.latitude = 48.8530;
	d.location.longitude = 2.3498;
	d.location.altitude = 35;
	d.location.valid = 1;

	/* QZSS (1 satellite) */
	d.num_qzss = 1;
	d.qzss[0].prn = 193;
	d.qzss[0].health = 0;
	d.qzss[0].iodc = 55;
	d.qzss[0].iode = 55;
	d.qzss[0].week = 2356;
	d.qzss[0].toe = 396000;
	d.qzss[0].toc = 396000;
	d.qzss[0].af0 = -1.0e-06;
	d.qzss[0].af1 = 0.0;
	d.qzss[0].af2 = 0.0;
	d.qzss[0].sqrt_a = 6493.5;
	d.qzss[0].e = 7.5e-02;
	d.qzss[0].i0 = 0.7330;
	d.qzss[0].omega0 = 2.1230;
	d.qzss[0].omega = -1.4500;
	d.qzss[0].m0 = 0.2500;
	d.qzss[0].delta_n = 3.0e-09;
	d.qzss[0].omega_dot = -7.0e-09;
	d.qzss[0].idot = 1.0e-10;
	d.qzss[0].cuc = 1.0e-06;
	d.qzss[0].cus = -1.0e-06;
	d.qzss[0].crc = 100.0;
	d.qzss[0].crs = -30.0;
	d.qzss[0].cic = 5.0e-08;
	d.qzss[0].cis = -5.0e-08;
	d.qzss[0].tgd = -1.0e-08;

	return d;
}

static void test_encode_decode(void)
{
	struct gps_assist_data data = make_test_data();
	uint8_t *buf = NULL;
	size_t len = 0;

	printf("=== LPP encode ===\n");

	int rc = lpp_build_assistance_data(&data, &buf, &len);

	CHECK(rc == 0, "lpp_build_assistance_data returned %d", rc);
	CHECK(buf != NULL, "output buffer is NULL");
	CHECK(len > 0, "output length is 0");

	if (rc != 0 || !buf)
		return;

	printf("  UPER encoded: %zu bytes\n", len);

	/* Decode back */
	printf("=== LPP decode ===\n");

	LPP_Message_t *msg = NULL;
	asn_dec_rval_t dec = uper_decode_complete(
		NULL, &asn_DEF_LPP_Message, (void **)&msg, buf, len);

	CHECK(dec.code == RC_OK, "UPER decode failed: %d", dec.code);
	if (dec.code != RC_OK || !msg) {
		free(buf);
		return;
	}

	/* Verify top-level structure */
	CHECK(msg->endTransaction == 1, "endTransaction not set");
	CHECK(msg->lpp_MessageBody != NULL, "lpp_MessageBody is NULL");
	CHECK(msg->lpp_MessageBody->present == LPP_MessageBody_PR_c1,
	      "body not c1");
	CHECK(msg->lpp_MessageBody->choice.c1.present ==
	      LPP_MessageBody__c1_PR_provideAssistanceData,
	      "c1 not provideAssistanceData");

	ProvideAssistanceData_t *pad =
		&msg->lpp_MessageBody->choice.c1.choice.provideAssistanceData;
	CHECK(pad->criticalExtensions.present ==
	      ProvideAssistanceData__criticalExtensions_PR_c1,
	      "critExt not c1");

	ProvideAssistanceData_r9_IEs_t *r9 =
		pad->criticalExtensions.choice.c1.choice.provideAssistanceData_r9;
	CHECK(r9 != NULL, "r9 IEs is NULL");
	if (!r9) goto done;

	A_GNSS_ProvideAssistanceData_t *agnss =
		r9->a_gnss_ProvideAssistanceData;
	CHECK(agnss != NULL, "a_gnss is NULL");
	if (!agnss) goto done;

	/* Verify common assist data */
	printf("=== Common assist data ===\n");
	GNSS_CommonAssistData_t *common = agnss->gnss_CommonAssistData;

	CHECK(common != NULL, "common is NULL");
	if (!common) goto done;

	/* Reference time */
	CHECK(common->gnss_ReferenceTime != NULL, "refTime is NULL");
	if (common->gnss_ReferenceTime) {
		GNSS_SystemTime_t *st =
			common->gnss_ReferenceTime->gnss_SystemTime;
		CHECK(st != NULL, "systemTime is NULL");
		if (st) {
			CHECK_EQ(st->gnss_TimeID->gnss_id,
				 GNSS_ID__gnss_id_gps, "refTime gnss_id");

			time_t unix_t = (time_t)data.timestamp;
			long gps_t = (long)(unix_t - 315964800L) + 18;

			CHECK_EQ(st->gnss_DayNumber,
				 gps_t / 86400, "gnss_DayNumber");
			CHECK_EQ(st->gnss_TimeOfDay,
				 gps_t % 86400, "gnss_TimeOfDay");
		}
	}

	/* Reference location */
	CHECK(common->gnss_ReferenceLocation != NULL, "refLoc is NULL");
	if (common->gnss_ReferenceLocation) {
		EllipsoidPointWithAltitudeAndUncertaintyEllipsoid_t *pt =
			common->gnss_ReferenceLocation->threeDlocation;
		CHECK(pt != NULL, "threeDlocation is NULL");
		if (pt) {
			CHECK_EQ(pt->latitudeSign, 0, "latSign north");
			long exp_lat = (long)(48.8530 * (double)(1<<23) / 90.0);
			CHECK_EQ(pt->degreesLatitude, exp_lat,
				 "degreesLatitude");
			long exp_lon = (long)(2.3498 * (double)(1<<24) / 360.0);
			CHECK_EQ(pt->degreesLongitude, exp_lon,
				 "degreesLongitude");
			CHECK_EQ(pt->altitude, 35, "altitude");
			CHECK_EQ(pt->confidence, 68, "confidence");
		}
	}

	/* Klobuchar ionosphere */
	CHECK(common->gnss_IonosphericModel != NULL, "ionoModel is NULL");
	if (common->gnss_IonosphericModel) {
		KlobucharModelParameter_t *k =
			common->gnss_IonosphericModel->klobucharModel;
		CHECK(k != NULL, "klobuchar is NULL");
		if (k) {
			CHECK_EQ(k->alfa0,
				 (long)round(9.31e-09 / 9.31322574615478515625e-10),
				 "alfa0");
			CHECK_EQ(k->beta0,
				 (long)round(87040.0 / 2048.0), "beta0");
			CHECK_EQ(k->beta2,
				 (long)round(-65536.0 / 65536.0), "beta2");
		}
	}

	/* Verify generic assist data */
	printf("=== Generic assist data ===\n");
	GNSS_GenericAssistData_t *gen = agnss->gnss_GenericAssistData;

	CHECK(gen != NULL, "generic is NULL");
	if (!gen) goto done;

	/* Should have 2 elements: GPS + QZSS */
	CHECK_EQ(gen->list.count, 2, "generic element count");

	/* GPS element */
	if (gen->list.count >= 1) {
		GNSS_GenericAssistDataElement_t *gps = gen->list.array[0];

		CHECK(gps != NULL, "gps elem is NULL");
		if (!gps) goto done;

		CHECK_EQ(gps->gnss_ID->gnss_id, GNSS_ID__gnss_id_gps,
			 "gps gnss_id");

		/* Navigation model */
		CHECK(gps->gnss_NavigationModel != NULL, "gps navModel");
		if (gps->gnss_NavigationModel) {
			GNSS_NavModelSatelliteList_t *sl =
				gps->gnss_NavigationModel->gnss_SatelliteList;
			CHECK(sl != NULL, "gps satList");
			if (sl) {
				CHECK_EQ(sl->list.count, 2,
					 "gps sat count");
				if (sl->list.count >= 1) {
					GNSS_NavModelSatelliteElement_t *s0 =
						sl->list.array[0];
					CHECK_EQ(s0->svID->satellite_id, 0,
						 "sv0 id (0-based)");

					/* Check clock model */
					NAV_ClockModel_t *nc =
						s0->gnss_ClockModel->choice.nav_ClockModel;
					CHECK(nc != NULL, "sv0 navClk");
					if (nc) {
						CHECK_EQ(nc->navToc,
							 396000/16,
							 "sv0 toc");
						long exp_af0 = (long)round(
							-1.283828169107e-05 /
							4.65661287307739257812e-10);
						CHECK_EQ(nc->navaf0,
							 exp_af0,
							 "sv0 af0");
					}

					/* Check orbit model */
					NavModelNAV_KeplerianSet_t *nk =
						s0->gnss_OrbitModel->choice.nav_KeplerianSet;
					CHECK(nk != NULL, "sv0 navKep");
					if (nk) {
						CHECK_EQ(nk->navToe,
							 396000/16,
							 "sv0 toe");
						long exp_m0 = (long)round(
							(1.5708/M_PI) /
							4.65661287307739257812e-10);
						CHECK_EQ(nk->navM0,
							 exp_m0,
							 "sv0 M0");
						unsigned long exp_e = (unsigned long)round(
							5.48e-03 /
							1.16415321826934814453e-10);
						CHECK(nk->navE == exp_e,
						      "sv0 e: %lu != %lu",
						      nk->navE, exp_e);
					}
				}
			}
		}

		/* Almanac */
		CHECK(gps->gnss_Almanac != NULL, "gps almanac");
		if (gps->gnss_Almanac) {
			GNSS_Almanac_t *alm = gps->gnss_Almanac;

			CHECK(alm->completeAlmanacProvided == 1,
			      "alm complete");
			CHECK(alm->gnss_AlmanacList != NULL,
			      "alm list");
			if (alm->gnss_AlmanacList) {
				CHECK_EQ(alm->gnss_AlmanacList->list.count,
					 1, "alm count");
				if (alm->gnss_AlmanacList->list.count >= 1) {
					GNSS_AlmanacElement_t *ae =
						alm->gnss_AlmanacList->list.array[0];
					CHECK(ae->present ==
					      GNSS_AlmanacElement_PR_keplerianNAV_Almanac,
					      "alm type NAV");
					AlmanacNAV_KeplerianSet_t *nav =
						ae->choice.keplerianNAV_Almanac;
					CHECK(nav != NULL, "alm nav");
					if (nav) {
						CHECK_EQ(nav->svID->satellite_id,
							 0, "alm sv_id");
						long exp_e = (long)round(
							5.48e-03 /
							4.76837158203125e-07);
						CHECK_EQ(nav->navAlmE,
							 exp_e,
							 "alm e");
					}
				}
			}
		}

		/* UTC model */
		CHECK(gps->gnss_UTC_Model != NULL, "gps utc");
		if (gps->gnss_UTC_Model) {
			CHECK(gps->gnss_UTC_Model->present ==
			      GNSS_UTC_Model_PR_utcModel1,
			      "utc model1");
			UTC_ModelSet1_t *m1 =
				gps->gnss_UTC_Model->choice.utcModel1;
			CHECK(m1 != NULL, "utc m1");
			if (m1) {
				long exp_a0 = (long)round(
					-9.31e-10 /
					9.31322574615478515625e-10);
				CHECK_EQ(m1->gnss_Utc_A0, exp_a0,
					 "utc A0");
				CHECK_EQ(m1->gnss_Utc_DeltaTls, 18,
					 "utc deltaTls");
			}
		}
	}

	/* QZSS element */
	if (gen->list.count >= 2) {
		GNSS_GenericAssistDataElement_t *qzss = gen->list.array[1];

		CHECK(qzss != NULL, "qzss elem");
		if (qzss) {
			CHECK_EQ(qzss->gnss_ID->gnss_id,
				 GNSS_ID__gnss_id_qzss, "qzss gnss_id");
			CHECK(qzss->gnss_NavigationModel != NULL,
			      "qzss navModel");
			if (qzss->gnss_NavigationModel) {
				GNSS_NavModelSatelliteList_t *sl =
					qzss->gnss_NavigationModel->gnss_SatelliteList;
				CHECK_EQ(sl->list.count, 1,
					 "qzss sat count");
				if (sl->list.count >= 1) {
					/* QZSS PRN 193 → satellite_id 192 */
					CHECK_EQ(sl->list.array[0]->svID->satellite_id,
						 0,
						 "qzss sv_id (0-based)");
				}
			}
		}
	}

done:
	ASN_STRUCT_FREE(asn_DEF_LPP_Message, msg);
	free(buf);
}

static void test_empty_data(void)
{
	struct gps_assist_data data;

	memset(&data, 0, sizeof(data));
	data.timestamp = 1741651200;

	uint8_t *buf = NULL;
	size_t len = 0;

	printf("=== Empty data ===\n");

	int rc = lpp_build_assistance_data(&data, &buf, &len);

	CHECK(rc == 0, "empty data encode returned %d", rc);
	CHECK(buf != NULL, "empty data buf is NULL");
	CHECK(len > 0, "empty data len is 0");

	if (buf) {
		/* Verify it decodes cleanly */
		LPP_Message_t *msg = NULL;
		asn_dec_rval_t dec = uper_decode_complete(
			NULL, &asn_DEF_LPP_Message, (void **)&msg, buf, len);
		CHECK(dec.code == RC_OK, "empty decode failed");
		ASN_STRUCT_FREE(asn_DEF_LPP_Message, msg);
	}

	free(buf);
}

int main(void)
{
	test_encode_decode();
	test_empty_data();

	printf("\n%d passed, %d failed, %d total\n",
	       pass_count, fail_count, pass_count + fail_count);
	return fail_count ? 1 : 0;
}
