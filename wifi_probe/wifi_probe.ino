/*
Change from https://github.com/ESP-EOS/ESP32-WiFi-Sniffer

*/

#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

//Makerfabs init
#include "makerfabs_pin.h"
#include "SPI.h"
#include "SD.h"
#include "FS.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

String filename = "";
Adafruit_SSD1306 display(MP_ESP32_SSD1306_WIDTH, MP_ESP32_SSD1306_HEIGHT, &Wire, MP_ESP32_SSD1306_RST);

#define WIFI_CHANNEL_SWITCH_INTERVAL (500)
#define WIFI_CHANNEL_MAX (13)

uint8_t mac_lib[100][6];
int mac_count = 0;

uint8_t level = 0, channel = 1;

static wifi_country_t wifi_country = {.cc = "CN", .schan = 1, .nchan = 13}; //Most recent esp32 library struct

typedef struct
{
  unsigned frame_ctrl : 16;
  unsigned duration_id : 16;
  uint8_t addr1[6]; /* receiver address */
  uint8_t addr2[6]; /* sender address */
  uint8_t addr3[6]; /* filtering address */
  unsigned sequence_ctrl : 16;
  uint8_t addr4[6]; /* optional */
} wifi_ieee80211_mac_hdr_t;

typedef struct
{
  wifi_ieee80211_mac_hdr_t hdr;
  uint8_t payload[0]; /* network data ended with 4 bytes csum (CRC32) */
} wifi_ieee80211_packet_t;

static esp_err_t event_handler(void *ctx, system_event_t *event);
static void wifi_sniffer_init(void);
static void wifi_sniffer_set_channel(uint8_t channel);
static const char *wifi_sniffer_packet_type2str(wifi_promiscuous_pkt_type_t type);
static void wifi_sniffer_packet_handler(void *buff, wifi_promiscuous_pkt_type_t type);

// the setup function runs once when you press reset or power the board
void setup()
{
  // initialize digital pin 5 as an output.
  Serial.begin(115200);
  delay(10);

  //LCD init
  Wire.begin(MP_ESP32_I2C_SDA, MP_ESP32_I2C_SCL);
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, MP_ESP32_SSD1306_I2C_ADDR))
  { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ; // Don't proceed, loop forever
  }
  display.clearDisplay();
  lcd_text("WIFI SNIFFER");

  //SD(SPI)
  pinMode(MP_A9G_SD_CS, OUTPUT);
  digitalWrite(MP_A9G_SD_CS, HIGH);
  SPI.begin(MP_A9G_SPI_SCK, MP_A9G_SPI_MISO, MP_A9G_SPI_MOSI);
  SPI.setFrequency(1000000);
  if (!SD.begin(MP_A9G_SD_CS, SPI))
  {
    Serial.println("Card Mount Failed");
    lcd_text("Card Mount Failed");
    while (1)
      ;
  }
  else
  {
    Serial.println("SD OK");
  }

  int log_index = 0;
  while (1)
  {
    filename = "/log" + String(log_index) + ".txt";
    Serial.println("Check if the " + filename + " exists ");
    if (!SD.exists(filename))
    {
      //Open log
      writeFile(SD, filename, "WIFI COUNT:");
      break;
    }
    log_index++;
  }

  lcd_text(filename);

  //Init sniffer
  wifi_sniffer_init();
}

// the loop function runs over and over again forever
void loop()
{
  Serial.printf("______________CHANNEL:%02d____________\n", channel);
  delay(1000); // wait for a second

  vTaskDelay(WIFI_CHANNEL_SWITCH_INTERVAL / portTICK_PERIOD_MS);
  wifi_sniffer_set_channel(channel);
  channel = (channel % WIFI_CHANNEL_MAX) + 1;
  if (channel == 1)
  {
    String data = create_data_str(mac_count,get_run_time());
    Serial.println(data);
    appendFile(SD, filename, data);

    lcd_text(String(mac_count) + "/" + String(millis() / 1000));

    mac_count = 0;
  }
}

//Check and Save Only MAC
int check_mac_only(const uint8_t addr3[6])
{
  for (char i = 0; i < mac_count; i++)
  {
    bool flag = true;
    for (char j = 0; j < 6; j++)
    {
      if (mac_lib[i][j] != addr3[j])
      {
        flag = false;
        break;
      }
    }
    if (flag == true)
      return 0;
  }
  for (char j = 0; j < 6; j++)
  {
    mac_lib[mac_count][j] = addr3[j];
  }
  mac_count++;
  return 1;
}

void writeFile(fs::FS &fs, String path, String message)
{
  Serial.println("Writing file: " + path);

  File file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.println(message))
  {
    Serial.println("File written");
  }
  else
  {
    Serial.println("Write failed");
  }
  file.close();
}

void appendFile(fs::FS &fs, String path, String message)
{
  Serial.println("Appending to file: " + path);

  File file = fs.open(path, FILE_APPEND);

  if (bool(file))
  {
    if (file.println(message))
    {
      Serial.println("Message appended");
    }
    else
    {
      Serial.println("Append failed");
      lcd_text("SD WRITE ERROR");
      while (1)
        ;
    }
    file.close();
  }
  else
  {
    Serial.println("Failed to open file for appending");
    file.close();
    return;
  }
}

int get_run_time()
{
  return millis() / 1000;
}

String create_data_str(int count,int run_time)
{
  String data = "";
  data = "{'COUNT':'" + String(mac_count) + "','TIME':'"+ String(get_run_time()) + "'}";
  return data;
}

void lcd_text(String text)
{
  display.clearDisplay();

  display.setTextSize(2);              // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);             // Start at top-left corner
  display.println(text);
  display.display();
  delay(500);
}

esp_err_t event_handler(void *ctx, system_event_t *event)
{
  return ESP_OK;
}

void wifi_sniffer_init(void)
{
  nvs_flash_init();
  tcpip_adapter_init();
  ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_country(&wifi_country)); /* set country for channel range [1, 13] */
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
  ESP_ERROR_CHECK(esp_wifi_start());
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
}

void wifi_sniffer_set_channel(uint8_t channel)
{
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

const char *wifi_sniffer_packet_type2str(wifi_promiscuous_pkt_type_t type)
{
  switch (type)
  {
  case WIFI_PKT_MGMT:
    return "MGMT";
  case WIFI_PKT_DATA:
    return "DATA";
  default:
  case WIFI_PKT_MISC:
    return "MISC";
  }
}

void wifi_sniffer_packet_handler(void *buff, wifi_promiscuous_pkt_type_t type)
{
  if (type != WIFI_PKT_MGMT)
    return;

  const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buff;
  const wifi_ieee80211_packet_t *ipkt = (wifi_ieee80211_packet_t *)ppkt->payload;
  const wifi_ieee80211_mac_hdr_t *hdr = &ipkt->hdr;

  if (check_mac_only(hdr->addr3))
    Serial.printf("PACKET TYPE=%s, CHAN=%02d, RSSI=%02d,"
                  " ADDR1=%02x:%02x:%02x:%02x:%02x:%02x,"
                  " ADDR2=%02x:%02x:%02x:%02x:%02x:%02x,"
                  " ADDR3=%02x:%02x:%02x:%02x:%02x:%02x\n",
                  wifi_sniffer_packet_type2str(type),
                  ppkt->rx_ctrl.channel,
                  ppkt->rx_ctrl.rssi,
                  /* ADDR1 */
                  hdr->addr1[0], hdr->addr1[1], hdr->addr1[2],
                  hdr->addr1[3], hdr->addr1[4], hdr->addr1[5],
                  /* ADDR2 */
                  hdr->addr2[0], hdr->addr2[1], hdr->addr2[2],
                  hdr->addr2[3], hdr->addr2[4], hdr->addr2[5],
                  /* ADDR3 */
                  hdr->addr3[0], hdr->addr3[1], hdr->addr3[2],
                  hdr->addr3[3], hdr->addr3[4], hdr->addr3[5]);
}