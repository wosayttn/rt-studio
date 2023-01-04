/**
 * \file            lwesp_sys_freertos.c
 * \brief           System dependant functions for FreeRTOS
 */

/*
 * Copyright (c) 2022 Tilen MAJERLE
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
 * AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Author:          Tilen MAJERLE <tilen@majerle.eu>
 * Author:          Adrian Carpenter (FreeRTOS port)
 * Version:         v1.1.2-dev
 */
#include "rtthread.h"
#include "system/lwesp_sys.h"

#if !__DOXYGEN__

/* Mutex ID for main protection */
static rt_mutex_t sys_mutex;

typedef struct
{
    void *d;
} rtthread_mbox_t;

uint8_t
lwesp_sys_init(void)
{
    sys_mutex = rt_mutex_create("lwesp", RT_IPC_FLAG_PRIO);
    return sys_mutex == RT_NULL ? 0 : 1;
}

uint32_t
lwesp_sys_now(void)
{
    return rt_tick_get_millisecond();
}

uint8_t
lwesp_sys_protect(void)
{
    lwesp_sys_mutex_lock(&sys_mutex);
    return 1;
}

uint8_t
lwesp_sys_unprotect(void)
{
    lwesp_sys_mutex_unlock(&sys_mutex);
    return 1;
}

uint8_t
lwesp_sys_mutex_create(lwesp_sys_mutex_t *p)
{
    *p = rt_mutex_create("lwespsys", RT_IPC_FLAG_PRIO);
    return *p != RT_NULL;
}

uint8_t
lwesp_sys_mutex_delete(lwesp_sys_mutex_t *p)
{
    rt_mutex_delete(*p);
    return 1;
}

uint8_t
lwesp_sys_mutex_lock(lwesp_sys_mutex_t *p)
{
    return rt_mutex_take(*p, LWESP_SYS_TIMEOUT) == RT_EOK;
}

uint8_t
lwesp_sys_mutex_unlock(lwesp_sys_mutex_t *p)
{
    return rt_mutex_release(*p) == RT_EOK;
}

uint8_t
lwesp_sys_mutex_isvalid(lwesp_sys_mutex_t *p)
{
    return (p != RT_NULL) && (*p != RT_NULL);
}

uint8_t
lwesp_sys_mutex_invalid(lwesp_sys_mutex_t *p)
{
    *p = LWESP_SYS_MUTEX_NULL;
    return 1;
}

uint8_t
lwesp_sys_sem_create(lwesp_sys_sem_t *p, uint8_t cnt)
{

    *p = rt_sem_create("lwespsem", cnt, RT_IPC_FLAG_PRIO);

    return *p != RT_NULL;
}

uint8_t
lwesp_sys_sem_delete(lwesp_sys_sem_t *p)
{
    rt_sem_delete(*p);
    return 1;
}

uint32_t
lwesp_sys_sem_wait(lwesp_sys_sem_t *p, uint32_t timeout)
{
    uint32_t t = rt_tick_get();
    return rt_sem_take(*p, !timeout ? LWESP_SYS_TIMEOUT : RT_TICK_PER_SECOND * timeout / 1000) == RT_EOK
           ? ((rt_tick_get() - t) * RT_TICK_PER_SECOND)
           : LWESP_SYS_TIMEOUT;
}

uint8_t
lwesp_sys_sem_release(lwesp_sys_sem_t *p)
{
    return rt_sem_release(*p) == RT_EOK;
}

uint8_t
lwesp_sys_sem_isvalid(lwesp_sys_sem_t *p)
{
    return p != RT_NULL && *p != RT_NULL;
}

uint8_t
lwesp_sys_sem_invalid(lwesp_sys_sem_t *p)
{
    *p = LWESP_SYS_SEM_NULL;
    return 1;
}

uint8_t
lwesp_sys_mbox_create(lwesp_sys_mbox_t *b, size_t size)
{
    *b = rt_mq_create("lwespmq", size, sizeof(rtthread_mbox_t), RT_IPC_FLAG_PRIO);
    return *b != RT_NULL;
}

uint8_t
lwesp_sys_mbox_delete(lwesp_sys_mbox_t *b)
{
    rt_mq_delete(*b);
    return 1;
}

uint32_t
lwesp_sys_mbox_put(lwesp_sys_mbox_t *b, void *m)
{
    rtthread_mbox_t mb;
    uint32_t t;

		t = rt_tick_get();
    mb.d = m;

    if ( rt_mq_send(*b, &mb, sizeof(rtthread_mbox_t)) == RT_EOK )
		{
        return rt_tick_get() - t;
		}

		return LWESP_SYS_TIMEOUT;
}

uint32_t
lwesp_sys_mbox_get(lwesp_sys_mbox_t *b, void **m, uint32_t timeout)
{
    rtthread_mbox_t mb;
    uint32_t t;

    t	= rt_tick_get();

    if (rt_mq_recv(*b, &mb, sizeof(rtthread_mbox_t), !timeout ? LWESP_SYS_TIMEOUT : RT_TICK_PER_SECOND * timeout / 1000) == RT_EOK)
    {
        *m = mb.d;
        return (rt_tick_get() - t) * RT_TICK_PER_SECOND;
    }

    return LWESP_SYS_TIMEOUT;
}

uint8_t
lwesp_sys_mbox_putnow(lwesp_sys_mbox_t *b, void *m)
{
    rtthread_mbox_t mb;

    mb.d = m;
    return rt_mq_send(*b, &mb, sizeof(rtthread_mbox_t)) == RT_EOK;
}

uint8_t
lwesp_sys_mbox_getnow(lwesp_sys_mbox_t *b, void **m)
{
    rtthread_mbox_t mb;

    if (rt_mq_recv(*b, &mb, sizeof(rtthread_mbox_t), 0) == RT_EOK)
    {
        *m = mb.d;
        return 1;
    }
    return 0;
}

uint8_t
lwesp_sys_mbox_isvalid(lwesp_sys_mbox_t *b)
{
    return b != RT_NULL && *b != RT_NULL;
}

uint8_t
lwesp_sys_mbox_invalid(lwesp_sys_mbox_t *b)
{
    *b = LWESP_SYS_MBOX_NULL;
    return 1;
}

uint8_t
lwesp_sys_thread_create(lwesp_sys_thread_t *t, const char *name, lwesp_sys_thread_fn thread_func, void *const arg,
                        size_t stack_size, lwesp_sys_thread_prio_t prio)
{
    lwesp_sys_thread_t new_t;

    new_t = rt_thread_create(name,
                             thread_func,
                             arg,
                             (stack_size == 0) ? LWESP_SYS_THREAD_SS : stack_size,
                             (prio == 0) ? LWESP_SYS_THREAD_PRIO : prio,
                             10);

    if (t)
        *t = new_t;

    if (new_t)
        rt_thread_startup(new_t);

    return new_t != RT_NULL;
}

uint8_t
lwesp_sys_thread_terminate(lwesp_sys_thread_t *t)
{
    if (t && *t)
        rt_thread_delete(*t);
    return 1;
}

uint8_t
lwesp_sys_thread_yield(void)
{
    rt_thread_yield();
    return 1;
}

#endif /* !__DOXYGEN__ */
