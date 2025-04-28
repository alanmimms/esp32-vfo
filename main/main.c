/* ESP32-VFO

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sys/param.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "soc/soc_caps.h"
#include "cmd_system.h"
#include "cmd_wifi.h"
#include "cmd_nvs.h"
#include "cmd_i2c.h"
#include "console_settings.h"

#include "esp_event.h"
#include "esp_mac.h"

#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "mdns.h"

#include "esp_http_server.h"
#include "dns_server.h"

#define ESP32VFO_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define ESP32VFO_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define ESP32VFO_MAX_STA_CONN CONFIG_ESP_MAX_STA_CONN

extern const char root_start[] asm("_binary_root_html_start");
extern const char root_end[] asm("_binary_root_html_end");

static const char *TAG = "esp32-vfo";



/*
 * We warn if a secondary serial console is enabled. A secondary serial console is always output-only and
 * hence not very useful for interactive console applications. If you encounter this warning, consider disabling
 * the secondary serial console in menuconfig unless you know what you are doing.
 */
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#if !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#warning "A secondary serial console is not useful when using the console component. Please disable it in menuconfig."
#endif
#endif

#define PROMPT_STR CONFIG_IDF_TARGET

/* Console command history can be stored to and loaded from a file.
 * The easiest way to do this is to use FATFS filesystem on top of
 * wear_levelling library.
 */
#if CONFIG_CONSOLE_STORE_HISTORY

#define MOUNT_PATH "/data"
#define HISTORY_PATH MOUNT_PATH "/history.txt"

static void initialize_filesystem(void)
{
  static wl_handle_t wl_handle;
  const esp_vfs_fat_mount_config_t mount_config = {
    .max_files = 4,
    .format_if_mount_failed = true
  };
  esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_PATH, "storage", &mount_config, &wl_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
    return;
  }
}
#else
#define HISTORY_PATH NULL
#endif // CONFIG_CONSOLE_STORE_HISTORY

static void initialize_nvs(void)
{
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK( nvs_flash_erase() );
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}


static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
  if (event_id == WIFI_EVENT_AP_STACONNECTED) {
    wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
    ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
	     MAC2STR(event->mac), event->aid);
  } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
    ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d, reason=%d",
	     MAC2STR(event->mac), event->aid, event->reason);
  }
}


void WiFiSetAP() {
}


static void wifi_init_softap(void)
{
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

  static wifi_config_t wifi_config = {
    .ap = {
      .ssid = ESP32VFO_ESP_WIFI_SSID,
      .ssid_len = strlen(ESP32VFO_ESP_WIFI_SSID),
      .password = ESP32VFO_ESP_WIFI_PASS,
      .max_connection = ESP32VFO_MAX_STA_CONN,
      .authmode = WIFI_AUTH_WPA_WPA2_PSK
    },
  };

  if (ESP32VFO_ESP_WIFI_PASS[0] == 0) wifi_config.ap.authmode = WIFI_AUTH_OPEN;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  esp_netif_ip_info_t ip_info;
  esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

  char ip_addr[16];
  inet_ntoa_r(ip_info.ip.addr, ip_addr, 16);
  ESP_LOGI(TAG, "Set up softAP with IP: %s", ip_addr);

  ESP_LOGI(TAG, "wifi_init_softap finished. SSID:'%s' password:'%s'",
	   ESP32VFO_ESP_WIFI_SSID, ESP32VFO_ESP_WIFI_PASS);
}

#ifdef CONFIG_ESP_ENABLE_DHCP_CAPTIVEPORTAL
static void dhcp_set_captiveportal_url(void) {
  // get the IP of the access point to redirect to
  esp_netif_ip_info_t ip_info;
  esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

  char ip_addr[16];
  inet_ntoa_r(ip_info.ip.addr, ip_addr, 16);
  ESP_LOGI(TAG, "Set up softAP with IP: %s", ip_addr);

  // turn the IP into a URI
  char* captiveportal_uri = (char*) malloc(32 * sizeof(char));
  assert(captiveportal_uri && "Failed to allocate captiveportal_uri");
  strcpy(captiveportal_uri, "http://");
  strcat(captiveportal_uri, ip_addr);

  // get a handle to configure DHCP with
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

  // set the DHCP option 114
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(netif));
  ESP_ERROR_CHECK(esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, captiveportal_uri, strlen(captiveportal_uri)));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(netif));
}
#endif // CONFIG_ESP_ENABLE_DHCP_CAPTIVEPORTAL

// HTTP GET Handler
static esp_err_t root_get_handler(httpd_req_t *req)
{
  const uint32_t root_len = root_end - root_start;

  ESP_LOGI(TAG, "Serve root");
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, root_start, root_len);

  return ESP_OK;
}

static const httpd_uri_t root = {
  .uri = "/",
  .method = HTTP_GET,
  .handler = root_get_handler
};

// HTTP Error (404) Handler - Redirects all requests to the root page
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
  // Set status
  httpd_resp_set_status(req, "302 Temporary Redirect");
  // Redirect to the "/" root directory
  httpd_resp_set_hdr(req, "Location", "/");
  // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
  httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

  ESP_LOGI(TAG, "Redirecting to root");
  return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_open_sockets = 13;
  config.lru_purge_enable = true;

  // Start the httpd server
  ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
  if (httpd_start(&server, &config) == ESP_OK) {
    // Set URI handlers
    ESP_LOGI(TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &root);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
  }
  return server;
}


static void start_mdns_service() {
  //initialize mDNS service
  esp_err_t err = mdns_init();
  if (err) {
    printf("MDNS Init failed: %d\n", err);
    return;
  }

  //set hostname
  mdns_hostname_set(CONFIG_LOCAL_HOSTNAME);

  //set default instance
  mdns_instance_name_set("ESP32 VFO");
}


void app_main(void)
{
  initialize_nvs();

#if CONFIG_CONSOLE_STORE_HISTORY
  initialize_filesystem();
  ESP_LOGI(TAG, "Command history enabled");
#else
  ESP_LOGI(TAG, "Command history disabled");
#endif

  /* Initialize console output periheral (UART, USB_OTG, USB_JTAG) */
  initialize_console_peripheral();

  /* Initialize linenoise library and esp_console*/
  initialize_console_library(HISTORY_PATH);

  /* Prompt to be printed before each line.
   * This can be customized, made dynamic, etc.
   */
  const char *prompt = setup_prompt(PROMPT_STR ">");

  /*
    Turn of warnings from HTTP server as redirecting traffic will yield
    lots of invalid requests
  */
  esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
  esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
  esp_log_level_set("httpd_parse", ESP_LOG_ERROR);


  // Initialize networking stack
  ESP_ERROR_CHECK(esp_netif_init());

  // Create default event loop needed by the  main app
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Initialize NVS needed by Wi-Fi
  ESP_ERROR_CHECK(nvs_flash_init());

  // Initialize Wi-Fi including netif with default config
  esp_netif_create_default_wifi_ap();

  // Initialise ESP32 in SoftAP mode
  wifi_init_softap();

  // Configure DNS-based captive portal, if configured
#ifdef CONFIG_ESP_ENABLE_DHCP_CAPTIVEPORTAL
  dhcp_set_captiveportal_url();
#endif

  // Define this hostname on MDNS protocol for local network.
  start_mdns_service();

  // Start the server for the first time
  start_webserver();

  // Start the DNS server that will redirect all queries to the softAP IP
  dns_server_config_t config = DNS_SERVER_CONFIG_SINGLE("*" /* all A queries */, "WIFI_AP_DEF" /* softAP netif ID */);
  start_dns_server(&config);


  /* Register commands */
  esp_console_register_help_command();
  register_system_common();
#if SOC_LIGHT_SLEEP_SUPPORTED
  register_system_light_sleep();
#endif
#if SOC_DEEP_SLEEP_SUPPORTED
  register_system_deep_sleep();
#endif
#if SOC_WIFI_SUPPORTED
  register_wifi();
#endif
  register_nvs();
  register_i2c();

  printf("\n"
	 "Type 'help' to get the list of commands.\n"
	 "Use UP/DOWN arrows to navigate through command history.\n"
	 "Press TAB when typing command name to auto-complete.\n");

  if (linenoiseIsDumbMode()) {
    printf("\n"
	   "Your terminal application does not support escape sequences.\n"
	   "Line editing and history features are disabled.\n"
	   "On Windows, try using Putty instead.\n");
  }

  /* Main loop */
  while(true) {
    /* Get a line using linenoise.
     * The line is returned when ENTER is pressed.
     */
    char* line = linenoise(prompt);

#if CONFIG_CONSOLE_IGNORE_EMPTY_LINES
    if (line == NULL) { /* Ignore empty lines */
      continue;;
    }
#else
    if (line == NULL) { /* Break on EOF or error */
      break;
    }
#endif // CONFIG_CONSOLE_IGNORE_EMPTY_LINES

    /* Add the command to the history if not empty*/
    if (strlen(line) > 0) {
      linenoiseHistoryAdd(line);
#if CONFIG_CONSOLE_STORE_HISTORY
      /* Save command history to filesystem */
      linenoiseHistorySave(HISTORY_PATH);
#endif // CONFIG_CONSOLE_STORE_HISTORY
    }

    /* Try to run the command */
    int ret;
    esp_err_t err = esp_console_run(line, &ret);
    if (err == ESP_ERR_NOT_FOUND) {
      printf("Unrecognized command\n");
    } else if (err == ESP_ERR_INVALID_ARG) {
      // command was empty
    } else if (err == ESP_OK && ret != ESP_OK) {
      printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
    } else if (err != ESP_OK) {
      printf("Internal error: %s\n", esp_err_to_name(err));
    }
    /* linenoise allocates line buffer on the heap, so need to free it */
    linenoiseFree(line);
  }

  ESP_LOGE(TAG, "Error or end-of-input, terminating console");
  esp_console_deinit();
}
