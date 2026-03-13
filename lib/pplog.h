/*
 * pplog.h - Processing pipeline log macros
 *
 * Lightweight logging for the data pipeline (download, parse, store,
 * generate). System-level errors (fopen, bind, ...) should use err(3)
 * instead.
 *
 * Copyright (C) 2026 Free Mobile
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef PPLOG_H
#define PPLOG_H

#include <stdio.h>

#define pplog_err(fmt, ...) fprintf(stderr, "error: " fmt "\n", ##__VA_ARGS__)
#define pplog_warn(fmt, ...) fprintf(stderr, "warning: " fmt "\n", ##__VA_ARGS__)
#define pplog_info(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)

#endif /* PPLOG_H */
