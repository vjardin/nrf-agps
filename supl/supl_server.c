/*
 * supl_server.c - SUPL 2.0 A-GNSS server over TCP/TLS using libevent
 *
 * Handles SET-initiated SUPL sessions:
 *   recv SUPL START → send SUPL RESPONSE
 *   recv SUPL POS INIT → send SUPL POS (LPP) + SUPL END
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include <event2/bufferevent.h>
#include <event2/bufferevent_ssl.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/event.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "supl_server.h"
#include "supl_codec.h"
#include "sqlitedb.h"
#include "lpp_builder.h"
#include "gps_assist.h"

#include "ULP-PDU.h"
#include "UlpMessage.h"
#include "SessionID.h"
#include "SetSessionID.h"
#include "SETId.h"
#include "IPAddress.h"

/* Maximum ULP PDU size we'll accept (64 KB) */
#define MAX_ULP_SIZE 65536

/* SUPL session state */
enum supl_state {
	STATE_WAIT_START,
	STATE_WAIT_POS_INIT,
	STATE_DONE
};

struct supl_session {
	enum supl_state state;
	long set_session_id;
	uint8_t slp_session_id[4];
	uint8_t set_id_buf[16];
	size_t set_id_len;
	int set_id_type;
	const struct supl_server_config *cfg;
	uint32_t session_counter;
};

static uint32_t next_session_id;

static void extract_set_id(const ULP_PDU_t *pdu, struct supl_session *sess)
{
	if (!pdu->sessionID || !pdu->sessionID->setSessionID ||
	    !pdu->sessionID->setSessionID->setId)
		return;

	SETId_t *sid = pdu->sessionID->setSessionID->setId;

	sess->set_id_type = sid->present;

	switch (sid->present) {
	case SETId_PR_msisdn:
		sess->set_id_len = sid->choice.msisdn.size;
		if (sess->set_id_len > sizeof(sess->set_id_buf))
			sess->set_id_len = sizeof(sess->set_id_buf);
		memcpy(sess->set_id_buf, sid->choice.msisdn.buf,
		       sess->set_id_len);
		break;
	case SETId_PR_imsi:
		sess->set_id_len = sid->choice.imsi.size;
		if (sess->set_id_len > sizeof(sess->set_id_buf))
			sess->set_id_len = sizeof(sess->set_id_buf);
		memcpy(sess->set_id_buf, sid->choice.imsi.buf,
		       sess->set_id_len);
		break;
	case SETId_PR_iPAddress:
		if (sid->choice.iPAddress) {
			IPAddress_t *ip = sid->choice.iPAddress;

			if (ip->present == IPAddress_PR_ipv4Address) {
				sess->set_id_len = 4;
				memcpy(sess->set_id_buf,
				       ip->choice.ipv4Address.buf, 4);
			} else if (ip->present == IPAddress_PR_ipv6Address) {
				sess->set_id_len = 16;
				memcpy(sess->set_id_buf,
				       ip->choice.ipv6Address.buf, 16);
			}
		}
		break;
	default:
		/* Use a dummy IMSI */
		sess->set_id_type = SETId_PR_imsi;
		memset(sess->set_id_buf, 0, 8);
		sess->set_id_len = 8;
		break;
	}
}

static void handle_supl_start(struct bufferevent *bev,
			      const ULP_PDU_t *pdu,
			      struct supl_session *sess)
{
	sess->set_session_id = supl_get_set_session_id(pdu);
	extract_set_id(pdu, sess);

	/* Assign SLP session ID */
	uint32_t sid = ++next_session_id;

	sess->slp_session_id[0] = (sid >> 24) & 0xFF;
	sess->slp_session_id[1] = (sid >> 16) & 0xFF;
	sess->slp_session_id[2] = (sid >> 8) & 0xFF;
	sess->slp_session_id[3] = sid & 0xFF;

	if (sess->cfg->verbose)
		fprintf(stderr, "  SUPL START: set_session=%ld, slp_session=%u\n",
			sess->set_session_id, sid);

	/* Send SUPL RESPONSE */
	uint8_t *resp_buf = NULL;
	size_t resp_len = 0;

	if (supl_build_response(sess->set_session_id,
				sess->slp_session_id,
				sess->set_id_buf, sess->set_id_len,
				sess->set_id_type,
				&resp_buf, &resp_len) != 0) {
		fprintf(stderr, "  Failed to build SUPL RESPONSE\n");
		bufferevent_free(bev);
		return;
	}

	if (sess->cfg->verbose)
		fprintf(stderr, "  Sending SUPL RESPONSE (%zu bytes)\n",
			resp_len);

	bufferevent_write(bev, resp_buf, resp_len);
	free(resp_buf);

	sess->state = STATE_WAIT_POS_INIT;
}

static void handle_supl_pos_init(struct bufferevent *bev,
				 const ULP_PDU_t *pdu,
				 struct supl_session *sess)
{
	(void)pdu;

	if (sess->cfg->verbose)
		fprintf(stderr, "  SUPL POS INIT received\n");

	/* Read assistance data from SQLite */
	struct gps_assist_data data;

	if (sqlitedb_read_latest(sess->cfg->db_path, &data) != 0) {
		fprintf(stderr, "  Failed to read database: %s\n",
			sess->cfg->db_path);
		bufferevent_free(bev);
		return;
	}

	if (sess->cfg->verbose)
		fprintf(stderr, "  DB: %d GPS + %d QZSS SVs, %d almanacs\n",
			data.num_sv, data.num_qzss, data.num_alm);

	/* Build LPP assistance data */
	uint8_t *lpp_buf = NULL;
	size_t lpp_len = 0;

	if (lpp_build_assistance_data(&data, &lpp_buf, &lpp_len) != 0) {
		fprintf(stderr, "  Failed to build LPP message\n");
		bufferevent_free(bev);
		return;
	}

	if (sess->cfg->verbose)
		fprintf(stderr, "  LPP encoded: %zu bytes\n", lpp_len);

	/* Build and send SUPL POS with LPP payload */
	uint8_t *pos_buf = NULL;
	size_t pos_len = 0;

	if (supl_build_pos(sess->set_session_id,
			   sess->slp_session_id,
			   sess->set_id_buf, sess->set_id_len,
			   sess->set_id_type,
			   lpp_buf, lpp_len,
			   &pos_buf, &pos_len) != 0) {
		fprintf(stderr, "  Failed to build SUPL POS\n");
		free(lpp_buf);
		bufferevent_free(bev);
		return;
	}
	free(lpp_buf);

	if (sess->cfg->verbose)
		fprintf(stderr, "  Sending SUPL POS (%zu bytes)\n", pos_len);

	bufferevent_write(bev, pos_buf, pos_len);
	free(pos_buf);

	/* Build and send SUPL END */
	uint8_t *end_buf = NULL;
	size_t end_len = 0;

	if (supl_build_end(sess->set_session_id,
			   sess->slp_session_id,
			   sess->set_id_buf, sess->set_id_len,
			   sess->set_id_type,
			   &end_buf, &end_len) != 0) {
		fprintf(stderr, "  Failed to build SUPL END\n");
		bufferevent_free(bev);
		return;
	}

	if (sess->cfg->verbose)
		fprintf(stderr, "  Sending SUPL END (%zu bytes)\n", end_len);

	bufferevent_write(bev, end_buf, end_len);
	free(end_buf);

	sess->state = STATE_DONE;
}

static void read_cb(struct bufferevent *bev, void *ctx)
{
	struct supl_session *sess = ctx;
	struct evbuffer *input = bufferevent_get_input(bev);

	for (;;) {
		size_t avail = evbuffer_get_length(input);

		if (avail < 2)
			return;

		/*
		 * ULP PDU framing: the length field is encoded in the first
		 * 2 bytes of the UPER bitstream (ULP-PDU.length is the first
		 * field, INTEGER(0..65535), 16 bits in UPER).
		 */
		uint8_t hdr[2];

		evbuffer_copyout(input, hdr, 2);
		size_t pdu_len = ((size_t)hdr[0] << 8) | hdr[1];

		if (pdu_len < 2 || pdu_len > MAX_ULP_SIZE) {
			fprintf(stderr, "  Invalid ULP length: %zu\n",
				pdu_len);
			bufferevent_free(bev);
			return;
		}

		if (avail < pdu_len)
			return;

		/* Read the full PDU */
		uint8_t *buf = malloc(pdu_len);

		if (!buf) {
			bufferevent_free(bev);
			return;
		}
		evbuffer_remove(input, buf, pdu_len);

		ULP_PDU_t *pdu = supl_decode_pdu(buf, pdu_len);

		free(buf);
		if (!pdu) {
			fprintf(stderr, "  Failed to decode ULP PDU\n");
			bufferevent_free(bev);
			return;
		}

		int msg_type = supl_get_message_type(pdu);

		switch (msg_type) {
		case UlpMessage_PR_msSUPLSTART:
			if (sess->state == STATE_WAIT_START) {
				handle_supl_start(bev, pdu, sess);
			} else {
				fprintf(stderr,
					"  Unexpected SUPL START in state %d\n",
					sess->state);
				supl_pdu_free(pdu);
				bufferevent_free(bev);
				return;
			}
			break;

		case UlpMessage_PR_msSUPLPOSINIT:
			if (sess->state == STATE_WAIT_POS_INIT) {
				handle_supl_pos_init(bev, pdu, sess);
			} else {
				fprintf(stderr,
					"  Unexpected SUPL POS INIT in state %d\n",
					sess->state);
				supl_pdu_free(pdu);
				bufferevent_free(bev);
				return;
			}
			break;

		case UlpMessage_PR_msSUPLEND:
			if (sess->cfg->verbose)
				fprintf(stderr, "  Client sent SUPL END\n");
			supl_pdu_free(pdu);
			bufferevent_free(bev);
			return;

		default:
			fprintf(stderr, "  Unhandled ULP message type: %d\n",
				msg_type);
			supl_pdu_free(pdu);
			bufferevent_free(bev);
			return;
		}

		supl_pdu_free(pdu);

		if (sess->state == STATE_DONE)
			return;
	}
}

static void event_cb(struct bufferevent *bev, short what, void *ctx)
{
	struct supl_session *sess = ctx;

	if (what & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
		if (sess->cfg->verbose) {
			if (what & BEV_EVENT_EOF)
				fprintf(stderr, "  Connection closed\n");
			else
				fprintf(stderr, "  Connection error\n");
		}
		bufferevent_free(bev);
	}
}

static void accept_cb(struct evconnlistener *listener,
		      evutil_socket_t fd,
		      struct sockaddr *addr, int addrlen,
		      void *ctx)
{
	const struct supl_server_config *cfg = ctx;
	struct event_base *base = evconnlistener_get_base(listener);

	(void)addrlen;

	char addr_str[INET6_ADDRSTRLEN] = {0};

	if (addr->sa_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)addr;

		inet_ntop(AF_INET, &sin->sin_addr, addr_str, sizeof(addr_str));
		if (cfg->verbose)
			fprintf(stderr, "Connection from %s:%d\n",
				addr_str, ntohs(sin->sin_port));
	} else if (addr->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr;

		inet_ntop(AF_INET6, &sin6->sin6_addr,
			  addr_str, sizeof(addr_str));
		if (cfg->verbose)
			fprintf(stderr, "Connection from [%s]:%d\n",
				addr_str, ntohs(sin6->sin6_port));
	}

	struct supl_session *sess = calloc(1, sizeof(*sess));

	if (!sess) {
		evutil_closesocket(fd);
		return;
	}
	sess->state = STATE_WAIT_START;
	sess->cfg = cfg;

	struct bufferevent *bev;

	bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
	if (!bev) {
		free(sess);
		evutil_closesocket(fd);
		return;
	}

	bufferevent_setcb(bev, read_cb, NULL, event_cb, sess);
	bufferevent_enable(bev, EV_READ | EV_WRITE);
}

static void accept_error_cb(struct evconnlistener *listener, void *ctx)
{
	(void)ctx;
	int err = EVUTIL_SOCKET_ERROR();

	fprintf(stderr, "Accept error: %s\n",
		evutil_socket_error_to_string(err));
	event_base_loopexit(evconnlistener_get_base(listener), NULL);
}

/* TLS accept callback */
static SSL_CTX *g_ssl_ctx;

static void accept_tls_cb(struct evconnlistener *listener,
			   evutil_socket_t fd,
			   struct sockaddr *addr, int addrlen,
			   void *ctx)
{
	const struct supl_server_config *cfg = ctx;
	struct event_base *base = evconnlistener_get_base(listener);

	(void)addrlen;

	char addr_str[INET6_ADDRSTRLEN] = {0};

	if (addr->sa_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)addr;

		inet_ntop(AF_INET, &sin->sin_addr, addr_str, sizeof(addr_str));
		if (cfg->verbose)
			fprintf(stderr, "TLS connection from %s:%d\n",
				addr_str, ntohs(sin->sin_port));
	}

	struct supl_session *sess = calloc(1, sizeof(*sess));

	if (!sess) {
		evutil_closesocket(fd);
		return;
	}
	sess->state = STATE_WAIT_START;
	sess->cfg = cfg;

	SSL *ssl = SSL_new(g_ssl_ctx);

	if (!ssl) {
		free(sess);
		evutil_closesocket(fd);
		return;
	}

	struct bufferevent *bev = bufferevent_openssl_socket_new(
		base, fd, ssl, BUFFEREVENT_SSL_ACCEPTING,
		BEV_OPT_CLOSE_ON_FREE);

	if (!bev) {
		SSL_free(ssl);
		free(sess);
		evutil_closesocket(fd);
		return;
	}

	bufferevent_setcb(bev, read_cb, NULL, event_cb, sess);
	bufferevent_enable(bev, EV_READ | EV_WRITE);
}

static struct event_base *g_base;

static void signal_cb(evutil_socket_t sig, short events, void *arg)
{
	(void)sig;
	(void)events;
	struct event_base *base = arg;

	fprintf(stderr, "\nShutting down...\n");
	event_base_loopexit(base, NULL);
}

int supl_server_run(const struct supl_server_config *cfg)
{
	if (!cfg->db_path) {
		fprintf(stderr, "Error: database path required (-d)\n");
		return -1;
	}

	if (!cfg->no_tls && !cfg->cert_file) {
		fprintf(stderr, "Error: TLS certificate required (-c/-k) "
			"or use --no-tls\n");
		return -1;
	}

	int port = cfg->port;

	if (port == 0)
		port = cfg->no_tls ? 7276 : 7275;

	const char *bind_addr = cfg->bind_addr;

	if (!bind_addr)
		bind_addr = "0.0.0.0";

	/* Verify database is readable */
	struct gps_assist_data test_data;

	if (sqlitedb_read_latest(cfg->db_path, &test_data) != 0) {
		fprintf(stderr, "Error: cannot read database: %s\n",
			cfg->db_path);
		return -1;
	}
	fprintf(stderr, "Database: %d GPS + %d QZSS SVs, week %d\n",
		test_data.num_sv, test_data.num_qzss, test_data.gps_week);

	/* Setup TLS if needed */
	if (!cfg->no_tls) {
		SSL_library_init();
		SSL_load_error_strings();

		g_ssl_ctx = SSL_CTX_new(TLS_server_method());
		if (!g_ssl_ctx) {
			fprintf(stderr, "SSL_CTX_new failed\n");
			return -1;
		}

		if (SSL_CTX_use_certificate_chain_file(g_ssl_ctx,
						       cfg->cert_file) != 1) {
			fprintf(stderr, "Failed to load certificate: %s\n",
				cfg->cert_file);
			ERR_print_errors_fp(stderr);
			SSL_CTX_free(g_ssl_ctx);
			return -1;
		}

		if (SSL_CTX_use_PrivateKey_file(g_ssl_ctx, cfg->key_file,
						SSL_FILETYPE_PEM) != 1) {
			fprintf(stderr, "Failed to load private key: %s\n",
				cfg->key_file);
			ERR_print_errors_fp(stderr);
			SSL_CTX_free(g_ssl_ctx);
			return -1;
		}

		if (!SSL_CTX_check_private_key(g_ssl_ctx)) {
			fprintf(stderr, "Certificate/key mismatch\n");
			SSL_CTX_free(g_ssl_ctx);
			return -1;
		}
	}

	struct event_base *base = event_base_new();

	if (!base) {
		fprintf(stderr, "event_base_new failed\n");
		return -1;
	}
	g_base = base;

	struct sockaddr_in sin;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons((uint16_t)port);
	inet_pton(AF_INET, bind_addr, &sin.sin_addr);

	evconnlistener_cb accept_fn = cfg->no_tls ? accept_cb : accept_tls_cb;

	struct evconnlistener *listener = evconnlistener_new_bind(
		base, accept_fn, (void *)cfg,
		LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE, -1,
		(struct sockaddr *)&sin, sizeof(sin));

	if (!listener) {
		fprintf(stderr, "Cannot bind to %s:%d: %s\n",
			bind_addr, port, strerror(errno));
		event_base_free(base);
		return -1;
	}

	evconnlistener_set_error_cb(listener, accept_error_cb);

	struct event *sigint = evsignal_new(base, SIGINT, signal_cb, base);
	struct event *sigterm = evsignal_new(base, SIGTERM, signal_cb, base);

	event_add(sigint, NULL);
	event_add(sigterm, NULL);

	fprintf(stderr, "SUPL server listening on %s:%d (%s)\n",
		bind_addr, port, cfg->no_tls ? "plain TCP" : "TLS");

	event_base_dispatch(base);

	event_free(sigint);
	event_free(sigterm);
	evconnlistener_free(listener);
	event_base_free(base);

	if (g_ssl_ctx) {
		SSL_CTX_free(g_ssl_ctx);
		g_ssl_ctx = NULL;
	}

	return 0;
}
