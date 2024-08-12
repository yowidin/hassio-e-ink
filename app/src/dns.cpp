/**
 * @file   dns.cpp
 * @author Dennis Sitelew
 * @date   Aug. 12, 2024
 */

#include <hei/dns.hpp>

#include <autoconf.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hei_dns, CONFIG_APP_LOG_LEVEL);

#if CONFIG_APP_DNS_SERVER
#include <zephyr/kernel.h>
#include <zephyr/net/net_config.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#include <cstdint>
#include <system_error>

namespace {

namespace dns {

// DNS server events
enum event : std::uint32_t {
   start = BIT(0),
   running = BIT(1),
   error = BIT(12),
};

K_EVENT_DEFINE(status)

auto last_error = ATOMIC_INIT(0);

constexpr std::uint16_t port = 53;
constexpr int max_message_len = 512;

struct header {
   std::uint16_t id;
   std::uint16_t flags;
   std::uint16_t qdcount;
   std::uint16_t ancount;
   std::uint16_t nscount;
   std::uint16_t arcount;
};

struct question {
   std::uint16_t qtype;
   std::uint16_t qclass;
};

struct resource {
   std::uint16_t type;
   std::uint16_t rdata_class;
   std::uint32_t ttl;
   std::uint16_t rdlength;
};

} // namespace dns

void dns_server_thread(void *p1, void *p2, void *p3) {
   ARG_UNUSED(p1);
   ARG_UNUSED(p2);
   ARG_UNUSED(p3);

   k_event_wait(&dns::status, dns::event::start, false, K_FOREVER);
   LOG_DBG("Starting DNS server");

   int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (sock < 0) {
      LOG_ERR("Failed to create socket: %d", sock);
      dns::last_error = sock;
      k_event_set(&::dns::status, ::dns::event::error);
      return;
   }

   LOG_DBG("Socket created");
   const sockaddr_in server_addr = {.sin_family = AF_INET,
                                    .sin_port = htons(dns::port),
                                    .sin_addr = {.s_addr = INADDR_ANY}};

   if (auto err = bind(sock, reinterpret_cast<const sockaddr *>(&server_addr), sizeof(server_addr)) < 0) {
      LOG_ERR("Failed to bind to port %" PRIu16 ": %d", dns::port, err);
      dns::last_error = err;
      close(sock);
      k_event_set(&::dns::status, ::dns::event::error);
      return;
   }

   LOG_DBG("Socket bound");
   auto iface = net_if_get_default();
   if (!iface) {
      LOG_ERR("Could not find default interface");
      dns::last_error = ENETDOWN;
      close(sock);
      k_event_set(&::dns::status, ::dns::event::error);
      return;
   }

   const auto server_ip = iface->config.ip.ipv4->unicast[0].ipv4.address.in_addr.s4_addr;

   k_event_set(&::dns::status, ::dns::event::running);

   static std::uint8_t rx_buf[dns::max_message_len];
   static std::uint8_t tx_buf[dns::max_message_len];

   sockaddr_in client_addr{};
   socklen_t client_addr_len = sizeof(client_addr);
   while (true) {
      LOG_DBG("Waiting for a request");
      int received =
         recvfrom(sock, rx_buf, sizeof(rx_buf), 0, reinterpret_cast<sockaddr *>(&client_addr), &client_addr_len);
      if (received < 0) {
         LOG_WRN("Failed to receive: %d", received);
         continue;
      }

      if (received < static_cast<int>(sizeof(dns::header))) {
         LOG_WRN("DNS message too short: %d", received);
         continue;
      }

      LOG_DBG("Received %d bytes", received);

      // Copy the received message to the transmit buffer
      memcpy(tx_buf, rx_buf, received);

      auto header = reinterpret_cast<dns::header *>(tx_buf);
      header->flags = htons(0x8180); // Standard query response, No error
      header->ancount = htons(1);    // One answer

      // Skip the question section
      std::uint8_t *answer_ptr = tx_buf + received;

      // Construct the answer section
      *answer_ptr++ = 0xc0; // Pointer to domain name
      *answer_ptr++ = 0x0c; // Offset to domain name (12 bytes from start)

      auto resource = reinterpret_cast<dns::resource *>(answer_ptr);
      resource->type = htons(1);        // A record
      resource->rdata_class = htons(1); // IN class
      resource->ttl = htonl(300);       // 5 minutes TTL
      resource->rdlength = htons(4);    // 4 bytes for IPv4 address
      answer_ptr += sizeof(dns::resource);

      LOG_INF("%d %d %d %d", server_ip[0], server_ip[1], server_ip[2], server_ip[3]);
      *answer_ptr++ = server_ip[0];
      *answer_ptr++ = server_ip[1];
      *answer_ptr++ = server_ip[2];
      *answer_ptr++ = server_ip[3];

      int response_len = answer_ptr - tx_buf;

      LOG_DBG("Sending response");
      sendto(sock, tx_buf, response_len, 0, reinterpret_cast<sockaddr *>(&client_addr), client_addr_len);
   }
}

K_THREAD_DEFINE(dns_server_id,
                CONFIG_APP_DNS_SERVER_STACK_SIZE,
                dns_server_thread,
                NULL,
                NULL,
                NULL,
                CONFIG_APP_DNS_SERVER_THREAD_PRIORITY,
                0,
                0);

} // namespace

#endif

namespace hei::dns::server {

void start() {
#if CONFIG_APP_DNS_SERVER
   k_event_set(&::dns::status, ::dns::event::start);

   auto events = k_event_wait(&::dns::status, ::dns::event::running | ::dns::event::error, false, K_FOREVER);
   if ((events & ::dns::event::running) != ::dns::event::running) {
      LOG_ERR("Error starting DNS server");
      throw std::system_error{std::make_error_code(std::errc{::dns::last_error})};
   }

   LOG_INF("DNS server started");
#else
   // Probably because the DHCPv4 server doesn't allow us to set a DNS server, so no captive portal for us :'(
   LOG_ERR("DNS server unsupported");
#endif
}

} // namespace hei::dns::server
