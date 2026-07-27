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

static const char *TAG = "dns";
static TaskHandle_t g_dns_task = NULL;

static void dns_reply_a(uint8_t *buf, size_t *len) {
  // Build a proper DNS response with one A-record answer.
  // We reuse buf for the response (it's large enough at 512 bytes)
  uint8_t resp[DNS_MAX_QUERY];
  size_t rlen = 0;

  // Header: copy ID, set QR=1 OPCODE=0 AA=1, set RA=1, RCODE=0
  resp[0] = buf[0];
  resp[1] = buf[1]; // ID
  resp[2] = 0x85;   // QR|AA
  resp[3] = 0x80;   // RA
  // QDCOUNT=1, ANCOUNT=1, NSCOUNT=0, ARCOUNT=0
  resp[4] = 0x00;
  resp[5] = 0x01;
  resp[6] = 0x00;
  resp[7] = 0x01;
  resp[8] = 0x00;
  resp[9] = 0x00;
  resp[10] = 0x00;
  resp[11] = 0x00;
  rlen = 12;

  // Copy the question section verbatim
  size_t qlen = 12;
  while (qlen < *len) {
    uint8_t c = buf[qlen];
    if (c == 0) {
      qlen += 5; // null label + QTYPE(2) + QCLASS(2)
      break;
    }
    if ((c & 0xC0) == 0xC0) {
      qlen += 4; // compression ptr(2) + QTYPE(2) + QCLASS(2)
      break;
    }
    qlen += 1 + c;
  }
  size_t question_len = qlen - 12;
  memcpy(resp + rlen, buf + 12, question_len);
  rlen += question_len;

  // Answer: name compression pointer to question
  resp[rlen++] = 0xC0;
  resp[rlen++] = 0x0C;
  // Type A
  resp[rlen++] = 0x00;
  resp[rlen++] = 0x01;
  // Class IN
  resp[rlen++] = 0x00;
  resp[rlen++] = 0x01;
  // TTL 60 seconds
  resp[rlen++] = 0x00;
  resp[rlen++] = 0x00;
  resp[rlen++] = 0x00;
  resp[rlen++] = 0x3C;
  // Data length 4 bytes
  resp[rlen++] = 0x00;
  resp[rlen++] = 0x04;
  // IP 192.168.4.1
  resp[rlen++] = 192;
  resp[rlen++] = 168;
  resp[rlen++] = 4;
  resp[rlen++] = 1;

  memcpy(buf, resp, rlen);
  *len = rlen;
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

  int opt = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    ESP_LOGE(TAG, "bind failed — port 53 already in use?");
    close(sock);
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG, "DNS server listening on port 53");

  uint8_t buf[DNS_MAX_QUERY];
  struct sockaddr_in client;
  socklen_t client_len = sizeof(client);

  while (1) {
    int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&client,
                       &client_len);
    if (len <= 0)
      continue;

    // Only respond to standard queries (QR=0)
    if ((buf[2] & 0x80) == 0) {
      dns_reply_a(buf, &len);
      sendto(sock, buf, len, 0, (struct sockaddr *)&client, client_len);
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
