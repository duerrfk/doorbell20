/**
 * This file is part of DoorBell20.
 *
 * Copyright 2016 Frank Duerr
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <nrf.h>
#include <nrf_gpio.h>
#include <ble.h>
#include <softdevice_handler.h>
#include <ble_advdata.h>
#include <app_timer.h>
#include <app_button.h>
#include <ble_conn_params.h>
#include <ble_hci.h>
#include <app_util_platform.h>

#ifdef TARGET_BOARD_NRF51DK
// Pinout of development board (DK):
// * Pin 17: Button 1
// * Pin 18: Button 2 
// * Pin 21: LED 1
// * Pin 22: LED 2
#define PIN_BELL 17
#define PIN_LED 21
#else
// Pinout of DoorBell20 board:
#define PIN_BELL 3
// Actually, the DoorBell20 board has no LED.
// Pin 21 is not connected on this board, so it will also do no harm.
#define PIN_LED 21
#endif

// Max. length of door bell alarm characteristic [bytes].
#define MAX_LENGTH_DOOR_BELL_ALARM_CHAR 4

// Max. length of local time characteristic [bytes].
#define MAX_LENGTH_LOCALTIME_CHAR 4

#define DEVICE_NAME "DoorBell20"
// Minimum connection interval in 1.25 ms. Minimum allowed value: 7.5 ms.
// 80 -> 100 ms.
#define MIN_CONN_INTERVAL 80
// Maximum connection interval in 1.25 ms. Maximum allowed value: 4000 ms.
// We do not want to delay notifications too long. If the bell rings, the 
// client should get notified fast since there is someone waiting at the door
// and the client-side processing like sending a message to a mobile phone
// might take additional time.
// 160 -> 200 ms.
#define MAX_CONN_INTERVAL 160
// Number of connection intervals the device can stay silent.
// If nothing is happening, i.e., no door bell is ringing, we want to sleep
// long to save energy. With a slave latency of 5, the client gets a
// response to requests latest within 500-1000 ms assuming connection intervals 
// between 100 and 200 ms, which should be sufficiently responsive. 
#define SLAVE_LATENCY 5
// Connection supervision timeout, i.e., time until a link is considered
// lost, in 10 ms.
// 400 -> 4 s. 
#define CONN_SUP_TIMEOUT 400
// Advertisement interval in 0.625 ms; min. 20 ms, max 10.24 s.
// 1600 -> 1000 ms.
#define ADV_INTERVAL 1600
// How long to advertise in seconds (0 = forever)
#define ADV_TIMEOUT 0

// Time after making a connection when to start negotiation of connection 
// timing parameters [ms].
// -> 5 s
#define FIRST_CONN_PARAMS_UPDATE_DELAY 5000
// Time when to re-negotiate connection timing parameters [ms].
#define NEXT_CONN_PARAMS_UPDATE_DELAY 30000
// Maximum number of attempts while negotiating connecting timing parameters. 
#define MAX_CONN_PARAMS_UPDATE_COUNT 3

// Prescaler of RTC1 (low-frequency clock at 32.768 kHz), which is used by the 
// app timer (RTC0 is used by the softdevice, and, therefore, cannot be used by 
// the application). 
#define APP_TIMER_PRESCALER 0
#define APP_TIMER_QUEUE_SIZE 4

// Delay for debouncing door bell signals [ms].
#define DEBOUNCING_DELAY APP_TIMER_TICKS(50, APP_TIMER_PRESCALER)

// Delay for not accepting another door bell event. This delay defines
// how long two door bell events must be separated in time to be considered
// two individual events. Note that some users might ring several times in
// a short period of time. In such cases, we only want to send one event.
// -> 1 min
#define ALARM_INHIBIT_DELAY APP_TIMER_TICKS(60000, APP_TIMER_PRESCALER)

// The clock for updating the local time ticks every 
// LOCALTIME_CLOCK_INTERVAL_SEC seconds. Thus, we need to add this amount of 
// seconds every time the clock ticks.
#define LOCALTIME_CLOCK_INTERVAL_SEC 15
#define LOCALTIME_CLOCK_INTERVAL APP_TIMER_TICKS(\
(1000*LOCALTIME_CLOCK_INTERVAL_SEC), APP_TIMER_PRESCALER)

// Service and charateristic UUIDs in Little Endian format.
// The 16 bit values will become byte 12 and 13 of the 128 bit UUID:
// 0x451eXXXX-dd1c-4f20-a42e-ff91a53d2992
// 0x0a9dXXXX-5ff4-4c58-8a53627de7cf1faf
#define UUID_BASE {0x92, 0x29, 0x3d, 0xa5, 0x91, 0xff, 0x2e, 0xa4, \
	           0x20, 0x4f, 0x1c, 0xdd, 0x00, 0x00, 0x1e, 0x45}
#define UUID_SERVICE 0x0001
#define UUID_CHARACTERISTIC_DOOR_BELL_ALARM 0x0002
#define UUID_CHARACTERISTIC_LOCALTIME 0x0003

APP_TIMER_DEF(alarm_inhibit_timer);
APP_TIMER_DEF(localtime_timer);

uint8_t uuid_type;
uint16_t service_handle;
ble_gatts_char_handles_t char_handle_door_bell_alarm;
ble_gatts_char_handles_t char_handle_localtime;
uint16_t conn_handle = BLE_CONN_HANDLE_INVALID; 

// This variable signals, whether a client has subscribed to receive
// door bell alarm events.
volatile bool is_client_subscribed = false;

// This variable shows whether door bell events are blocked at the moment.
volatile bool is_alarm_inhibited = false;

// Local time of last door bell alarm.
// The variable is word-aligned to use single atomic LDR and STR operations to 
// load and store the variable.
uint32_t door_bell_alarm_time __attribute__ ((aligned (4))) = 0;

// Local time in seconds. 
// Local time has no relation to wall-clock time.
// Local time is updated with the interval defined by 
// LOCALTIME_CLOCK_INTERVAL_SEC. 
// The variable is word-aligned to use single atomic LDR and STR operations to 
// load and store the variable.
volatile uint32_t localtime __attribute__ ((aligned (4))) = 1;

volatile bool is_localtime_updated = false;
volatile bool is_door_bell_alarm = false;

static void led_off()
{
     // LED is active low -> set to turn off.
     nrf_gpio_pin_set(PIN_LED);
}

static void led_on()
{
     // LED is active low -> clear to turn on.
     nrf_gpio_pin_clear(PIN_LED);
}

static void led_init()
{
     nrf_gpio_cfg_output(PIN_LED);
     led_off();
}

/**
 * We follow the "let it crash" paradigm known from Erlang. If something
 * unexpected happens (in particular, an SDK function not returning
 * NRF_SUCCESS), we reboot the system. So "die" actually means, die and
 * be reborn ("self-healing"). Besides that, we of course try to avoid 
 * situations where things might fail (defensive programming). However, 
 * in very unlikely failure situations, we opt for crashing and rebooting 
 * rather than overloading code (and precious code memory) with failure 
 * handling routines.
 */
static void die()
{
     __disable_irq();
 
     // In a development system, we loop forever.
     // Remove the endless loop in a productive system to auto-reset.
     //while (1);
     
     // In a productive system, we automatically reset the system on errors.
     sd_nvic_SystemReset();
}

static void start_advertising()
{
    uint32_t err_code;
    ble_gap_adv_params_t adv_params;

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.type = BLE_GAP_ADV_TYPE_ADV_IND;
    adv_params.p_peer_addr = NULL;
    adv_params.fp = BLE_GAP_ADV_FP_ANY;
    adv_params.interval = ADV_INTERVAL;
    adv_params.timeout = ADV_TIMEOUT;

    err_code = sd_ble_gap_adv_start(&adv_params);
    if (err_code != NRF_SUCCESS)
	 die();
}

static void cccd_door_bell_alarm_write_evt(ble_gatts_evt_write_t *evt_write)
{
     // A subscription is made by the client by writing the characteristic's
     // CCCD (Client Characteristic Configuration Descriptor). A value of 
     // 0x0001 signals a subscription to notifications;
     // 0x0002 signals a subscription to indications. See:
     // See https://www.bluetooth.com/specifications/gatt/
     // viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.
     // client_characteristic_configuration.xml
     if (evt_write->handle == char_handle_door_bell_alarm.cccd_handle) {
	  if (evt_write->data[0] == 0x01 && evt_write->data[1] == 0x00) {
	       // Client subscribed to door bell alarm events.
	       is_client_subscribed = true;
	  } else if (evt_write->data[0] == 0x00 && evt_write->data[1] == 0x00) {
	       // Client unsubscribed from door bell alarm events.
	       is_client_subscribed = false;
	  }
     }
}

static void on_sys_evt(uint32_t sys_evt)
{
     // No need to handle any system events.
}

static void sys_evt_dispatch(uint32_t sys_evt)
{
    on_sys_evt(sys_evt);
}

static void ble_evt_handler(ble_evt_t *ble_evt)
{
     ble_gatts_evt_write_t *evt_write;

     switch (ble_evt->header.evt_id) {
     case BLE_GAP_EVT_CONNECTED:
	  conn_handle = ble_evt->evt.gap_evt.conn_handle;
	  // If we sometimes use bonding, note that bonded devices might 
	  // already have subscribed when they connect. Subscriptions 
	  // are stored for bonded devices.
	  is_client_subscribed = false;
	  break;
     case BLE_GAP_EVT_DISCONNECTED:
	  conn_handle = BLE_CONN_HANDLE_INVALID;
	  start_advertising();
	  break;
     case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
	  // Pairing not supported.
	  sd_ble_gap_sec_params_reply(conn_handle, 
				      BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP,
				      NULL, NULL);
	  break;
     case BLE_GATTS_EVT_WRITE:
	  evt_write = &ble_evt->evt.gatts_evt.params.write;
	  cccd_door_bell_alarm_write_evt(evt_write);
	  break;
     case BLE_GATTS_EVT_HVC:
	  // Indication has been acknowledged by the client.
	  // Not used. We just send notifications.
	  break;
     case BLE_GATTS_EVT_SYS_ATTR_MISSING:
	  // No system attributes have been stored.
	  sd_ble_gatts_sys_attr_set(conn_handle, NULL, 0, 0);
	  break;
     case BLE_GAP_EVT_TIMEOUT:
	  // TODO: Should we do something?
	  break;
    }
}

static void ble_stack_init()
{
     // The softdevice uses RTC0 (32 kHz real-time clock) for timing.
     // We use an external crystal with 20 ppm accuracy.
     SOFTDEVICE_HANDLER_INIT(NRF_CLOCK_LFCLKSRC_XTAL_20_PPM, false);

     // Enable BLE stack. 
     ble_enable_params_t ble_enable_params;
     memset(&ble_enable_params, 0, sizeof(ble_enable_params));
     if (sd_ble_enable(&ble_enable_params) != NRF_SUCCESS)
	  die();
     
     // Set Bluetooth address of device.
     ble_gap_addr_t addr;
     if (sd_ble_gap_address_get(&addr) != NRF_SUCCESS)
	  die();
     if (sd_ble_gap_address_set(BLE_GAP_ADDR_CYCLE_MODE_NONE, &addr) !=
	 NRF_SUCCESS)
	  die();
     
     // Subscribe for BLE events.
     if (softdevice_ble_evt_handler_set(ble_evt_handler) != NRF_SUCCESS)
	  die();
     
     // Subscribe for system events.
     // Actually, so far, we do not need to handle any system events.
     // But maybe in the future, for instance, when we store some events
     // persistently in flash or similar.
     if (softdevice_sys_evt_handler_set(sys_evt_dispatch) !=
	 NRF_SUCCESS)
	  die();
}

static void gap_init()
{
     ble_gap_conn_params_t gap_conn_params;
     ble_gap_conn_sec_mode_t sec_mode;

     // Open link, no encryption required on BLE layer.
     BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
     
     // Set device name.
     if (sd_ble_gap_device_name_set(&sec_mode,
				    (const uint8_t *) DEVICE_NAME,
				    strlen(DEVICE_NAME)) != NRF_SUCCESS)
	  die();
     
     // Set connection parameters.
     memset(&gap_conn_params, 0, sizeof(gap_conn_params));
     gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
     gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
     gap_conn_params.slave_latency = SLAVE_LATENCY;
     gap_conn_params.conn_sup_timeout = CONN_SUP_TIMEOUT;     
     if (sd_ble_gap_ppcp_set(&gap_conn_params) != NRF_SUCCESS)
	  die();
}

static void set_localtime_char()
{
     // Make a copy of localtime to avoid race conditions of
     // concurrent write operations to variable localtime.
     // 32 bit load operations are atomic on ARM Cortex M0, so we do not
     // need to protect this assignment by disabling interrupts.
     uint32_t t = localtime;

     ble_gatts_value_t value;
     value.len = sizeof(t);
     value.offset = 0;
     value.p_value = (uint8_t *) &t;

     if (sd_ble_gatts_value_set(conn_handle, 
				char_handle_localtime.value_handle,
				&value) != NRF_SUCCESS)
	  die();
}

static void set_door_bell_alarm_char()
{
     // Make a copy of door_bell_alarm_time to avoid race conditions of
     // concurrent write operations to variable door_bell_alarm_time.
     // 32 bit load operations are atomic on ARM Cortex M0, so we do not
     // need to protect this assignment by disabling interrupts.
     uint32_t t = door_bell_alarm_time;

     ble_gatts_value_t value;
     value.len = sizeof(t);
     value.offset = 0;
     value.p_value = (uint8_t *) &t;

     if (sd_ble_gatts_value_set(conn_handle, 
				char_handle_door_bell_alarm.value_handle,
				&value) != NRF_SUCCESS)
	  die();
}

static void add_characteristic_door_bell_alarm(uint16_t service_handle)
{
     // Characteristic UUID.
     ble_uuid_t ble_uuid;
     ble_uuid.type = uuid_type;
     ble_uuid.uuid = UUID_CHARACTERISTIC_DOOR_BELL_ALARM;

     // Define characteristic presentation format.
     // The door bell alarm is a single unsigned 32 bit integer representing
     // a timestamp when the event happened.
     ble_gatts_char_pf_t char_presentation_format;
     memset(&char_presentation_format, 0, sizeof(char_presentation_format));
     char_presentation_format.format = BLE_GATT_CPF_FORMAT_UINT32;
     char_presentation_format.exponent = 0;
     char_presentation_format.unit = 0x2703; // seconds

     // Define CCCD attributes. 
     // CCCD (Client Characteristic Configuration Descriptor) is used by the
     // client to enable notifications or indications by writing a flag to the
     // CCCD attribute.
     ble_gatts_attr_md_t cccd_meta_data;
     memset(&cccd_meta_data, 0, sizeof(cccd_meta_data));
     // CCCD must be readable and writeable. 
     BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_meta_data.read_perm);
     BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_meta_data.write_perm);
     cccd_meta_data.vloc = BLE_GATTS_VLOC_STACK;

     // Define characteristic meta data.
     // The door bell alarm is readable and can send notifications.
     ble_gatts_char_md_t char_meta_data;
     memset(&char_meta_data, 0, sizeof(char_meta_data));
     char_meta_data.char_props.read = 1;
     char_meta_data.char_props.write = 0;
     char_meta_data.char_props.notify = 1;
     char_meta_data.char_props.indicate = 0;
     char_meta_data.p_char_user_desc = NULL;
     char_meta_data.p_char_pf = &char_presentation_format;
     char_meta_data.p_user_desc_md = NULL;
     // CCCD (Client Characteristic Configuration Descriptor) needs to be 
     // set for characteristics allowing for notifications and indications.
     char_meta_data.p_cccd_md = &cccd_meta_data;
     char_meta_data.p_sccd_md = NULL;

     // Define attribute meta data. 
     ble_gatts_attr_md_t char_attr_meta_data;
     memset(&char_attr_meta_data, 0, sizeof(char_attr_meta_data));
     // No security needed.
     BLE_GAP_CONN_SEC_MODE_SET_OPEN(&char_attr_meta_data.read_perm);
     BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&char_attr_meta_data.write_perm);
     // value location
     char_attr_meta_data.vloc = BLE_GATTS_VLOC_STACK;
     // always request read authorization from application 
     char_attr_meta_data.rd_auth = 0;
     // always request write authorization from application
     char_attr_meta_data.wr_auth = 0;
     // variable length attribute
     char_attr_meta_data.vlen = 0;

     // Define characteristic attributes. 
     // Door bell alarm is a fixed length 32 bit data structure.
     ble_gatts_attr_t char_attributes;
     memset(&char_attributes, 0, sizeof(char_attributes));
     char_attributes.p_uuid = &ble_uuid;
     char_attributes.p_attr_md = &char_attr_meta_data;
     char_attributes.init_len = sizeof(door_bell_alarm_time);
     char_attributes.init_offs = 0;
     char_attributes.max_len = MAX_LENGTH_DOOR_BELL_ALARM_CHAR;
     // For attributes managed by the application (BLE_GATTS_VLOC_USER)
     // rather than the BLE stack, set a pointer to the memory location here.
     char_attributes.p_value = (uint8_t *) &door_bell_alarm_time;

     // Add characteristic to service.
     if (sd_ble_gatts_characteristic_add(service_handle,
					 &char_meta_data,
					 &char_attributes,
					 &char_handle_door_bell_alarm) 
	 != NRF_SUCCESS)
	  die();
}

static void add_characteristic_localtime(uint16_t service_handle)
{
     // Characteristic UUID.
     ble_uuid_t ble_uuid;
     ble_uuid.type = uuid_type;
     ble_uuid.uuid = UUID_CHARACTERISTIC_LOCALTIME;

     // Define characteristic presentation format.
     // The localtime characteristic is a single unsigned 32 bit integer 
     // representing a timestamp in seconds.
     ble_gatts_char_pf_t char_presentation_format;
     memset(&char_presentation_format, 0, sizeof(char_presentation_format));
     char_presentation_format.format = BLE_GATT_CPF_FORMAT_UINT32;
     char_presentation_format.exponent = 0;
     char_presentation_format.unit = 0x2703; // seconds

     // Define characteristic meta data.
     // Localtime is readable.
     ble_gatts_char_md_t char_meta_data;
     memset(&char_meta_data, 0, sizeof(char_meta_data));
     char_meta_data.char_props.read = 1;
     char_meta_data.char_props.write = 0;
     char_meta_data.char_props.notify = 0;
     char_meta_data.char_props.indicate = 0;
     char_meta_data.p_char_user_desc = NULL;
     char_meta_data.p_char_pf = &char_presentation_format;
     char_meta_data.p_user_desc_md = NULL;
     // CCCD (Client Characteristic Configuration Descriptor) needs to be 
     // set for characteristics allowing for notifications and indications.
     char_meta_data.p_cccd_md = NULL;
     char_meta_data.p_sccd_md = NULL;

     // Define attribute meta data. 
     ble_gatts_attr_md_t char_attr_meta_data;
     memset(&char_attr_meta_data, 0, sizeof(char_attr_meta_data));
     // No security needed.
     BLE_GAP_CONN_SEC_MODE_SET_OPEN(&char_attr_meta_data.read_perm);
     BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&char_attr_meta_data.write_perm);
     // value location
     char_attr_meta_data.vloc = BLE_GATTS_VLOC_STACK;
     // always request read authorization from application 
     char_attr_meta_data.rd_auth = 0;
     // always request write authorization from application
     char_attr_meta_data.wr_auth = 0;
     // variable length attribute
     char_attr_meta_data.vlen = 0;

     // Define characteristic attributes. 
     ble_gatts_attr_t char_attributes;
     memset(&char_attributes, 0, sizeof(char_attributes));
     char_attributes.p_uuid = &ble_uuid;
     char_attributes.p_attr_md = &char_attr_meta_data;
     char_attributes.init_len = sizeof(localtime);
     char_attributes.init_offs = 0;
     char_attributes.max_len = MAX_LENGTH_LOCALTIME_CHAR;
     // For attributes managed by the application (BLE_GATTS_VLOC_USER)
     // rather than the BLE stack, set a pointer to the memory location here.
     char_attributes.p_value = (uint8_t *) &localtime;

     // Add characteristic to service.
     if (sd_ble_gatts_characteristic_add(service_handle,
					 &char_meta_data,
					 &char_attributes,
					 &char_handle_localtime) 
	 != NRF_SUCCESS)
	  die();
}

static void service_init()
{
     uint32_t err_code;
     
     // Add base UUID to list of base UUIDs.
     // The uuid_type field filled by this function call can be used later to
     // refer to this base UUID.
     ble_uuid128_t base_uuid = {UUID_BASE};
     err_code = sd_ble_uuid_vs_add(&base_uuid, &uuid_type);
     if (err_code != NRF_SUCCESS)
	  die();

     // Build 128 bit service UUID by referring to base UUID using uuid_type
     // and specifying the two bytes that will replace byte 12 and 13 of the
     // base UUID. 
     ble_uuid_t ble_uuid;
     ble_uuid.type = uuid_type;
     ble_uuid.uuid = UUID_SERVICE;
     
     err_code = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, 
					 &ble_uuid, &service_handle);
     if (err_code != NRF_SUCCESS)
	  die();
     
     // Add characteristics to service.
     add_characteristic_door_bell_alarm(service_handle);
     add_characteristic_localtime(service_handle);
}

static void conn_params_error_handler(uint32_t nrf_error)
{
     die();
}

static void on_conn_params_evt(ble_conn_params_evt_t * p_evt)
{
     switch (p_evt->evt_type) {
     case BLE_CONN_PARAMS_EVT_FAILED :
	  // Negotiation of connection parameters finally failed
	  // -> disconnect.
	  if (sd_ble_gap_disconnect(conn_handle, 
				    BLE_HCI_CONN_INTERVAL_UNACCEPTABLE) !=
	      NRF_SUCCESS)
	       die();
	  break;
     case BLE_CONN_PARAMS_EVT_SUCCEEDED :
	  break;
     }
}

static void conn_params_init()
{
    ble_conn_params_init_t cp_init;
    ble_gap_conn_params_t conn_parameters;

    conn_parameters.min_conn_interval = MIN_CONN_INTERVAL;
    conn_parameters.max_conn_interval = MAX_CONN_INTERVAL;
    conn_parameters.slave_latency = SLAVE_LATENCY;
    conn_parameters.conn_sup_timeout = CONN_SUP_TIMEOUT;

    memset(&cp_init, 0, sizeof(cp_init));
    cp_init.p_conn_params = &conn_parameters;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail = false;
    cp_init.evt_handler = on_conn_params_evt;
    cp_init.error_handler = conn_params_error_handler;

    if (ble_conn_params_init(&cp_init) != NRF_SUCCESS)
	 die();
}

static void advertising_init(void)
{
     ble_uuid_t adv_uuids[] = {{UUID_SERVICE, uuid_type}};
     
     ble_advdata_t advdata;
     memset(&advdata, 0, sizeof(advdata));
     advdata.name_type = BLE_ADVDATA_FULL_NAME;
     advdata.include_appearance = false;
     // LE General Discoverable Mode.
     advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
     // Send complete set of UUIDs.
     advdata.uuids_complete.uuid_cnt = sizeof(adv_uuids)/sizeof(adv_uuids[0]);
     advdata.uuids_complete.p_uuids = adv_uuids;
     
     // No scan response data needs to be defined (second parameter) since 
     // everything fits into the advertisement message (the scan response can 
     // be requested by the central device to get more information from the 
     // peripheral).
     if (ble_advdata_set(&advdata, NULL) != NRF_SUCCESS)
	  die();
}

static void alarm_inhibit_timer_evt_handler(void *p_context)
{
     UNUSED_PARAMETER(p_context);
     // Will accept again door bell signals.
     is_alarm_inhibited = false;
}

static void localtime_timer_evt_handler(void *p_context)
{
     UNUSED_PARAMETER(p_context);

     // This routine, which is executed in interrupt context, is the only 
     // place where the 32 bit variable localtime is updated. So we 
     // do not need to protect against concurrent write opertions
     // by disabling interrupts.
     localtime += LOCALTIME_CLOCK_INTERVAL_SEC;

     is_localtime_updated = true;
}

static uint32_t local_time()
{
     // On ARM Cortex M0, storing and loading 32 bit values (STR, LDR) are 
     // atomic operations. Thus, we do not need to protect the read operation
     // against concurrent write operations by disabling interrupts.
     return localtime;
}

static void timers_init()
{
     // Initialize application timer using RTC1 (RTC0 is used by the
     // BLE softdevice).
     APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_QUEUE_SIZE, false);

     if (app_timer_create(&alarm_inhibit_timer, APP_TIMER_MODE_SINGLE_SHOT,
			  alarm_inhibit_timer_evt_handler) != NRF_SUCCESS)
	  die();

     if (app_timer_create(&localtime_timer, APP_TIMER_MODE_REPEATED,
			  localtime_timer_evt_handler) != NRF_SUCCESS)
	  die();
}

static void start_alarm_inhibit_timer()
{
     if (app_timer_start(alarm_inhibit_timer, ALARM_INHIBIT_DELAY, NULL) !=
	 NRF_SUCCESS)
	  die();
}

static void start_localtime_timer()
{
     if (app_timer_start(localtime_timer, LOCALTIME_CLOCK_INTERVAL, NULL) !=
	 NRF_SUCCESS)
	  die();
}

static void stop_alarm_inhibit_timer()
{
     app_timer_stop(alarm_inhibit_timer);
}

static void notify_door_bell_alarm()
{
     // Make a copy of door_bell_alarm_time to avoid race conditions of
     // concurrent write operations to variable door_bell_alarm_time.
     // (Strictly speaking, concurrent writes are very unlikely due to 
     // the long inhibit period preventing immediate follow-up updates.)
     // 32 bit load operations are atomic on ARM Cortex M0, so we do not
     // need to protect this assignment by disabling interrupts.
     uint32_t t = door_bell_alarm_time;

     ble_gatts_hvx_params_t params;
     uint16_t len = sizeof(t);
     
     // Send door bell alarm event as notification.
     memset(&params, 0, sizeof(params));
     params.type = BLE_GATT_HVX_NOTIFICATION;
     params.handle = char_handle_door_bell_alarm.value_handle;
     params.p_data = (uint8_t *) &t;
     params.p_len = &len;
     if (sd_ble_gatts_hvx(conn_handle, &params) != NRF_SUCCESS)
	  die();
}

static void buttons_evt_handler(uint8_t pin_no, uint8_t action)
{
     switch (pin_no) {
     case PIN_BELL :
	  if (APP_BUTTON_PUSH == action)
	       is_door_bell_alarm = true;
	  break;
     }
}

static void buttons_init()
{
     // We treat the door bell GPIO as a button which is active low.
     // The DoorBell20 board has a pull-up resistor. For the nRF51 DK, we 
     // enable the internal pull-up resistor of the nRF51 chip.
     // Door bell signal is active low -> second parameter = false.
     #ifdef TARGET_BOARD_NRF51DK
     static app_button_cfg_t buttons[] = {
	  {PIN_BELL, false, NRF_GPIO_PIN_PULLUP, buttons_evt_handler}
     };
     #else
     static app_button_cfg_t buttons[] = {
	  {PIN_BELL, false, NRF_GPIO_PIN_NOPULL, buttons_evt_handler}
     };
     #endif
     if (app_button_init(buttons, sizeof(buttons) / sizeof(buttons[0]),
			 DEBOUNCING_DELAY) != NRF_SUCCESS)
	  die();
}

static void start_button_event_detection()
{
     if (app_button_enable() != NRF_SUCCESS)
          die();
}

int main(void)
{
     led_init();
	       
     timers_init();
     buttons_init();
     ble_stack_init();
     gap_init();
     service_init();
     advertising_init();
     conn_params_init();

     start_localtime_timer();
     start_advertising();
     start_button_event_detection();

     while (1) {
	  // The following function puts the processor into sleep mode
	  // and waits for interrupts to wake up. Wakeup events include
	  // events from the softdevice, which are processed in the BLE event 
	  // loop, or other events like interrupts from application timers and
	  // pressed buttons.
	  sd_app_evt_wait();

	  if (is_localtime_updated) {
	       // Update the localtime characteristic value to reflect current
	       // time.
	       set_localtime_char();
	       is_localtime_updated = false;
	  }
	  
	  if (is_door_bell_alarm) {
	       if (!is_alarm_inhibited) {
		    // This is the only place where where variable 
		    // door_bell_alarm_time is written. So we do not have to 
		    // protect it against concurrent write operations. 
		    // Moreover, the 32 bit load operation for reading 
		    // variable localtime is atomic on ARM Cortex M0. Thus, we 
		    // do not need to protect this load operation against 
		    // concurrent updates by disabling interrupts.
		    door_bell_alarm_time = localtime;
		    if (is_client_subscribed) {
			 notify_door_bell_alarm();	 
		    } else {
			 set_door_bell_alarm_char();
		    }
		    is_alarm_inhibited = true;
		    start_alarm_inhibit_timer();
	       }
	       is_door_bell_alarm = false;
	  }
     }
}
