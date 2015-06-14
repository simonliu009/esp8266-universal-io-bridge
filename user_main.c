#include "user_main.h"

#include "uart.h"
#include "ap_auth.h"

#include <os_type.h>
#include <ets_sys.h>
#include <ip_addr.h>
#include <espconn.h>
#include <mem.h>
#include <user_interface.h>

#include "esp-missing-decls.h"

typedef enum
{
    ts_raw,
    ts_dodont,
    ts_data,
} telnet_strip_state_t;

flags_t flags = { 0 };

queue_t *uart_send_queue;
queue_t *uart_receive_queue;

os_event_t background_task_queue[background_task_queue_length];

static char *tcp_cmd_receive_buffer;
static char *tcp_cmd_send_buffer;
static char tcp_cmd_send_buffer_busy;
static struct espconn *esp_cmd_tcp_connection;

static char *tcp_data_send_buffer;
static char tcp_data_send_buffer_busy;
static struct espconn *esp_data_tcp_connection;

ICACHE_FLASH_ATTR static void user_init2(void);

ICACHE_FLASH_ATTR static void watchdog_crash(void)
{
	for(;;)
		(void)0;
}

ICACHE_FLASH_ATTR static void tcp_accept(struct espconn *esp_config, esp_tcp *esp_tcp_config,
		uint16_t port, void (*connect_callback)(struct espconn *))
{
	memset(esp_tcp_config, 0, sizeof(*esp_tcp_config));
	esp_tcp_config->local_port = port;
	memset(esp_config, 0, sizeof(*esp_config));
	esp_config->type = ESPCONN_TCP;
	esp_config->state = ESPCONN_NONE;
	esp_config->proto.tcp = esp_tcp_config;
	espconn_regist_connectcb(esp_config, (espconn_connect_callback)connect_callback);
	espconn_accept(esp_config);
	esp_cmd_tcp_connection = 0;

	espconn_tcp_set_max_con_allow(esp_config, 1);
}

static void background_task(os_event_t *events)
{
	uint16_t tcp_data_send_buffer_length;

	// send data in the uart receive fifo to tcp

	if(!queue_empty(uart_receive_queue) && !tcp_data_send_buffer_busy)
	{
		// data available and can be sent now

		tcp_data_send_buffer_length = 0;

		while((tcp_data_send_buffer_length < buffer_size) && !queue_empty(uart_receive_queue))
			tcp_data_send_buffer[tcp_data_send_buffer_length++] = queue_pop(uart_receive_queue);

		if(tcp_data_send_buffer_length > 0)
		{
			tcp_data_send_buffer_busy = 1;
			espconn_sent(esp_data_tcp_connection, tcp_data_send_buffer, tcp_data_send_buffer_length);
		}
	}

	// if there is still data in uart receive fifo that can't be
	// sent to tcp yet, tcp_sent_callback will call us when it can
}

static void tcp_data_sent_callback(void *arg)
{
    tcp_data_send_buffer_busy = 0;

	// retry to send data still in the fifo

	system_os_post(background_task_id, 0, 0);
}

static void tcp_data_receive_callback(void *arg, char *data, uint16_t length)
{
	uint16_t current;
	uint8_t byte;
	uint8_t telnet_strip_state;

	if(!esp_data_tcp_connection)
		return;

	telnet_strip_state = ts_raw;

	for(current = 0; (current < length) && !queue_full(uart_send_queue); current++)
	{
		byte = (uint8_t)data[current];

		switch(telnet_strip_state)
		{
			case(ts_raw):
			{
				if(flags.strip_telnet && (byte == 0xff))
					telnet_strip_state = ts_dodont;
				else
					queue_push(uart_send_queue, (char)byte);

				break;
			}

			case(ts_dodont):
			{
				telnet_strip_state = ts_data;
				break;
			}

			case(ts_data):
			{
				telnet_strip_state = ts_raw;
				break;
			}
		}
	}

	uart_start_transmit(!queue_empty(uart_send_queue));
}

static void tcp_data_disconnect_callback(void *arg)
{
	esp_data_tcp_connection = 0;
}

static void tcp_data_connect_callback(struct espconn *new_connection)
{
	if(esp_data_tcp_connection)
		espconn_disconnect(new_connection);
	else
	{
		esp_data_tcp_connection	= new_connection;
		tcp_data_send_buffer_busy = 0;

		espconn_regist_recvcb(esp_data_tcp_connection, tcp_data_receive_callback);
		espconn_regist_sentcb(esp_data_tcp_connection, tcp_data_sent_callback);
		espconn_regist_disconcb(esp_data_tcp_connection, tcp_data_disconnect_callback);

		espconn_set_opt(esp_data_tcp_connection, ESPCONN_REUSEADDR);

		queue_flush(uart_send_queue);
		queue_flush(uart_receive_queue);
	}
}

static void tcp_cmd_sent_callback(void *arg)
{
    tcp_cmd_send_buffer_busy = 0;
}

static void tcp_cmd_receive_callback(void *arg, char *data, uint16_t length)
{
	uint16_t current;
	uint16_t tcp_cmd_receive_buffer_length;
	uint16_t tcp_cmd_send_buffer_length;
	uint8_t byte;
	uint8_t telnet_strip_state;

	if(!esp_cmd_tcp_connection)
		return;

	tcp_cmd_receive_buffer_length = 0;

	telnet_strip_state = ts_raw;

	for(current = 0; (current < length) && (tcp_cmd_receive_buffer_length < buffer_size); current++)
	{
		byte = (uint8_t)data[current];

		switch(telnet_strip_state)
		{
			case(ts_raw):
			{
				if(flags.strip_telnet && (byte == 0xff))
					telnet_strip_state = ts_dodont;
				else
					if((byte >= ' ') && (byte <= '~'))
						tcp_cmd_receive_buffer[tcp_cmd_receive_buffer_length++] = (char)byte;

				break;
			}

			case(ts_dodont):
			{
				telnet_strip_state = ts_data;
				break;
			}

			case(ts_data):
			{
				telnet_strip_state = ts_raw;
				break;
			}
		}
	}

	memcpy(tcp_cmd_send_buffer, tcp_cmd_receive_buffer, tcp_cmd_receive_buffer_length);

	if((tcp_cmd_receive_buffer_length + 2) > buffer_size)
		tcp_cmd_send_buffer_length = tcp_cmd_receive_buffer_length - 2;
	else
		tcp_cmd_send_buffer_length = tcp_cmd_receive_buffer_length;

	tcp_cmd_send_buffer[tcp_cmd_send_buffer_length + 0] = '\r';
	tcp_cmd_send_buffer[tcp_cmd_send_buffer_length + 1] = '\n';

	tcp_cmd_send_buffer_length += 2;

	if(!tcp_cmd_send_buffer_busy)
	{
		tcp_cmd_send_buffer_busy = 1;
		espconn_sent(esp_cmd_tcp_connection, tcp_cmd_send_buffer, tcp_cmd_send_buffer_length);
	}
}

static void tcp_cmd_disconnect_callback(void *arg)
{
	esp_cmd_tcp_connection = 0;
}

static void tcp_cmd_connect_callback(struct espconn *new_connection)
{
	if(esp_cmd_tcp_connection)
		espconn_disconnect(new_connection);
	else
	{
		esp_cmd_tcp_connection = new_connection;
		tcp_cmd_send_buffer_busy = 0;

		espconn_regist_recvcb(esp_cmd_tcp_connection, tcp_cmd_receive_callback);
		espconn_regist_sentcb(esp_cmd_tcp_connection, tcp_cmd_sent_callback);
		espconn_regist_disconcb(esp_cmd_tcp_connection, tcp_cmd_disconnect_callback);

		espconn_set_opt(esp_cmd_tcp_connection, ESPCONN_REUSEADDR);
	}
}

ICACHE_FLASH_ATTR void user_init(void)
{
	flags.strip_telnet = 1; // FIXME

	if(!(uart_send_queue = queue_new(buffer_size)))
		watchdog_crash();

	if(!(uart_receive_queue = queue_new(buffer_size)))
		watchdog_crash();

	if(!(tcp_cmd_receive_buffer = os_malloc(buffer_size)))
		watchdog_crash();

	if(!(tcp_cmd_send_buffer = os_malloc(buffer_size)))
		watchdog_crash();

	if(!(tcp_data_send_buffer = os_malloc(buffer_size)))
		watchdog_crash();

	system_init_done_cb(user_init2);
}

ICACHE_FLASH_ATTR static void user_init2(void)
{
	// create ap_auth.h and #define ap_ssid / ap_password accordingly

	static struct station_config station_config = { ap_ssid, ap_password, 0, { 0, 0, 0, 0, 0, 0 } };
	static struct espconn esp_cmd_config, esp_data_config;
	static esp_tcp esp_cmd_tcp_config, esp_data_tcp_config;

	wifi_set_sleep_type(NONE_SLEEP_T);
	wifi_set_opmode_current(STATION_MODE);
	wifi_station_set_auto_connect(0);
	wifi_station_disconnect();
	wifi_station_set_config_current(&station_config);
	wifi_station_connect();

	tcp_accept(&esp_cmd_config, &esp_cmd_tcp_config, 24, tcp_cmd_connect_callback);
	espconn_regist_time(&esp_cmd_config, 0, 0);
	esp_cmd_tcp_connection = 0;

	tcp_accept(&esp_data_config, &esp_data_tcp_config, 25, tcp_data_connect_callback);
	espconn_regist_time(&esp_data_config, 30, 0);
	esp_data_tcp_connection = 0;

	uart_init();

	system_os_task(background_task, background_task_id, background_task_queue, background_task_queue_length);
	system_os_post(background_task_id, 0, 0);
}
