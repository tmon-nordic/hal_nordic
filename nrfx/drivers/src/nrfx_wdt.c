/*
 * Copyright (c) 2015 - 2023, Nordic Semiconductor ASA
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <nrfx.h>

#if NRFX_CHECK(NRFX_WDT_ENABLED)

#include <nrfx_wdt.h>

#if !NRFX_FEATURE_PRESENT(NRFX_WDT, _ENABLED)
#error "No enabled WDT instances. Check <nrfx_config.h>."
#endif

#define NRFX_LOG_MODULE WDT
#include <nrfx_log.h>

// Control block - driver instance local data.
typedef struct
{
    nrfx_drv_state_t         state;
    uint8_t                  alloc_index;
#if !NRFX_CHECK(NRFX_WDT_CONFIG_NO_IRQ)
    nrfx_wdt_event_handler_t wdt_event_handler;
#endif
} wdt_control_block_t;

static wdt_control_block_t m_cb[NRFX_WDT_ENABLED_COUNT];

static void wdt_configure(nrfx_wdt_t const *        p_instance,
                          nrfx_wdt_config_t const * p_config)
{
    uint64_t ticks = (p_config->reload_value * 32768ULL) / 1000;
    NRFX_ASSERT(ticks <= UINT32_MAX);

    nrfy_wdt_config_t nrfy_conf = {
        .behaviour    = p_config->behaviour,
        .reload_value = (uint32_t)ticks,
    };

    nrfy_wdt_periph_configure(p_instance->p_reg, &nrfy_conf);

#if !NRFX_CHECK(NRFX_WDT_CONFIG_NO_IRQ)
    wdt_control_block_t * p_cb = &m_cb[p_instance->drv_inst_idx];
    if (p_cb->wdt_event_handler)
    {
        nrfy_wdt_int_init(p_instance->p_reg, NRF_WDT_INT_TIMEOUT_MASK,
                          p_config->interrupt_priority, true);
    }
#endif
}

nrfx_err_t nrfx_wdt_init(nrfx_wdt_t const *        p_instance,
                         nrfx_wdt_config_t const * p_config,
                         nrfx_wdt_event_handler_t  wdt_event_handler)
{
    NRFX_ASSERT(p_config);
    nrfx_err_t err_code;

    wdt_control_block_t * p_cb = &m_cb[p_instance->drv_inst_idx];

#if NRFX_CHECK(NRFX_WDT_CONFIG_NO_IRQ)
    (void)wdt_event_handler;
#else
    p_cb->wdt_event_handler = wdt_event_handler;
#endif

    if (p_cb->state == NRFX_DRV_STATE_UNINITIALIZED)
    {
        p_cb->state = NRFX_DRV_STATE_INITIALIZED;
    }
    else
    {
        err_code = NRFX_ERROR_INVALID_STATE;
        NRFX_LOG_WARNING("Function: %s, error code: %s.",
                         __func__,
                         NRFX_LOG_ERROR_STRING_GET(err_code));
        return err_code;
    }

    if (p_config)
    {
        wdt_configure(p_instance, p_config);
    }

    err_code = NRFX_SUCCESS;
    NRFX_LOG_INFO("Function: %s, error code: %s.", __func__, NRFX_LOG_ERROR_STRING_GET(err_code));
    return err_code;
}

nrfx_err_t nrfx_wdt_reconfigure(nrfx_wdt_t const *        p_instance,
                                nrfx_wdt_config_t const * p_config)
{
    NRFX_ASSERT(p_config);
    wdt_control_block_t * p_cb = &m_cb[p_instance->drv_inst_idx];

    if (p_cb->state == NRFX_DRV_STATE_UNINITIALIZED)
    {
        return NRFX_ERROR_INVALID_STATE;
    }
    if (p_cb->state == NRFX_DRV_STATE_POWERED_ON)
    {
        return NRFX_ERROR_BUSY;
    }
    wdt_configure(p_instance, p_config);
    return NRFX_SUCCESS;
}

void nrfx_wdt_enable(nrfx_wdt_t const * p_instance)
{
    wdt_control_block_t * p_cb = &m_cb[p_instance->drv_inst_idx];
    NRFX_ASSERT(p_cb->alloc_index != 0);
    NRFX_ASSERT(p_cb->state == NRFX_DRV_STATE_INITIALIZED);
    nrfy_wdt_task_trigger(p_instance->p_reg, NRF_WDT_TASK_START);
    p_cb->state = NRFX_DRV_STATE_POWERED_ON;
    NRFX_LOG_INFO("Enabled.");
}

void nrfx_wdt_feed(nrfx_wdt_t const * p_instance)
{
    wdt_control_block_t const * p_cb = &m_cb[p_instance->drv_inst_idx];
    NRFX_ASSERT(p_cb->state == NRFX_DRV_STATE_POWERED_ON);
    for (uint8_t i = 0; i < p_cb->alloc_index; i++)
    {
        nrfy_wdt_reload_request_set(p_instance->p_reg, (nrf_wdt_rr_register_t)(NRF_WDT_RR0 + i));
    }
}

nrfx_err_t nrfx_wdt_channel_alloc(nrfx_wdt_t const * p_instance, nrfx_wdt_channel_id * p_channel_id)
{
    nrfx_err_t result;
    wdt_control_block_t * p_cb = &m_cb[p_instance->drv_inst_idx];

    NRFX_ASSERT(p_channel_id);
    NRFX_ASSERT(p_cb->state == NRFX_DRV_STATE_INITIALIZED);

    NRFX_CRITICAL_SECTION_ENTER();
    if (p_cb->alloc_index < NRF_WDT_CHANNEL_NUMBER)
    {
        *p_channel_id = (nrfx_wdt_channel_id)(NRF_WDT_RR0 + p_cb->alloc_index);
        p_cb->alloc_index++;
        nrfy_wdt_reload_request_enable(p_instance->p_reg, *p_channel_id);
        result = NRFX_SUCCESS;
    }
    else
    {
        result = NRFX_ERROR_NO_MEM;
    }
    NRFX_CRITICAL_SECTION_EXIT();
    NRFX_LOG_INFO("Function: %s, error code: %s.", __func__, NRFX_LOG_ERROR_STRING_GET(result));
    return result;
}

void nrfx_wdt_channel_feed(nrfx_wdt_t const * p_instance, nrfx_wdt_channel_id channel_id)
{
    NRFX_ASSERT(m_cb[p_instance->drv_inst_idx].state == NRFX_DRV_STATE_POWERED_ON);
    nrfy_wdt_reload_request_set(p_instance->p_reg, channel_id);
}

#if !NRFX_CHECK(NRFX_WDT_CONFIG_NO_IRQ)
static void irq_handler(NRF_WDT_Type * p_reg, wdt_control_block_t * p_cb)
{
    /* Clearing timeout event also causes request status register to be cleared, so read it
     * before clearing. */
    uint32_t requests = nrf_wdt_request_status_get(p_reg);

    uint32_t evt_mask = nrfy_wdt_events_process(p_reg,
                                                NRFY_EVENT_TO_INT_BITMASK(NRF_WDT_EVENT_TIMEOUT));

    if (evt_mask & NRFY_EVENT_TO_INT_BITMASK(NRF_WDT_EVENT_TIMEOUT))
    {
        p_cb->wdt_event_handler(requests);
    }
}

NRFX_INSTANCE_IRQ_HANDLERS(WDT, wdt)

#endif // !NRFX_CHECK(NRFX_WDT_CONFIG_NO_IRQ)

#endif // NRFX_CHECK(NRFX_WDT_ENABLED)
