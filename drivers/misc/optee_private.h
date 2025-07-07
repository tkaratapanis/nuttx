/****************************************************************************
 * drivers/misc/optee_private.h
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

#ifndef __DRIVERS_MISC_OPTEE_PRIVATE_H
#define __DRIVERS_MISC_OPTEE_PRIVATE_H


#include <sys/types.h>
#include "optee.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Some Global Platform error codes used in this driver. */
#define TEE_SUCCESS              0x00000000
#define TEE_ERROR_GENERIC        0xFFFF0000
#define TEE_ERROR_BAD_PARAMETERS 0xFFFF0006
#define TEE_ERROR_NOT_SUPPORTED  0xFFFF000A
#define TEE_ERROR_COMMUNICATION  0xFFFF000E
#define TEE_ERROR_OUT_OF_MEMORY  0xFFFF000C
#define TEE_ERROR_SHORT_BUFFER   0xFFFF0010

#define TEE_ORIGIN_COMMS   0x00000002


void optee_rpc_handle_cmd(FAR struct optee_priv_data *priv,
                          struct optee_shm *shm, void **last_page_list);

/****************************************************************************
 * Name: optee_supplicant_cmd_alloc
 *
 * Description:
 *   Request from OP-TEE to suspend the current nuttx process.
 *
 * Input Parameters:
 *   arg  - Pointer to the RPC message argument, allocated in the shared page
 *          by the secure world. A copy of this message will be sent to the
 *          supplicant process that runs in userspace for further processing.
 *
 * Returned Value:
 *   None.  Result codes are written into arg->ret.
 *   Information about the shared memory is passed through arg->params
 *
 ****************************************************************************/
int32_t optee_supplicant_cmd_alloc(FAR struct optee_priv_data *priv,
  size_t sz, struct optee_shm **shm);

#endif /*OPTEE_PRIVATE_H*/
