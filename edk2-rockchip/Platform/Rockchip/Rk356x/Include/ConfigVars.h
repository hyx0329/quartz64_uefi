/** @file
 *
 *  Copyright (c) 2023, Jared McNeill <jmcneill@invisible.ca>
 *  Copyright (c) 2020, Andrei Warkentin <andrey.warkentin@gmail.com>
 *  Copyright (c) 2020, ARM Limited. All rights reserved.
 *           
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef CONFIG_VARS_H
#define CONFIG_VARS_H

typedef struct {
#define SYSTEM_TABLE_MODE_ACPI 0
#define SYSTEM_TABLE_MODE_BOTH 1
#define SYSTEM_TABLE_MODE_DT   2
  UINT32 Mode;
} SYSTEM_TABLE_MODE_VARSTORE_DATA;

#endif /* CONFIG_VARS_H */