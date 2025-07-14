/****************************************************************************
 * drivers/misc/optee_rpc.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "optee.h"
#include "optee_msg.h"
#include <arch/syscall.h>
#include <stdint.h>
#include <debug.h>
#include "optee_supplicant.h"
#include "optee_rpc.h"
#include <nuttx/signal.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: optee_rpc_handle_cmd_get_time
 *
 * Description:
 *   Return REE wall-clock time (seconds + nanoseconds) to secure world.
 *
 * Parameters:
 *   arg - [In/Out] Pointer to the RPC message argument located in a shared
 *         page and filled by the secure world.
 *
 * Returned Value:
 *   The time is written to:
 *     arg->params[0].u.value.a   containing seconds since epoch
 *     arg->params[0].u.value.b   containing nanoseconds
 *   Result code is written to arg->ret.
 *
 ****************************************************************************/

static void optee_rpc_handle_cmd_get_time(struct optee_msg_arg *arg)
{
  struct timespec ts;

  /* OP-TEE parameter validation. */

  if (arg->num_params != 1 ||
      (arg->params[0].attr & OPTEE_MSG_ATTR_TYPE_MASK)
      != OPTEE_MSG_ATTR_TYPE_VALUE_OUTPUT)
    {
      arg->ret = TEE_ERROR_BAD_PARAMETERS;
      return;
    }

  if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
    {
      /* Should not happen unless the RTC driver is missing */

      arg->ret = TEE_ERROR_GENERIC;
      return;
    }

  arg->params[0].u.value.a = (uint32_t)ts.tv_sec;   /* Seconds since epoch  */
  arg->params[0].u.value.b = (uint32_t)ts.tv_nsec;  /* Nanoseconds          */

  arg->ret = TEE_SUCCESS;
  return;
}

/****************************************************************************
 * Name: optee_rpc_cmd_suspend
 *
 * Description:
 *   Request from OP-TEE to suspend the current nuttx process.
 *
 * Parameters:
 *   arg - [In/Out] Pointer to the RPC message argument, located in a
 *         shared page, and filled by the secure world, containing the time
 *         in msec to sleep.
 *
 * Returned Value:
 *   None.  Result codes are written into arg->ret.
 *
 ****************************************************************************/

static void optee_rpc_cmd_suspend(struct optee_msg_arg *arg)
{
  if (arg->num_params != 1 ||
      (arg->params[0].attr & OPTEE_MSG_ATTR_TYPE_MASK) !=
          OPTEE_MSG_ATTR_TYPE_VALUE_INPUT)
    {
      arg->ret = TEE_ERROR_BAD_PARAMETERS;
      return;
    }

  uint32_t msec_to_wait = arg->params[0].u.value.a;

  if (msec_to_wait)
    {
      int ret = nxsig_usleep((useconds_t)msec_to_wait * 1000);

      if (ret < 0 && get_errno() != EINTR)
        {
            arg->ret = TEE_ERROR_GENERIC;
            return;
        }
    }

  arg->ret = TEE_SUCCESS;
}

/****************************************************************************
 * Name: optee_rpc_cmd_supplicant
 *
 * Description:
 *   Request from OP-TEE to suspend the current nuttx process.
 *
 * Input Parameters:
 *   priv - Pointer to the driver's optee_priv_data struct.
 *   arg  - Pointer to the RPC message argument, located in a shared page,
 *          filled by the secure world. A copy of this message will be sent
 *          to the supplicant process that runs in userspace for further
 *          processing.
 *
 * Returned Value:
 *   None.  Result codes are written into arg->ret.
 *
 ****************************************************************************/

static void optee_rpc_cmd_supplicant(FAR struct optee_priv_data *priv, struct optee_msg_arg *arg)
{
  struct tee_ioctl_param *params;

  arg->ret_origin = TEE_ORIGIN_COMMS;
  _alert("[%s], line %u\n", __func__, __LINE__);

  params = kmm_zalloc(TEE_IOCTL_PARAM_SIZE(arg->num_params));
  if (!params)
    {
      arg->ret = TEE_ERROR_OUT_OF_MEMORY;
      return;
    }

  _alert("[%s], line %u\n", __func__, __LINE__);
  if (optee_from_msg_param(params, arg->num_params, arg->params))
    {
      arg->ret = TEE_ERROR_BAD_PARAMETERS;
      goto out;
    }

  _alert("[%s], line %u\n", __func__, __LINE__);

  arg->ret = optee_supplicant_request(arg->cmd, arg->num_params, params);

  if (optee_to_msg_param(priv, arg->params, arg->num_params, params))
  {
      arg->ret = TEE_ERROR_BAD_PARAMETERS;
  }

out:
  kmm_free(params);
}

/****************************************************************************
 * Name: optee_rpc_cmd_shm_alloc
 *
 * Description:
 *   Request from OP-TEE to suspend the current nuttx process.
 *
 * Input Parameters:
 *   arg  - Pointer to the RPC message argument, allocated in the shared page
 *          by the secure world. A copy of this message might be sent to the
 *          supplicant process that runs in userspace for further processing.
 *
 * Returned Value:
 *   None.  Result codes are written into arg->ret.
 *   Information about the shared memory is passed through arg->params
 *
 ****************************************************************************/

static void optee_rpc_cmd_shm_alloc(FAR struct optee_priv_data *priv,
                                    struct optee_msg_arg *arg,
                                    void **last_page_list)
{
  struct optee_shm *shm;
  size_t n;
  size_t sz;
  int32_t ret = OK;

  arg->ret_origin = TEE_ORIGIN_COMMS;

  if (arg->num_params != 1 ||
      arg->params[0].attr != OPTEE_MSG_ATTR_TYPE_VALUE_INPUT)
    {
      arg->ret = TEE_ERROR_BAD_PARAMETERS;
      return;
    }

  for (n = 1; n < arg->num_params; n++)
    {
      if (arg->params[n].attr != OPTEE_MSG_ATTR_TYPE_NONE)
        {
          arg->ret = TEE_ERROR_BAD_PARAMETERS;
          return;
        }
    }

  usleep(1000);
  usleep(1000);
  sz = arg->params[0].u.value.b;
  _alert("[%s], sz is %lu, switch is %lu", __func__, sz,
    arg->params[0].u.value.a);
  switch (arg->params[0].u.value.a)
    {
      case OPTEE_MSG_RPC_SHM_TYPE_APPL:
        ret = optee_supplicant_cmd_alloc(priv, sz, &shm);
        break;
      case OPTEE_MSG_RPC_SHM_TYPE_KERNEL:
        ret = optee_shm_alloc(priv, NULL , sz, TEE_SHM_ALLOC, &shm);
        break;
      default:
        arg->ret = TEE_ERROR_BAD_PARAMETERS;
        return;
    }

  if (ret == -ENOMEM)
    {
      arg->ret = TEE_ERROR_OUT_OF_MEMORY;
      return;
    }
  else if (ret == -ENODEV)
    {
      arg->ret = TEE_ERROR_COMMUNICATION;
      return;
    }
  else if (ret != OK)
    {
      arg->ret = TEE_ERROR_GENERIC;
      return;
    }

  if (shm->flags | TEE_SHM_REGISTER)
    {
      arg->params[0].attr = OPTEE_MSG_ATTR_TYPE_TMEM_OUTPUT |
                            OPTEE_MSG_ATTR_NONCONTIG;
      arg->params[0].u.tmem.buf_ptr = shm->paddr;
      arg->params[0].u.tmem.size = shm->length;
      arg->params[0].u.tmem.shm_ref = (unsigned long)shm;
      *last_page_list = shm->page_list;
      usleep(1000);
      _alert("[%s], line :: %u, ID is %u, SIZE %lu, shm_ref %lx",
        __func__, __LINE__, shm->id, shm->length, (uintptr_t)shm);
      usleep(1000);
    }
  else
    {
      arg->params[0].attr = OPTEE_MSG_ATTR_TYPE_TMEM_OUTPUT;
      arg->params[0].u.tmem.buf_ptr = shm->paddr;
      arg->params[0].u.tmem.size = sz;
      arg->params[0].u.tmem.shm_ref = (unsigned long)shm;
    }

  arg->ret = TEE_SUCCESS;
}

/****************************************************************************
 * Name: optee_rpc_cmd_free_supplicant
 *
 * Description:
 *   Request from OP-TEE to suspend the current nuttx process.
 *
 * Input Parameters:
 *   shm_id - The id of the shared memory to be freed.
 *
 * Returned Value:
 *   TEE_SUCCESS on success or a global platform api error code on failure.
 *
 ****************************************************************************/

static uint32_t optee_rpc_cmd_free_supplicant(int32_t shm_id)
{
  struct tee_ioctl_param param;

  param.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
  param.a = OPTEE_MSG_RPC_SHM_TYPE_APPL;
  param.b = shm_id;
  param.c = 0;
  _alert("[cmd_free_suppl]!!! The id is %u", shm_id);

  return optee_supplicant_request(OPTEE_MSG_RPC_CMD_SHM_FREE, 1, &param);
}

/****************************************************************************
 * Name: optee_rpc_func_cmd_shm_free
 *
 * Description:
 *   Request from OP-TEE to suspend the current nuttx process.
 *
 * Input Parameters:
 *   priv - Pointer to the driver's optee_priv_data struct.
 *   arg  - Pointer to the RPC message argument, allocated in the shared page
 *          by the secure world. A copy of this message might be sent to the
 *          supplicant process that runs in userspace for further processing.
 *
 * Returned Value:
 *   None.  Result codes are written into arg->ret.
 *
 ****************************************************************************/

static void optee_rpc_func_cmd_shm_free(FAR struct optee_priv_data *priv,
           struct optee_msg_arg *arg)
{
  struct optee_shm *shm;

  arg->ret_origin = TEE_ORIGIN_COMMS;

  if (arg->num_params != 1 ||
      arg->params[0].attr != OPTEE_MSG_ATTR_TYPE_VALUE_INPUT)
    {
      arg->ret = TEE_ERROR_BAD_PARAMETERS;
      return;
    }

  shm = (struct optee_shm *)(unsigned long)arg->params[0].u.value.b;
  usleep(1000);
  _alert("[%s], shm is %lx, switch is %lu", __func__,
    (uintptr_t)shm, arg->params[0].u.value.a);
  usleep(1000);
  switch (arg->params[0].u.value.a)
    {
      case OPTEE_MSG_RPC_SHM_TYPE_APPL:
        arg->ret = optee_rpc_cmd_free_supplicant(shm->id);
        idr_remove(optee_supplicant_get_shm_idr(), shm->id);
        break;
      case OPTEE_MSG_RPC_SHM_TYPE_KERNEL:
        idr_remove(priv->shms, shm->id);
        kmm_free((void *)shm->vaddr);
        kmm_free(shm);
        arg->ret = TEE_SUCCESS;
        break;
      default:
        arg->ret = TEE_ERROR_BAD_PARAMETERS;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: optee_rpc_handle_cmd
 *
 * Description:
 *   Request from OP-TEE to suspend the current nuttx process.
 *
 * Input Parameters:
 *   priv - Pointer to the driver's optee_priv_data struct.
 *   shm  - Contains a pointer to the RPC message argument, located in the
 *          shared page, filled by the secure world. A copy of this message
 *          might be sent to the supplicant process that runs in userspace
 *          for further processing.
 * Output Parameters:
 *   last_page_list - Passes by reference a pointer to the virtual address
 *                    of a page list. The page list can be freed by a
 *                    caller or by this function, depending on the response
 *                    of the OP-TEE to the next SMC.
 *
 * Returned Value:
 *   None. The response to OP-TEE will passed through the shared memory.
 *
 ****************************************************************************/

void optee_rpc_handle_cmd(FAR struct optee_priv_data *priv,
                          struct optee_shm *shm, void **last_page_list)
{
  struct optee_msg_arg *arg;

  if (0 == shm)
    {
      _err("[%s] shm error.\n", __func__);
      return;
    }

  arg = (struct optee_msg_arg *)shm->vaddr;

  usleep(1000);
  _alert("RPC invoked with %u", arg->cmd);
  usleep(1000);
  switch (arg->cmd)
  {
    case OPTEE_MSG_RPC_CMD_GET_TIME:
      optee_rpc_handle_cmd_get_time(arg);
      break;
    case OPTEE_MSG_RPC_CMD_SUSPEND:
      optee_rpc_cmd_suspend(arg);
      break;
    case OPTEE_MSG_RPC_CMD_SHM_ALLOC:
      kmm_free(*last_page_list);
      *last_page_list = 0;
      optee_rpc_cmd_shm_alloc(priv, arg, last_page_list);
      break;
    case OPTEE_MSG_RPC_CMD_SHM_FREE:
      optee_rpc_func_cmd_shm_free(priv, arg);
      break;
    default:
      optee_rpc_cmd_supplicant(priv, arg);
  }
}
