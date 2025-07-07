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
#include "optee_private.h"
#include <debug.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Request structure for supplicant RPC */

struct optee_supp_req
{
  sq_entry_t        link;
  bool              in_queue;
  uint32_t          func;
  uint32_t          ret;
  size_t            num_params;
  struct tee_ioctl_param *param;
  sem_t             c;
};

struct optee_supp
{
  mutex_t mutex;
  int req_id;
  struct sq_queue_s reqs;
  FAR struct idr_s *idr;
  FAR struct idr_s *shm_idr;
  sem_t reqs_c;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct optee_supp supp;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static FAR struct optee_supp_req * supp_pop_entry(size_t num_params, int *id)
{
  struct optee_supp_req *req;

  if (supp.req_id != -1)
    {
      /* Mixing sync/async not supported */
      return NULL;
    }

  if (sq_empty(&supp.reqs))
    {
      return NULL;
    }

  req = (struct optee_supp_req *)sq_remfirst(&supp.reqs);
  if (num_params < req->num_params)
    {
      sq_addlast(req, &supp.reqs);
      // Might be better to just remove it if it can't be processed?
      //kmm_free(req);
      return NULL;
    }

  /* Allocate an ID for async response tracking */
  *id = idr_alloc(supp.idr, req, 0, INT32_MAX);
  if (*id < 0)
    {
      //kmm_free(req);
      return NULL;
    }

  req->in_queue = false;
  return req;
}


/****************************************************************************
 * Public Functions
 ****************************************************************************/

void optee_supp_init(void)
{
  memset(&supp, 0, sizeof(supp));
  nxmutex_init(&supp.mutex);
  nxsem_init(&supp.reqs_c, 0, 0);
  sq_init(&supp.reqs);
  supp.idr = idr_init();
  supp.shm_idr = idr_init();
  supp.req_id = -1;
}

void optee_supp_uninit(void)
{
  nxmutex_destroy(&supp.mutex);
  nxsem_destroy(&supp.reqs_c);
  idr_destroy(supp.idr);
  idr_destroy(supp.shm_idr);
}

uint32_t optee_supp_thrd_req(uint32_t func, size_t num_params,
                             FAR struct tee_ioctl_param *param)
{
  //struct optee *optee = tee_get_drvdata(ctx->teedev);
  //struct optee_supp *supp = &optee->supp;
  struct optee_supp_req *req;
  //int id;
  uint32_t ret;

  //if (!supp->ctx && ctx->supp_nowait)
  //  return TEEC_ERROR_COMMUNICATION;

  req = (struct optee_supp_req *)kmm_zalloc(sizeof(*req));
  if (!req)
    return TEE_ERROR_OUT_OF_MEMORY;

  _alert("[%s],  line %u", __func__, __LINE__);
  sem_init(&req->c, 0, 0);
  req->func = func;
  req->num_params = num_params;
  req->param = param;
  _alert("[%s],  line %u", __func__, __LINE__);

  nxmutex_lock(&supp.mutex);
  _alert("[%s],  line %u", __func__, __LINE__);
  sq_addlast(&req->link, &supp.reqs);
  req->in_queue = true;
  nxmutex_unlock(&supp.mutex);

  _alert("[%s],  line %u", __func__, __LINE__);
  /* Wake supplicant receiver */
  sem_post(&supp.reqs_c);
  _alert("[%s],  line %u", __func__, __LINE__);

  /* Wait for completion */
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

int optee_supp_recv(FAR uint32_t *func, FAR uint32_t *num_params,
                    FAR struct tee_ioctl_param *params)
{
  struct optee_supp_req *req = NULL;
  int id;
  size_t num_meta = (params->attr == TEE_IOCTL_PARAM_ATTR_META);

  if(0 == num_params)
    {
      return -EINVAL;
    }

  /* Linux here also checks the shm refcount, however in nuttx we don't use it. */

  for (int n = 0; n < *num_params; n++)
    {
      if (params[n].attr &&
          params[n].attr != TEE_IOCTL_PARAM_ATTR_META)
        {
          return -EINVAL;
        }
    }

  for(;;)
    {
      nxmutex_lock(&supp.mutex);
      req = supp_pop_entry(*num_params - num_meta, &id);
      nxmutex_unlock(&supp.mutex);

      if (req)
        {
          break;
        }

      if (sem_wait(&supp.reqs_c) < 0)
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
      nxmutex_lock(&supp.mutex);
      supp.req_id = id;
      nxmutex_unlock(&supp.mutex);
    }

  /* Setup parameters */
  *func = req->func;
  *num_params = req->num_params + num_meta;

  memcpy(params + num_meta, req->param, req->num_params * sizeof(params[0]));


  return OK;
}

int optee_supp_send(uint32_t ret, uint32_t num_params,
                    FAR struct tee_ioctl_param *param)
{
  //struct optee *optee = tee_get_drvdata(ctx->teedev);
  //struct optee_supp *supp = &optee->supp;
  struct optee_supp_req *req;
  int id;
  size_t meta_params = 0;
  const uint32_t async_attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT |
                         TEE_IOCTL_PARAM_ATTR_META;

  nxmutex_lock(&supp.mutex);

  /* Check the parameters and obtain the request from the idr. */

  {
    if (!num_params)
      {
        return -EINVAL;
      }

    /* Async. */

    if (supp.req_id == -1)
      {
        if (param->attr != async_attr)
          {
            _alert("[ERRRRRRRRRROR] Param attr is %x\n", param->attr);
            return -EINVAL;
          }
        id = param->a;
        meta_params = 1;
      }
    else
      {
        /* Sync. */

        id = supp.req_id;
        meta_params = 0;
      }

    req = idr_find(supp.idr, id);
    if (!req)
      {
        return -ENOENT;
      }

    if ((num_params - meta_params) != req->num_params)
      {
        //usleep(10000);
        _alert("[ERRRROR], num_params are %u, meta is %lu, req_params are %u\n", num_params, meta_params, req->num_params);
        //usleep(10000);
        return -EINVAL;
      }

    idr_remove(supp.idr, id);
    supp.req_id = -1;
  }

  nxmutex_unlock(&supp.mutex);

  if (!req)
    {
      return -EINVAL;
    }

  /* Update parameters. */

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
          //p->u.memref.size = r->u.memref.size;
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

int32_t optee_supplicant_cmd_alloc(FAR struct optee_priv_data *priv,
  size_t sz, struct optee_shm **shm)
{
	uint32_t ret;
	struct tee_ioctl_param param;

	param.attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
	param.a = OPTEE_MSG_RPC_SHM_TYPE_APPL;
	param.b = sz;
	param.c = 0;

	ret = optee_supp_thrd_req(OPTEE_MSG_RPC_CMD_SHM_ALLOC, 1, &param);
	if (ret)
    {
      return -ENOMEM;
    }

	nxmutex_lock(&supp.mutex);
	/* Increases count as secure world doesn't have a reference */
  *shm = idr_find(optee_supplicant_get_shm_idr(), param.c);
	nxmutex_unlock(&supp.mutex);
	return OK;
}



FAR struct idr_s *optee_supplicant_get_shm_idr(void)
{
  return supp.shm_idr;
}

//static int supp_check_recv_params(size_t num_params, struct tee_param *params,
//          size_t *num_meta)
//{
//  size_t n;
//
//  if (!num_params)
//    return -EINVAL;
//
//  /*
//   * If there's memrefs we need to decrease those as they where
//   * increased earlier and we'll even refuse to accept any below.
//   */
//  for (n = 0; n < num_params; n++)
//    if (tee_param_is_memref(params + n) && params[n].u.memref.shm)
//      tee_shm_put(params[n].u.memref.shm);
//
//  /*
//   * We only expect parameters as TEE_IOCTL_PARAM_ATTR_TYPE_NONE with
//   * or without the TEE_IOCTL_PARAM_ATTR_META bit set.
//   */
//  for (n = 0; n < num_params; n++)
//    if (params[n].attr &&
//        params[n].attr != TEE_IOCTL_PARAM_ATTR_META)
//      return -EINVAL;
//
//  /* At most we'll need one meta parameter so no need to check for more */
//  if (params->attr == TEE_IOCTL_PARAM_ATTR_META)
//    *num_meta = 1;
//  else
//    *num_meta = 0;
//
//  return 0;
//}

//void optee_supp_release(void)
//{
//  struct optee_supp_req *req;
//  FAR void *ptr;
//  int id;
//
//  nxmutex_lock(&supp->mutex);
//
//  /* Abort all requests registered in IDR */
//  /* TODO: NuttX idr does not provide direct iteration; using placeholder max_entries */
//  for (id = 0; id < CONFIG_IDR_MAX_ENTRIES; id++)
//    {
//      ptr = idr_find(&supp->idr, id);
//      if (ptr)
//        {
//          req = (struct optee_supp_req *)ptr;
//          idr_remove(&supp->idr, id);
//          req->ret = TEEC_ERROR_COMMUNICATION;
//          sem_post(&req->c);
//        }
//    }
//
//  /* Abort queued requests */
//  while ((req = (struct optee_supp_req *)sq_remfirst(&supp->reqs)) != NULL)
//    {
//      req->in_queue = false;
//      req->ret = TEEC_ERROR_COMMUNICATION;
//      sem_post(&req->c);
//    }
//
//  supp->ctx = NULL;
//  supp->req_id = -1;
//
//  nxmutex_unlock(&supp->mutex);
//}
