#include "dns_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include <string.h>

#define DNS_PORT 53
#define DNS_MAX_QUERY 512
#define DNS_HEADER_LEN 12

typedef struct __attribute__((packed)) {
  uint16_t id;
  uint16_t flags;
  uint16_t qdcount;
  uint16_t ancount;
  uint16_t nscount;
  uint16_t arcount;
} dns_header_t;

static const char *TAG = "dns";
static TaskHandle_t g_dns_task = NULL;

static void dns_reply_to_esp_ip(uint8_t *buffer, size_t len) {
  if (len < DNS_HEADER_LEN)
    return;

  // Minimal DNS response: set QR bit, echo query, append A-record answer
  // pointing to 192.168.4.1 (c0 a8 04 01)
  uint8_t response[128] = {0};
  size_t rlen = 0;

  // Copy query ID and set response flag
  memcpy(response, buffer, 2);
  response[2] = 0x81;
  response[3] = 0x80;
  // QDCOUNT = 1, ANCOUNT = 1
  response[4] = 0x00;
  response[5] = 0x01;
  response[6] = 0x00;
  response[7] = 0x01;
  rlen = 12;

  // Echo the original question
  size_t qlen = 12;
  while (qlen < len && buffer[qlen] != 0) {
    uint8_t label_len = buffer[qlen];
    qlen += 1 + label_len;
  }
  qlen += 5; // skip null + QTYPE + QCLASS
  memcpy(response + rlen, buffer + 12, qlen - 12);
  rlen += qlen - 12;

  // Answer: type A, class IN, TTL 60s, IP 192.168.4.1
  uint16_t p = htons(0xC00C); // pointer to query name
  memcpy(response + rlen, &p, 2);
  rlen += 2;
  response[rlen++] = 0x00;
  response[rlen++] = 0x01; // A record
  response[rlen++] = 0x00;
  response[rlen++] = 0x01; // IN class
  response[rlen++] = 0x00;
  response[rlen++] = 0x00;
  response[rlen++] = 0x00;
  response[rlen++] = 0x3C; // TTL 60
  response[rlen++] = 0x00;
  response[rlen++] = 0x04; // data len = 4
  response[rlen++] = 192;
  response[rlen++] = 168;
  response[rlen++] = 4;
  response[rlen++] = 1;

  memcpy(buffer, response, rlen);
}

static void dns_task(void *arg) {
  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(DNS_PORT),
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    ESP_LOGE(TAG, "socket create failed");
    vTaskDelete(NULL);
    return;
  }

  bind(sock, (struct sockaddr *)&addr, sizeof(addr));

  uint8_t buf[DNS_MAX_QUERY];
  struct sockaddr_in client;
  socklen_t client_len = sizeof(client);

  while (1) {
    int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&client,
                       &client_len);
    if (len < 0)
      continue;

    if ((buf[2] & 0x80) == 0) { // standard query
      dns_reply_to_esp_ip(buf, len);
      sendto(sock, buf, 128, 0, (struct sockaddr *)&client, client_len);
    }
  }

  close(sock);
  vTaskDelete(NULL);
}

esp_err_t dns_server_start(void) {
  xTaskCreate(dns_task, "dns", 4096, NULL, 3, &g_dns_task);
  return ESP_OK;
}

esp_err_t dns_server_stop(void) {
  if (g_dns_task) {
    vTaskDelete(g_dns_task);
    g_dns_task = NULL;
  }
  return ESP_OK;
}
