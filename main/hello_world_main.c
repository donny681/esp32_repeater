/* Hello World Example
,,,
 This example code is in the Public Domain (or CC0 licensed, at your option.)

 Unless required by applicable law or agreed to in writing, this
 software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 CONDITIONS OF ANY KIND, either express or implied.
 */
#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event_loop.h"
#include "esp_pm.h"
#include "nvs_flash.h"
#include "lwip/dns.h"
#include "lwip/ip4_napt.h"
#include "lwip/netif.h"
static char *TAG = "main";
static netif_input_fn orig_input_ap;
static netif_linkoutput_fn orig_output_ap;

/* Some stats */
uint64_t Bytes_in, Bytes_out;
uint32_t Packets_in, Packets_out;
static void  patch_netif_ap(netif_input_fn ifn, netif_linkoutput_fn ofn, bool nat)
{
struct netif *nif;
ip4_addr_t ap_ip;
IP4_ADDR(&ap_ip,192,168,4,1);
//	ap_ip = config.network_addr;
//	ip4_addr4(&ap_ip) = 1;

	for (nif = netif_list; nif != NULL && nif->ip_addr.u_addr.ip4.addr != ap_ip.addr; nif = nif->next);
	if (nif == NULL) return;

	nif->napt = nat?1:0;
	if (nif->input != ifn) {
	  orig_input_ap = nif->input;
	  nif->input = ifn;
	}
	if (nif->linkoutput != ofn) {
	  orig_output_ap = nif->linkoutput;
	  nif->linkoutput = ofn;
	}
}


static uint8_t columns = 0;
void  my_input_ap (struct pbuf *p, struct netif *inp){

    //os_printf("Got packet from STA\r\n");
    Bytes_in += p->tot_len;
    Packets_in++;

#ifdef STATUS_LED
    GPIO_OUTPUT_SET (LED_NO, 1);
#endif

#ifdef REMOTE_MONITORING
    if (monitoring_on) {
//       system_os_post(user_procTaskPrio, SIG_PACKET, 0 );
       if (put_packet_to_ringbuf(p) != 0) {
#ifdef DROP_PACKET_IF_NOT_RECORDED
               pbuf_free(p);
	       return;
/*#else
       	       os_printf("x");
	       if (++columns > 40) {
		  os_printf("\r\n");
		  columns = 0;
	       }
*/
#endif
       }
       if (!onitoring_send_ongoing)
	       tcp_monitor_sent_cb(cur_mon_conn);
    }
#endif

    orig_input_ap (p, inp);
}

void  my_output_ap (struct netif *outp, struct pbuf *p) {

    //os_printf("Send packet to STA\r\n");
    Bytes_out += p->tot_len;
    Packets_out++;

#ifdef STATUS_LED
    GPIO_OUTPUT_SET (LED_NO, 0);
#endif

#ifdef REMOTE_MONITORING
    if (monitoring_on) {
//     system_os_post(user_procTaskPrio, SIG_PACKET, 0 );
       if (put_packet_to_ringbuf(p) != 0) {
#ifdef DROP_PACKET_IF_NOT_RECORDED
               pbuf_free(p);
	       return;
/*#else
       	       os_printf("x");
	       if (++columns > 40) {
		  os_printf("\r\n");
		  columns = 0;
	       }
*/
#endif
       }
       if (!monitoring_send_ongoing)
	       tcp_monitor_sent_cb(cur_mon_conn);
    }
#endif

    orig_output_ap (outp, p);
}

static ip4_addr_t my_ip;
static esp_err_t event_handler(void *ctx, system_event_t *event) {
	ip_addr_t dns_ip;
	switch (event->event_id) {
	case SYSTEM_EVENT_STA_START:
		esp_wifi_connect();
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		esp_wifi_connect();
		break;
	case SYSTEM_EVENT_STA_CONNECTED:
		break;
	case SYSTEM_EVENT_STA_GOT_IP:
		ESP_LOGI(TAG, "got ip:%s\n",
				ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip))
		;

		dns_ip = dns_getserver(0);
		dhcps_dns_setserver(&dns_ip);
		printf("ip: %s ,mask: %s ,gw: %s ,dns: %s \n",
				ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip),
				ip4addr_ntoa(&event->event_info.got_ip.ip_info.netmask),
				ip4addr_ntoa(&event->event_info.got_ip.ip_info.gw),
				ip4addr_ntoa(&dns_ip));
		my_ip = event->event_info.got_ip.ip_info.ip;
		// Update any predefined portmaps to the new IP addr
		for (int i = 0; i < IP_PORTMAP_MAX; i++) {
			if (ip_portmap_table[i].valid) {
				ip_portmap_table[i].maddr = my_ip.addr;
			}
		}

		break;
	case SYSTEM_EVENT_AP_STACONNECTED:
		ESP_LOGI(TAG, "station:"MACSTR" join,AID=%d\n",
				MAC2STR(event->event_info.sta_connected.mac),
				event->event_info.sta_connected.aid)
		;
//		patch_netif_ap(my_input_ap, my_output_ap, true);
		break;
	case SYSTEM_EVENT_AP_STADISCONNECTED:
		ESP_LOGI(TAG, "station:"MACSTR"leave,AID=%d\n",
				MAC2STR(event->event_info.sta_disconnected.mac),
				event->event_info.sta_disconnected.aid)
		;
		break;
	default:
		break;
	}
	return ESP_OK;
}

/*init wifi as sta and set power save mode*/
static void wifi_init(void) {
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT()
	;
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	wifi_config_t sta_wifi_config = { .sta = { .ssid = "test", .password =
			"wifiwifi", }, };
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_wifi_config));
	wifi_config_t ap_wifi_config = { .ap = { .ssid = "esp32_wifi",
			.ssid_len = 0, .max_connection = 5, .password = "wifiwifi",
			.authmode = WIFI_AUTH_WPA_WPA2_PSK, }, };
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "esp_wifi_set_ps().");
	esp_wifi_set_ps(WIFI_PS_NONE);
}

void app_main() {
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
	printf("Hello world!\n");
	wifi_init();
	while (1) {
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}

}
