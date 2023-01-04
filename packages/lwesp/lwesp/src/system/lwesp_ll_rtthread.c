/**
 * \file            lwesp_ll_rtthread.c
 * \brief           Generic RT-Thread porting
 */

#include "lwesp/lwesp.h"
#include "lwesp/lwesp_input.h"
#include "lwesp/lwesp_mem.h"
#include "system/lwesp_ll.h"
#include "rtdevice.h"
#include "drv_gpio.h"

#if !__DOXYGEN__

#if !LWESP_CFG_INPUT_USE_PROCESS
    #error "LWESP_CFG_INPUT_USE_PROCESS must be enabled in `lwesp_config.h` to use this driver."
#endif /* LWESP_CFG_INPUT_USE_PROCESS */

#if !defined(LWESP_USART_DMA_RX_BUFF_SIZE)
    #define LWESP_USART_DMA_RX_BUFF_SIZE 0x1000
#endif /* !defined(LWESP_USART_DMA_RX_BUFF_SIZE) */

#if !defined(LWESP_MEM_SIZE)
    #define LWESP_MEM_SIZE 0x1000
#endif /* !defined(LWESP_MEM_SIZE) */

#if !defined(LWESP_USART_RDR_NAME)
    #define LWESP_USART_RDR_NAME RDR
#endif /* !defined(LWESP_USART_RDR_NAME) */

/* USART memory */
static uint8_t uart_mem[LWESP_USART_DMA_RX_BUFF_SIZE];
static uint8_t is_running, initialized;
static size_t old_pos;

/* Serial */
static rt_device_t serial = RT_NULL;
#define LWESP_DEVNAME "uart4"

/* UART thread */
static lwesp_sys_thread_t s_sys_thread_serial;

/* Message queue */
static struct rt_messagequeue s_sys_mbox_rx;
static char msg_pool[256];

struct rx_msg
{
    rt_device_t dev;
    rt_size_t size;
};

static rt_err_t serial_rx_done(rt_device_t dev, rt_size_t size)
{
    struct rx_msg msg;
    rt_err_t result;

    msg.dev = dev;
    msg.size = size;
    result = rt_mq_send(&s_sys_mbox_rx, &msg, sizeof(msg));
    if (result == -RT_EFULL)
    {
        rt_kprintf("message queue full!\n");
        return -RT_ERROR;
    }

    return RT_EOK;
}

/**
 * \brief           USART data processing
 */
static void
lwesp_serial_ll_worker(void *arg)
{
    struct rx_msg msg;
    rt_uint32_t rx_length;
    rt_err_t result;

    while (1)
    {
        rt_memset(&msg, 0, sizeof(msg));

        result = rt_mq_recv(&s_sys_mbox_rx, &msg, sizeof(msg), RT_WAITING_FOREVER);
        if (result == RT_EOK)
        {
            rx_length = rt_device_read(msg.dev, 0, uart_mem, msg.size);

            lwesp_input_process(&uart_mem[0], rx_length);
        }
    }
}

/**
 * \brief           Configure UART using DMA for receive in double buffer mode and IDLE line detection
 */
static uint8_t
lwesp_serial_init(uint32_t baudrate)
{
    uint8_t ret;
    static struct serial_configure sUartConfig  = RT_SERIAL_CONFIG_DEFAULT;;

    if ((serial = rt_device_find(LWESP_DEVNAME)) == RT_NULL)
        goto exit_lwesp_serial_init;

    /* Set tx complete function */
    rt_device_set_tx_complete(serial, RT_NULL);

    /* Set rx indicate function */
    rt_device_set_rx_indicate(serial, RT_NULL);

    sUartConfig.baud_rate = baudrate;
    if (rt_device_control(serial, RT_DEVICE_CTRL_CONFIG, &sUartConfig) != RT_EOK)
        goto exit_lwesp_serial_init;

    if (rt_device_open(serial, RT_DEVICE_FLAG_DMA_RX) != RT_EOK)
        goto exit_lwesp_serial_init;

    /* Set rx indicate function */
    rt_device_set_rx_indicate(serial, serial_rx_done);

    if (!lwesp_sys_mbox_create(&s_sys_mbox_rx, 10))
        goto exit_lwesp_serial_init;

    rt_mq_init(&s_sys_mbox_rx, "rx_mq",
               msg_pool,
               sizeof(struct rx_msg),
               sizeof(msg_pool),
               RT_IPC_FLAG_FIFO);

    if (!lwesp_sys_thread_create(&s_sys_thread_serial,
                                 "lwuart",
                                 lwesp_serial_ll_worker,
                                 RT_NULL,
                                 LWESP_SYS_THREAD_SS,
                                 LWESP_SYS_THREAD_PRIO))
        goto exit_lwesp_serial_init;

    return 1;

exit_lwesp_serial_init:

    if (serial != RT_NULL)
    {
        rt_device_close(RT_NULL);
        serial = RT_NULL;
    }


    return 0;
}

/**
 * \brief           Hardware reset callback
 */
static uint8_t
prv_reset_device(uint8_t state)
{
    /* ESP8266 reset pin PC.13 */
    rt_base_t esp_rst_pin = NU_GET_PININDEX(NU_PC, 13);

    rt_pin_write(esp_rst_pin, state);

    return 0;
}

/**
 * \brief           Send data to ESP device
 * \param[in]       data: Pointer to data to send
 * \param[in]       len: Number of bytes to send
 * \return          Number of bytes sent
 */
static size_t
prv_send_data(const void *data, size_t len)
{
    size_t ret = 0;

    if (len && rt_device_write(serial, 0, data, len) == len)
        ret = len;

    return ret;
}

/**
 * \brief           Callback function called from initialization process
 */
lwespr_t
lwesp_ll_init(lwesp_ll_t *ll)
{
    rt_err_t ret;
    rt_base_t esp_rst_pin, esp_fwupdate_pin;


    if (initialized)
        return 1;

    /* Initialize UART for communication */
    if (!lwesp_serial_init(ll->uart.baudrate))
        goto exit_lwesp_ll_init;

    esp_rst_pin = NU_GET_PININDEX(NU_PC, 13);
    esp_fwupdate_pin = NU_GET_PININDEX(NU_PD, 12);

    /* ESP8266 reset pin PC.13 */
    rt_pin_mode(esp_rst_pin, PIN_MODE_OUTPUT);
    rt_pin_write(esp_rst_pin, 1);

    /* ESP8266 fwupdate pin PD.12 */
    rt_pin_mode(esp_fwupdate_pin, PIN_MODE_OUTPUT);
    rt_pin_write(esp_fwupdate_pin, 1);

    if (!initialized)
    {
        ll->send_fn = prv_send_data; /* Set callback function to send data */
        ll->reset_fn = prv_reset_device; /* Set callback for hardware reset */
    }

    initialized = 1;

    return lwespOK;

exit_lwesp_ll_init:

    return lwespERR;
}

/**
 * \brief           Callback function to de-init low-level communication part
 */
lwespr_t
lwesp_ll_deinit(lwesp_ll_t *ll)
{

    if (serial != RT_NULL)
    {
        rt_device_close(serial);
        serial = RT_NULL;
    }

    initialized = 0;
    LWESP_UNUSED(ll);
    return lwespOK;
}

#endif /* !__DOXYGEN__ */
