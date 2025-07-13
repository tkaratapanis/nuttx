/****************************************************************************
 * drivers/misc/optee_supplicant.c
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

#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/kmalloc.h>
#include <nuttx/queue.h>
#include <nuttx/idr.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include "optee.h"
#include "optee_supplicant.h"
#include <debug.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Request structure for RPCs serviced by the supplicant. */

struct optee_supplicant_req
{
  sq_entry_t              link;
  uint32_t                func;
  uint32_t                ret;
  size_t                  num_params;
  struct tee_ioctl_param *param;
  sem_t                   c;
};

struct optee_supplicant
{
  mutex_t           mutex;
  int               req_id;
  struct sq_queue_s reqs;
  FAR struct idr_s *idr;
  FAR struct idr_s *shm_idr;
  bool              running;
  sem_t             reqs_c;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct optee_supplicant supp_s;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: optee_supplicant_pop_entry
 *
 * Description:
 *   Pop the first request to the supplicant from from the request queue, and
 *   create for it a unique id.
 *
 * Parameters:
 *   num_params - Number of parameters passed.
 *   None
 *
 * Returned Value:
 *   True or False.
 *
 ****************************************************************************/

static FAR struct optee_supplicant_req * pop_entry(size_t num_params,
                                                   int *id)
{
  struct optee_supplicant_req *req;

  if (supp_s.req_id != -1)
    {
      /* Mixing sync/async not supported */

      return NULL;
    }

  if (sq_empty(&supp_s.reqs))
    {
      return NULL;
    }

  req = (struct optee_supplicant_req *)sq_remfirst(&supp_s.reqs);

  /* The request can't fit in the supplicant's supplied parameter buffer. */

  if (num_params < req->num_params)
    {
      kmm_free(req);
      return NULL;
    }

  *id = idr_alloc(supp_s.idr, req, 0, INT32_MAX);
  if (*id < 0)
    {
      kmm_free(req);
      return NULL;
    }

  return req;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: optee_supplicant_running
 *
 * Description:
 *   Returns true of the userspace supplicant is running.
 *
 * Parameters:
 *   None
 *
 * Returned Value:
 *   True or False.
 *
 ****************************************************************************/

bool optee_supplicant_running(void)
{
  return supp_s.running;
}

/****************************************************************************
 * Name: optee_supplicant_init
 *
 * Description:
 *   Initialize static supplicant data.
 *
 * Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void optee_supplicant_init(void)
{
  memset(&supp_s, 0, sizeof(supp_s));
  nxmutex_init(&supp_s.mutex);
  nxsem_init(&supp_s.reqs_c, 0, 0);
  sq_init(&supp_s.reqs);
  supp_s.idr = idr_init();
  supp_s.shm_idr = idr_init();
  supp_s.req_id = -1;
  supp_s.running = true;
}

/****************************************************************************
 * Name: optee_supplicant_uninit
 *
 * Description:
 *   Uninitialize static supplicant data.
 *
 * Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void optee_supplicant_uninit(void)
{
  _alert("[%s], lling shms\n", __func__);
  supp_s.running = false;
  nxmutex_destroy(&supp_s.mutex);
  nxsem_destroy(&supp_s.reqs_c);
  idr_destroy(supp_s.idr);
  _alert("[%s], lling shms\n", __func__);
}

/****************************************************************************
 * Name: optee_supplicant_request
 *
 * Description:
 *   Uninitialize static supplicant data.
 *
 * Parameters:
 *   func - Requested function for the supplicant to perform
 *   params - Pointer pointer to parameter data.
 *   num_params - Number of parameters passed.
 *
 * Returned Value:
 *   TEE_SUCCESS on success or a global platform api error code on failure.
 *
 ****************************************************************************/

uint32_t optee_supplicant_request(uint32_t func, size_t num_params,
                                  FAR struct tee_ioctl_param *param)
{
  struct optee_supplicant_req *req;
  uint32_t ret;

  if (!optee_supplicant_running())
    {
      return TEE_ERROR_COMMUNICATION;
    }

  req = (struct optee_supplicant_req *)kmm_zalloc(sizeof(*req));
  if (!req)
    {
      return TEE_ERROR_OUT_OF_MEMORY;
    }

  _alert("[%s],  line %u", __func__, __LINE__);
  sem_init(&req->c, 0, 0);
  req->func = func;
  req->num_params = num_params;
  req->param = param;
  _alert("[%s],  line %u", __func__, __LINE__);

  nxmutex_lock(&supp_s.mutex);
  _alert("[%s],  line %u", __func__, __LINE__);
  sq_addlast(&req->link, &supp_s.reqs);
  nxmutex_unlock(&supp_s.mutex);

  _alert("[%s],  line %u", __func__, __LINE__);

  /* Wake supplicant receiver */

  sem_post(&supp_s.reqs_c);
  _alert("[%s],  line %u", __func__, __LINE__);

  /* Wait for completion if supplicant is running. */

  while (sem_wait(&req->c) < 0)
    {
    }

  _alert("[%s],  line %u", __func__, __LINE__);
  ret = req->ret;
  sem_destroy(&req->c);
  kmm_free(req);

  _alert("[%s],  line %u", __func__, __LINE__);
  return ret;
}

/****************************************************************************
 * Name: optee_supplicant_recv
 *
 * Description:
 *   This function is invoked by an ioctl, used only by the supplicant. It
 *   will obtain the function to be performed and the parameters passed by
 *   OP-TEE.
 *
 * Parameters:
 *   func - Pointer to obtain the function to be performed by the supplicant.
 *   params - Pointer to the parameter data passed to supplicant.
 *   num_params - Pointer with the number of parameters the supplicant can
 *                process, and later updated with the number of parameters
 *                of the OP-TEE's RPC request.
 *
 * Returned Value:
 *   0 on success, a negated errno on failure.
 *
 ****************************************************************************/

int optee_supplicant_recv(FAR uint32_t *func, FAR uint32_t *num_params,
                          FAR struct tee_ioctl_param *params)
{
  struct optee_supplicant_req *req = NULL;
  int id;
  size_t num_meta = (params->attr == TEE_IOCTL_PARAM_ATTR_META);

  if (0 == num_params)
    {
      return -EINVAL;
    }

  for (int n = 0; n < *num_params; n++)
    {
      if (params[n].attr &&
          params[n].attr != TEE_IOCTL_PARAM_ATTR_META)
        {
          return -EINVAL;
        }
    }

  for (; ; )
    {
      nxmutex_lock(&supp_s.mutex);
      req = pop_entry(*num_params - num_meta, &id);
      nxmutex_unlock(&supp_s.mutex);

      if (req)
        {
          break;
        }

      if (sem_wait(&supp_s.reqs_c) < 0)
        {
          return -EINTR;
        }
    }

  if (num_meta)
    {
      params->attr |= TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
      params->a = id;
      params->b = 0;
      params->c = 0;
    }
  else
    {
      nxmutex_lock(&supp_s.mutex);
      supp_s.req_id = id;
      nxmutex_unlock(&supp_s.mutex);
    }

  /* Setup parameters */

  *func = req->func;
  *num_params = req->num_params + num_meta;

  memcpy(params + num_meta, req->param, req->num_params * sizeof(params[0]));

  return OK;
}

/****************************************************************************
 * Name: optee_supplicant_send
 *
 * Description:
 *   This function is invoked by an ioctl, used only by the supplicant. It
 *   will obtain the function to be performed and the parameters passed by
 *   OP-TEE.
 *
 * Parameters:
 *   ret - The return value to send to OP-TEE.
 *   params - Contains the parameters passed by the supplicant to OP-TEE.
 *   num_params - Number of parameters passed.
 *
 * Returned Value:
 *   0 on success, a negated errno on failure.
 *
 ****************************************************************************/

int optee_supplicant_send(uint32_t ret, uint32_t num_params,
                          FAR struct tee_ioctl_param *param)
{
  struct optee_supplicant_req *req;
  int id;
  size_t meta_params = 0;
  const uint32_t async_attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT |
                         TEE_IOCTL_PARAM_ATTR_META;

  nxmutex_lock(&supp_s.mutex);

  /* Check the parameters and obtain the request from the idr. */

  if (!num_params)
    {
      return -EINVAL;
    }

  /* Async. */

  if (supp_s.req_id == -1)
    {
      if (param->attr != async_attr)
        {
          return -EINVAL;
        }

      id = param->a;
      meta_params = 1;
    }
  else
    {
      /* Sync. */

      id = supp_s.req_id;
      meta_params = 0;
    }

  req = idr_find(supp_s.idr, id);
  if (!req)
    {
      return -ENOENT;
    }

  if ((num_params - meta_params) != req->num_params)
    {
      return -EINVAL;
    }

  idr_remove(supp_s.idr, id);
  supp_s.req_id = -1;

  nxmutex_unlock(&supp_s.mutex);

  if (!req)
    {
      return -EINVAL;
    }

  /* Update output and in/out parameters. */

  _alert("[%s],  Requested function was %u", __func__, req->func);
  for (size_t n = 0; n < req->num_params; n++)
    {
      struct tee_ioctl_param *p = &req->param[n];
      struct tee_ioctl_param *r = &param[n + meta_params];

      switch (p->attr & TEE_IOCTL_PARAM_ATTR_TYPE_MASK)
        {
          case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT:
          case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT:
            p->a = r->a;
            p->b = r->b;
            p->c = r->c;
            break;

          case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT:
          case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT:
            p->b = r->b;
            break;

          default:
            break;
        }

      usleep(1000);
      _alert("[THEO] Param attr is %lx\n", p->attr);
      _alert("[THEO] Param a is %lx\n", p->a);
      _alert("[THEO] Param b is %lx\n", p->b);
      _alert("[THEO] Param c is %lx\n", p->c);
      usleep(1000);
    }

  req->ret = ret;
  sem_post(&req->c);

  return OK;
}

/****************************************************************************
 * Name: optee_supplicant_cmd_alloc
 *
 * Description:
 *   Prepares and creates a request for userspace memory allocation that was
 *   requested by the OP-TEE through an RPC.
 *
 * Parameters:
 *   priv - Pointer to the driver's optee_priv_data struct
 *   shm - Passed by reference pointer to shared memory. On success it will
 *         be updated with the memory the supplicant allocated.
 *
 * Returned Value:
 *   0 on success, a negated errno on failure.
 *
 ****************************************************************************/

int32_t optee_supplicant_cmd_alloc(FAR struct optee_priv_data *priv,
                                   size_t sz, struct optee_shm **shm)
{
  uint32_t ret;
  struct tee_ioctl_param param;

  param.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
  param.a = OPTEE_MSG_RPC_SHM_TYPE_APPL;
  param.b = sz;
  param.c = 0;

  ret = optee_supplicant_request(OPTEE_MSG_RPC_CMD_SHM_ALLOC, 1, &param);
  if (ret)
    {
      return optee_convert_error(ret);
    }

  nxmutex_lock(&supp_s.mutex);
  *shm = idr_find(optee_supplicant_get_shm_idr(), param.c);
  nxmutex_unlock(&supp_s.mutex);

  if (NULL == *shm)
    {
      return -ENOENT;
    }

  return OK;
}

/****************************************************************************
 * Name: optee_supplicant_get_shm_idr
 *
 * Description:
 *   Return a pointer to the idr_s tree containing the shared memory ids
 *   allocated by the supplicant.
 *
 * Parameters:
 *   None.
 *
 * Returned Value:
 *   Pointer to the static shm_idr tree.
 *
 ****************************************************************************/

FAR struct idr_s *optee_supplicant_get_shm_idr(void)
{
  return supp_s.shm_idr;
}
