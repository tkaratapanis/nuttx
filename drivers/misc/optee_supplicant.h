/****************************************************************************
 * drivers/misc/optee_supplicant.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __DRIVERS_MISC_OPTEE_SUPPLICANT_H
#define __DRIVERS_MISC_OPTEE_SUPPLICANT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <sys/types.h>
#include "optee.h"

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

int32_t optee_supplicant_cmd_alloc(FAR struct optee_priv_data *priv,
                                   size_t sz, struct optee_shm **shm);

FAR struct idr_s *optee_supplicant_get_shm_idr(void);
FAR struct idr_s *optee_supplicant_init_shm_idr(void);
bool optee_supplicant_running(void);

void optee_supplicant_init(void);
void optee_supplicant_uninit(void);
int optee_convert_error(uint32_t oterr);

uint32_t optee_supplicant_request(uint32_t func, size_t num_params,
                             FAR struct tee_ioctl_param *param);

int optee_supplicant_send(uint32_t ret, uint32_t num_params,
                    FAR struct tee_ioctl_param *param);

int optee_supplicant_recv(FAR uint32_t *func, FAR uint32_t *num_params,
                    FAR struct tee_ioctl_param *params);

void optee_rpc_handle_cmd(FAR struct optee_priv_data *priv,
                          struct optee_shm *shm, void **last_page_list);

#endif /* __DRIVERS_MISC_OPTEE_SUPPLICANT_H */
