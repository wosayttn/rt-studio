#include "rtthread.h"
#include "rtdevice.h"
#include "lwesp.h"
#include "station_manager.h"
#include "netconn_server.h"
#include "lwesp/lwesp_mdns.h"

#define utils_printf   rt_kprintf
#define DEF_PRODUCT    "thermostat"

/**
 * \brief           Print IP string to the output
 * \param[in]       str_b: Text to print before IP address
 * \param[in]       ip: IP to print
 * \param[in]       str_a: Text to print after IP address
 */
void
utils_print_ip(const char *str_b, const lwesp_ip_t *ip, const char *str_a)
{
    if (str_b != NULL)
    {
        utils_printf("%s", str_b);
    }

    if (0)
    {
#if LWESP_CFG_IPV6
    }
    else if (ip->type == LWESP_IPTYPE_V6)
    {
        utils_printf("%04X:%04X:%04X:%04X:%04X:%04X:%04X:%04X\r\n",
                     (unsigned)ip->addr.ip6.addr[0], (unsigned)ip->addr.ip6.addr[1], (unsigned)ip->addr.ip6.addr[2],
                     (unsigned)ip->addr.ip6.addr[3], (unsigned)ip->addr.ip6.addr[4], (unsigned)ip->addr.ip6.addr[5],
                     (unsigned)ip->addr.ip6.addr[6], (unsigned)ip->addr.ip6.addr[7]);
#endif /* LWESP_CFG_IPV6 */
    }
    else
    {
        utils_printf("%d.%d.%d.%d\r\n",
                     (int)ip->addr.ip4.addr[0], (int)ip->addr.ip4.addr[1],
                     (int)ip->addr.ip4.addr[2], (int)ip->addr.ip4.addr[3]);
    }
    if (str_a != NULL)
    {
        utils_printf("%s", str_a);
    }
}

/**
 * \brief           Print MAC string to the output
 * \param[in]       str_b: Text to print before MAC address
 * \param[in]       mac: MAC to print
 * \param[in]       str_a: Text to print after MAC address
 */
void
utils_print_mac(const char *str_b, const lwesp_mac_t *mac, const char *str_a)
{
    if (str_b != NULL)
    {
        utils_printf("%s", str_b);
    }

    utils_printf("%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                 (unsigned)mac->mac[0], (unsigned)mac->mac[1], (unsigned)mac->mac[2],
                 (unsigned)mac->mac[3], (unsigned)mac->mac[4], (unsigned)mac->mac[5]);

    if (str_a != NULL)
    {
        utils_printf("%s", str_a);
    }
}


/**
 * \brief           Event callback function for ESP stack
 * \param[in]       evt: Event information with data
 * \return          espOK on success, member of \ref espr_t otherwise
 */
static lwespr_t lwesp_callback_func(lwesp_evt_t *evt)
{
    switch (lwesp_evt_get_type(evt))
    {
    case LWESP_EVT_AT_VERSION_NOT_SUPPORTED:
    {
        lwesp_sw_version_t v_min, v_curr;

        lwesp_get_min_at_fw_version(&v_min);
        lwesp_get_current_at_fw_version(&v_curr);

        utils_printf("Current ESP[8266/32[-C3]] AT version is not supported by library\r\n");
        utils_printf("Minimum required AT version is: %d.%d.%d\r\n", (int)v_min.major, (int)v_min.minor, (int)v_min.patch);
        utils_printf("Current AT version is: %d.%d.%d\r\n", (int)v_curr.major, (int)v_curr.minor, (int)v_curr.patch);
        break;
    }

    case LWESP_EVT_INIT_FINISH:
    {
        utils_printf("LWESP Library initialized!\r\n");
        break;
    }

    case LWESP_EVT_RESET_DETECTED:
    {
        utils_printf("Device reset detected!\r\n");
        break;
    }

    case LWESP_EVT_AP_CONNECTED_STA:
    {
        lwesp_mac_t *mac = lwesp_evt_ap_connected_sta_get_mac(evt);
        utils_print_mac("New station connected to access point with MAC address: ", mac, "\r\n");
        break;
    }

    case LWESP_EVT_AP_IP_STA:
    {
        lwesp_mac_t *mac = lwesp_evt_ap_ip_sta_get_mac(evt);
        lwesp_ip_t *ip = lwesp_evt_ap_ip_sta_get_ip(evt);

        utils_print_ip("IP ", ip, " assigned to station with MAC address: ");
        utils_print_mac(NULL, mac, "\r\n");
        break;
    }

    case LWESP_EVT_AP_DISCONNECTED_STA:
    {
        lwesp_mac_t *mac = lwesp_evt_ap_disconnected_sta_get_mac(evt);
        utils_print_mac("Station disconnected from access point with MAC address: ", mac, "\r\n");
        break;
    }

    default:
        break;
    }

    return lwespOK;
}

/* SNTP query callback function. */
static void lwesp_snftp_cbf(lwespr_t res, void *arg)
{
    switch (res)
    {
    case lwespOK:
    {
        time_t timestamp;
        lwesp_datetime_t *dt = (lwesp_datetime_t *)arg;
			
        RT_ASSERT(dt);

        set_date((rt_uint32_t)dt->year, (rt_uint32_t)dt->month, (rt_uint32_t)dt->date);
        set_time((rt_uint32_t)dt->hours, (rt_uint32_t)dt->minutes, (rt_uint32_t)dt->seconds);
        get_timestamp(&timestamp);

        if (timestamp > 1000)
        {
					   utils_printf("SYSTIME: %d, SNTP: %02d-%02d-%02d %02d:%02d:%02d\n",
                   (rt_uint32_t)timestamp,
                   (rt_uint32_t)dt->year, (rt_uint32_t)dt->month, (rt_uint32_t)dt->date,
                   (rt_uint32_t)dt->hours, (rt_uint32_t)dt->minutes, (rt_uint32_t)dt->seconds);
        }
    }
    break;

    default:
        break;
    }
}

static void lwesp_netif_worker(void *pvParameter)
{
    lwespr_t res;
    lwesp_mac_t ap_mac;
    char szTMP[64];
    lwesp_datetime_t dt;

    /* Initialize LWESP with default callback function */
    if (lwesp_init(lwesp_callback_func, 1) != lwespOK)
    {
        utils_printf("Cannot initialize LwESP!\r\n");
        goto exit_lwesp_netif_worker;
    }
    lwesp_evt_register(lwesp_callback_func);


#if 1
		#define DEF_SPEED_UP	(LWESP_CFG_AT_PORT_BAUDRATE * 4)
		/* Set baudrate to DEF_SPEED_UP and enable flow-control function. */
		if ( lwesp_set_at_baudrate(DEF_SPEED_UP, NULL, NULL, 1)  != lwespOK ) 
		{
			utils_printf("Cannot set baudrate to %d!\r\n", DEF_SPEED_UP);
			goto exit_lwesp_netif_worker;
		}
		else
		{
			  /* re-configuration UART baudrate. */
				extern uint8_t lwesp_serial_change_baudrate(uint32_t baudrate);
			  if (lwesp_serial_change_baudrate(DEF_SPEED_UP))
				{
					utils_printf("Reset baudrate to %d!\r\n", DEF_SPEED_UP);				
				}
				else
				{
					utils_printf("Cannot reset baudrate to %d!\r\n", DEF_SPEED_UP);
					goto exit_lwesp_netif_worker;
				}					
		}
#endif		
		
    if ((res = lwesp_ap_getmac(&ap_mac, RT_NULL, RT_NULL, 1)) == lwespOK)
    {
        utils_print_mac("SofAP MAC address: ", &ap_mac, "\r\n");
        rt_snprintf(szTMP, sizeof(szTMP), "nu%s-%02x%02x%02x", DEF_PRODUCT, ap_mac.mac[3], ap_mac.mac[4], ap_mac.mac[5]);
    }
    else
    {
        utils_printf("Failed to get ap mac address\r\n", (int)res);
        goto exit_lwesp_netif_worker;
    }

    if ((res = lwesp_set_wifi_mode(LWESP_MODE_STA_AP, NULL, NULL, 1)) == lwespOK)
    {
        utils_printf("ESP set to station mode\r\n");
    }
    else
    {
        utils_printf("Problems setting ESP to station mode: %d\r\n", (int)res);
        goto exit_lwesp_netif_worker;
    }

    /*
     * Connect to access point.
     *
     * Try unlimited time until access point accepts us.
     * Check for station_manager.c to define preferred access points ESP should connect to
     */
    res = station_manager_connect_to_preferred_access_point(1);
    /* Configure access point */
    //lwespr_t lwesp_ap_set_config(const char* ssid, const char* pwd, uint8_t ch, lwesp_ecn_t ecn, uint8_t max_sta, uint8_t hid,
    //                const lwesp_api_cmd_evt_fn evt_fn, void* const evt_arg, const uint32_t blocking);
    res = lwesp_ap_set_config(szTMP, "12345678", 10, LWESP_ECN_WPA2_PSK, 1, 0, RT_NULL, RT_NULL, 1);
    if (res == lwespOK)
    {
        utils_printf("Access point configured! %s \r\n", szTMP);
    }
    else
    {
        utils_printf("Cannot configure access point!\r\n");
        goto exit_lwesp_netif_worker;
    }

    /* Enable SNTP with default configuration for NTP servers */
    res = lwesp_sntp_set_config(1, 8, "tock.stdtime.gov.tw", "time.stdtime.gov.tw", NULL, NULL, NULL, 1);
    if (res == lwespOK)
    {
        utils_printf("sntp configured!\r\n");
    }
    else
    {
        utils_printf("Cannot configure sntp!\r\n");
    }

    /* mdns */
    res = lwesp_mdns_set_config(1, szTMP, "_"DEF_PRODUCT, 80, RT_NULL, RT_NULL, 1);
    if (res == lwespOK)
    {
        utils_printf("mdns configured! mdns: %s.local\r\n", szTMP);
        utils_printf("## You can execute 'ping %s.local' using window command-line.\r\n", szTMP);
    }
    else
    {
        utils_printf("Cannot configure mdns!\r\n");
    }

    /* The rest is handled in event function */
    /* Create server thread */
    lwesp_sys_thread_create(RT_NULL, "ncsvr", (lwesp_sys_thread_fn)netconn_server_thread, RT_NULL, LWESP_SYS_THREAD_SS, 0);

    /*
     * Do not stop program here.
     * New threads were created for ESP processing
     */
    lwesp_sntp_gettime(&dt, lwesp_snftp_cbf, &dt, 0);
    lwesp_delay(5000);
    while (1)
    {
        lwesp_sntp_gettime(&dt, lwesp_snftp_cbf, &dt, 0);
        lwesp_delay(60000);
    }

exit_lwesp_netif_worker:

    /* Terminate current thread */
    lwesp_sys_thread_terminate(RT_NULL);
}

int lwesp_worker_init(void)
{
    static lwesp_sys_thread_t lwesp_work_thread;

    lwesp_sys_thread_create(&lwesp_work_thread,
                            "lwapp",
                            lwesp_netif_worker,
                            RT_NULL,
                            LWESP_SYS_THREAD_SS,
                            LWESP_SYS_THREAD_PRIO);

#if defined(RT_USING_RTC) && defined(RT_USING_ALARM)
    rt_thread_t tid = rt_thread_find("alarmsvc");
    if (tid == RT_NULL)
        rt_alarm_system_init();
#endif

    return 0;
}
INIT_APP_EXPORT(lwesp_worker_init);
