#include "lab.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/*
 * Network-facing UDP implementation. The wire format is deliberately
 * serialized field-by-field rather than by copying C structs:
 *
 *   type (1), reserved (1), sequence (4), payload length (2),
 *   checksum (2), payload (0..LAB_MAX_PAYLOAD)
 *
 * Multi-byte fields are stored in network byte order through explicit byte
 * offsets, so host alignment and padding do not affect interoperability.
 */

#ifdef TEST
#define main lab_network_main
#endif

#define WIRE_DATA 0U
#define WIRE_ACK 1U
#define WIRE_FIN 2U
#define WIRE_HEADER_SIZE 10U
#define WIRE_TYPE_OFFSET 0U
#define WIRE_RESERVED_OFFSET 1U
#define WIRE_SEQUENCE_OFFSET 2U
#define WIRE_LENGTH_OFFSET 6U
#define WIRE_CHECKSUM_OFFSET 8U

/* Return the current wall-clock time in milliseconds for sender timers. */
static uint64_t now_ms(void)
{
  struct timeval time_value;
  (void)gettimeofday(&time_value, NULL);
  return (uint64_t)time_value.tv_sec * 1000U +
         (uint64_t)time_value.tv_usec / 1000U;
}

/* Return a monotonic millisecond timestamp for measuring transfer duration. */
static uint64_t monotonic_ms(void)
{
  struct timespec time_value;
  (void)clock_gettime(CLOCK_MONOTONIC, &time_value);
  return (uint64_t)time_value.tv_sec * 1000U +
         (uint64_t)time_value.tv_nsec / 1000000U;
}

/* Resolve a relay host and create a UDP socket for the first usable address. */
static int open_relay(const char *host, unsigned int port,
                      struct sockaddr_storage *address, socklen_t *address_len)
{
  char service[16];
  struct addrinfo hints;
  struct addrinfo *results;
  struct addrinfo *entry;
  int socket_fd = -1;

  (void)snprintf(service, sizeof(service), "%u", port);
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  if (getaddrinfo(host, service, &hints, &results) != 0) {
    return -1;
  }
  for (entry = results; entry != NULL; entry = entry->ai_next) {
    socket_fd = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
    if (socket_fd >= 0) {
      memcpy(address, entry->ai_addr, entry->ai_addrlen);
      *address_len = (socklen_t)entry->ai_addrlen;
      break;
    }
  }
  freeaddrinfo(results);
  return socket_fd;
}

/*
 * Register this process with the relay and retry lost HELLO responses.
 * Registration succeeds only when the relay responds with an "OK" message.
 */
static int send_hello(int socket_fd, const struct sockaddr_storage *relay,
                      socklen_t relay_len, const LabConfig *config)
{
  char hello[160];
  int length;
  int attempt;

  if (config->mode == LAB_MODE_SEND) {
    length = snprintf(hello, sizeof(hello), "HELLO %s send %g %g %g",
                      config->session, config->loss, config->corrupt, config->dup);
    fprintf(stderr, "sender: %s\n", hello);
  } else {
    length = snprintf(hello, sizeof(hello), "HELLO %s recv", config->session);
    fprintf(stderr, "reciever: %s\n", hello);
  }
  if (length < 0 || (size_t)length >= sizeof(hello)) {
    return 0;
  }
  for (attempt = 0; attempt < 5; ++attempt) {
    char response[64];
    ssize_t received;
    fd_set read_set;
    struct timeval timeout = {1, 0};
    if (sendto(socket_fd, hello, (size_t)length, 0,
               (const struct sockaddr *)relay, relay_len) < 0) {
      return 0;
    }
    FD_ZERO(&read_set);
    FD_SET(socket_fd, &read_set);
    if (select(socket_fd + 1, &read_set, NULL, NULL, &timeout) <= 0) {
      continue;
    }
    received = recvfrom(socket_fd, response, sizeof(response) - 1U, 0, NULL, NULL);
    if (received > 0) {
      response[received] = '\0';
      if (strncmp(response, "OK", 2U) == 0) {
        return 1;
      }
      fprintf(stderr, "relay rejected HELLO: %s\n", response);
      return 0;
    }
  }
  fprintf(stderr, "relay did not respond to HELLO\n");
  return 0;
}

/* Serialize a 32-bit unsigned field in network byte order at a fixed offset. */
static void write_wire_u32(unsigned char *wire, size_t offset, uint32_t value)
{
  wire[offset] = (unsigned char)((value >> 24U) & 0xffU);
  wire[offset + 1U] = (unsigned char)((value >> 16U) & 0xffU);
  wire[offset + 2U] = (unsigned char)((value >> 8U) & 0xffU);
  wire[offset + 3U] = (unsigned char)(value & 0xffU);
}

/* Read a 32-bit network-order field from a fixed packet offset. */
static uint32_t read_wire_u32(const unsigned char *wire, size_t offset)
{
  return ((uint32_t)wire[offset] << 24U) |
         ((uint32_t)wire[offset + 1U] << 16U) |
         ((uint32_t)wire[offset + 2U] << 8U) |
         (uint32_t)wire[offset + 3U];
}

/* Serialize a 16-bit unsigned field in network byte order at a fixed offset. */
static void write_wire_u16(unsigned char *wire, size_t offset, uint16_t value)
{
  wire[offset] = (unsigned char)((value >> 8U) & 0xffU);
  wire[offset + 1U] = (unsigned char)(value & 0xffU);
}

/* Read a 16-bit network-order field from a fixed packet offset. */
static uint16_t read_wire_u16(const unsigned char *wire, size_t offset)
{
  return (uint16_t)(((uint16_t)wire[offset] << 8U) |
                    (uint16_t)wire[offset + 1U]);
}

/* Write the one-byte packet type or reserved field. */
static void write_wire_u8(unsigned char *wire, size_t offset, unsigned int value)
{
  wire[offset] = (unsigned char)value;
}

/* Read a one-byte packet field from a fixed packet offset. */
static unsigned int read_wire_u8(const unsigned char *wire, size_t offset)
{
  return (unsigned int)wire[offset];
}

/*
 * Encode a DATA, ACK, or FIN packet into the fixed wire layout. Returns the
 * complete datagram length, or zero when the buffer or payload is invalid.
 */
static size_t encode_packet(unsigned char *wire, size_t wire_size,
                            unsigned int type, uint32_t sequence,
                            const unsigned char *data, size_t length)
{
  if (length > LAB_MAX_PAYLOAD || wire_size < WIRE_HEADER_SIZE + length) {
    return 0U;
  }
  write_wire_u8(wire, WIRE_TYPE_OFFSET, type);
  write_wire_u8(wire, WIRE_RESERVED_OFFSET, 0U);
  write_wire_u32(wire, WIRE_SEQUENCE_OFFSET, sequence);
  write_wire_u16(wire, WIRE_LENGTH_OFFSET, (uint16_t)length);
  write_wire_u16(wire, WIRE_CHECKSUM_OFFSET,
                 (uint16_t)lab_checksum(data, length));
  if (length > 0U) {
    memcpy(wire + WIRE_HEADER_SIZE, data, length);
  }
  return WIRE_HEADER_SIZE + length;
}

/*
 * Decode and structurally validate a datagram. The returned data pointer
 * aliases the input buffer and is valid only while that buffer is unchanged.
 */
static int decode_packet(const unsigned char *wire, size_t length,
                         unsigned int *type, uint32_t *sequence,
                         uint32_t *checksum, const unsigned char **data,
                         size_t *data_length)
{
  if (length < WIRE_HEADER_SIZE) {
    return 0;
  }
  *type = read_wire_u8(wire, WIRE_TYPE_OFFSET);
  if (read_wire_u8(wire, WIRE_RESERVED_OFFSET) != 0U) {
    return 0;
  }
  *sequence = read_wire_u32(wire, WIRE_SEQUENCE_OFFSET);
  *data_length = (size_t)read_wire_u16(wire, WIRE_LENGTH_OFFSET);
  *checksum = (uint32_t)read_wire_u16(wire, WIRE_CHECKSUM_OFFSET);
  if (*type > WIRE_FIN || *data_length > LAB_MAX_PAYLOAD ||
      WIRE_HEADER_SIZE + *data_length != length) {
    return 0;
  }
  *data = wire + WIRE_HEADER_SIZE;
  return 1;
}

/*
 * Send all file packets using Go-Back-N, process cumulative ACKs, rewind to
 * the base packet after timeout, then retransmit FIN until its ACK arrives.
 * Transfer timing begins when the first DATA packet leaves the sender.
 */
static int run_send(const LabConfig *config, int socket_fd,
                    const struct sockaddr_storage *relay, socklen_t relay_len)
{
  LabPacketList list;
  LabSenderState state;
  unsigned char wire[WIRE_HEADER_SIZE + LAB_MAX_PAYLOAD];
  unsigned char received[256];
  uint64_t transfer_start_ms = 0U;
  uint64_t ack_latency_total_ms = 0U;
  size_t acked_packet_count = 0U;
  size_t total_bytes = 0U;

  if (!lab_read_packets(config->file, &list)) {
    fprintf(stderr, "unable to read input file\n");
    return 0;
  }
  fprintf(stderr,
          "sender session=%s starting sending file=%s packets=%zu window=%u\n",
          config->session, config->file, list.count, config->window);
  for (size_t index = 0U; index < list.count; ++index) {
    total_bytes += list.packets[index].length;
  }
  lab_sender_init(&state, config->window);
  while (state.base < list.count) {
    while (lab_sender_can_send(&state, list.count)) {
      LabPacket *packet = &list.packets[state.next];
      size_t length = encode_packet(wire, sizeof(wire), WIRE_DATA,
                                    packet->sequence, packet->data, packet->length);
      if (transfer_start_ms == 0U) {
        transfer_start_ms = monotonic_ms();
      }
      if (length == 0U) {
        lab_packet_list_free(&list);
        return 0;
      }
      if (sendto(socket_fd, wire, length, 0,
                 (const struct sockaddr *)relay, relay_len) < 0) {
        lab_packet_list_free(&list);
        return 0;
      }
      lab_sender_mark_sent(&state, packet, now_ms());
    }
    {
      fd_set read_set;
      struct timeval timeout;
      uint64_t current = now_ms();
      uint64_t wait_ms = config->timeout_ms;
      if (state.base < list.count && list.packets[state.base].timer_active &&
          current - list.packets[state.base].sent_at_ms < wait_ms) {
        wait_ms -= current - list.packets[state.base].sent_at_ms;
      }
      timeout.tv_sec = (time_t)(wait_ms / 1000U);
      timeout.tv_usec = (suseconds_t)((wait_ms % 1000U) * 1000U);
      FD_ZERO(&read_set);
      FD_SET(socket_fd, &read_set);
      if (select(socket_fd + 1, &read_set, NULL, NULL, &timeout) > 0) {
        ssize_t received_length = recvfrom(socket_fd, received, sizeof(received),
                                           0, NULL, NULL);
        unsigned int type;
        uint32_t sequence;
        uint32_t checksum;
        const unsigned char *data;
        size_t data_length;
        if (received_length > 0 &&
            decode_packet(received, (size_t)received_length, &type, &sequence,
                          &checksum, &data, &data_length) &&
            type == WIRE_ACK) {
          uint64_t ack_received_ms = now_ms();
          if ((size_t)sequence < list.count &&
              !list.packets[sequence].acknowledged &&
              list.packets[sequence].timer_active &&
              ack_received_ms >= list.packets[sequence].sent_at_ms) {
            ack_latency_total_ms +=
                ack_received_ms - list.packets[sequence].sent_at_ms;
            ++acked_packet_count;
          }
          lab_sender_ack(&state, list.packets, list.count, sequence);
        }
      }
    }
    if (lab_sender_timed_out(&state, list.packets, list.count, now_ms(),
                             config->timeout_ms)) {
      lab_sender_reset_to_base(&state, list.packets, list.count);
    }
  }
  {
    size_t end_length = encode_packet(wire, sizeof(wire), WIRE_FIN,
                                      (uint32_t)list.count, NULL, 0U);
    int end_acknowledged = 0;
    if (end_length == 0U) {
      lab_packet_list_free(&list);
      return 0;
    }
    while (!end_acknowledged) {
      fd_set read_set;
      struct timeval timeout = {(time_t)(config->timeout_ms / 1000U),
                                (suseconds_t)((config->timeout_ms % 1000U) * 1000U)};
      unsigned char response[256];
      if (transfer_start_ms == 0U) {
        transfer_start_ms = monotonic_ms();
      }
      if (sendto(socket_fd, wire, end_length, 0,
                 (const struct sockaddr *)relay, relay_len) < 0) {
        lab_packet_list_free(&list);
        return 0;
      }
      FD_ZERO(&read_set);
      FD_SET(socket_fd, &read_set);
      if (select(socket_fd + 1, &read_set, NULL, NULL, &timeout) > 0) {
        ssize_t response_length = recvfrom(socket_fd, response, sizeof(response),
                                           0, NULL, NULL);
        unsigned int type;
        uint32_t sequence;
        uint32_t checksum;
        const unsigned char *data;
        size_t data_length;
        if (response_length > 0 &&
            decode_packet(response, (size_t)response_length, &type, &sequence,
                          &checksum, &data, &data_length) &&
            type == WIRE_ACK && sequence == (uint32_t)list.count) {
          end_acknowledged = 1;
        }
      }
    }
  }
  lab_packet_list_free(&list);
  printf("sender: %s sent\n", config->file);
  {
    uint64_t transfer_time_ms = monotonic_ms() - transfer_start_ms;
    fprintf(stderr,
            "sender session=%s transfer time=%llu ms (%.3f s)\n",
            config->session, (unsigned long long)transfer_time_ms,
            (double)transfer_time_ms / 1000.0);
    fprintf(stderr,
            "sender session=%s mean DATA send-to-ACK time=%.3f ms "
            "(%zu packets), throughput=%.3f KiB/s\n",
            config->session,
            acked_packet_count == 0U
                ? 0.0
                : (double)ack_latency_total_ms / (double)acked_packet_count,
            acked_packet_count,
            transfer_time_ms == 0U
                ? 0.0
                : ((double)total_bytes * 1000.0) /
                      ((double)transfer_time_ms * 1024.0));
  }
  fprintf(stderr, "sender session=%s stopped sending\n", config->session);
  return 1;
}

/*
 * Keep the output file open while receiving. Start transfer timing when the
 * first DATA packet arrives, accept and write only valid in-order DATA
 * packets, and close after valid FIN.
 */
static int run_recv(const LabConfig *config, int socket_fd,
                    const struct sockaddr_storage *relay, socklen_t relay_len)
{
  FILE *file = fopen(config->file, "wb");
  LabReceiverState state;
  unsigned char wire[WIRE_HEADER_SIZE + LAB_MAX_PAYLOAD];
  unsigned char reply[WIRE_HEADER_SIZE];
  uint64_t transfer_start_ms = 0U;
  if (file == NULL) {
    fprintf(stderr, "unable to open output file\n");
    return 0;
  }
  lab_receiver_init(&state);
  for (;;) {
    ssize_t received_length = recvfrom(socket_fd, wire, sizeof(wire), 0, NULL, NULL);
    unsigned int type;
    uint32_t sequence;
    uint32_t checksum;
    const unsigned char *data;
    size_t data_length;
    size_t reply_length;
    if (received_length <= 0 ||
        !decode_packet(wire, (size_t)received_length, &type, &sequence,
                       &checksum, &data, &data_length)) {
      continue;
    }
    if (type == WIRE_DATA && transfer_start_ms == 0U) {
      transfer_start_ms = monotonic_ms();
    }
    if (type == WIRE_FIN) {
      if (data_length == 0U && sequence == state.expected &&
          checksum == lab_checksum(data, data_length)) {
        reply_length = encode_packet(reply, sizeof(reply), WIRE_ACK, sequence,
                                     NULL, 0U);
        if (reply_length == 0U || sendto(socket_fd, reply, reply_length, 0,
                                         (const struct sockaddr *)relay,
                                         relay_len) < 0) {
          fclose(file);
          return 0;
        }
        {
          if (transfer_start_ms == 0U) {
            transfer_start_ms = monotonic_ms();
          }
          uint64_t transfer_time_ms = monotonic_ms() - transfer_start_ms;
          fprintf(stderr,
                  "receiver session=%s received FIN; transfer time=%llu ms (%.3f s)\n",
                  config->session, (unsigned long long)transfer_time_ms,
                  (double)transfer_time_ms / 1000.0);
        }
        break;
      }
      continue;
    }
    if (type != WIRE_DATA) {
      continue;
    }
    if (checksum != lab_checksum(data, data_length)) {
      reply_length = encode_packet(reply, sizeof(reply), WIRE_ACK,
                                   state.last_ack, NULL, 0U);
      if (reply_length == 0U ||
          sendto(socket_fd, reply, reply_length, 0,
                 (const struct sockaddr *)relay, relay_len) < 0) {
        fclose(file);
        return 0;
      }
      continue;
    }
    if (!lab_receiver_accept(&state, sequence, data, data_length, checksum)) {
      reply_length = encode_packet(reply, sizeof(reply), WIRE_ACK,
                                   state.last_ack, NULL, 0U);
      if (reply_length == 0U ||
          sendto(socket_fd, reply, reply_length, 0,
                 (const struct sockaddr *)relay, relay_len) < 0) {
        fclose(file);
        return 0;
      }
      continue;
    }
    if (data_length > 0U && fwrite(data, 1U, data_length, file) != data_length) {
      fclose(file);
      return 0;
    }
    reply_length = encode_packet(reply, sizeof(reply), WIRE_ACK, state.last_ack,
                                 NULL, 0U);
    if (reply_length == 0U || sendto(socket_fd, reply, reply_length, 0,
                                     (const struct sockaddr *)relay, relay_len) < 0) {
      fclose(file);
      return 0;
    }
  }
  fclose(file);
  printf("reciever: %s received\n", config->file);
  fprintf(stderr, "receiver session=%s stopped listening\n", config->session);
  return 1;
}

/* Parse configuration, register with the relay, run the selected mode, and clean up. */
int main(int argc, char *argv[])
{
  LabConfig config;
  char error[128];
  struct sockaddr_storage relay;
  socklen_t relay_len = 0;
  int socket_fd;
  int result;

  if (!lab_parse_args(argc, argv, &config, error, sizeof(error))) {
    fprintf(stderr, "%s\n", error);
    return EXIT_FAILURE;
  }
  socket_fd = open_relay(config.relay, config.port, &relay, &relay_len);
  if (socket_fd < 0 || !send_hello(socket_fd, &relay, relay_len, &config)) {
    fprintf(stderr, "unable to connect to relay\n");
    if (socket_fd >= 0) {
      close(socket_fd);
    }
    lab_config_free(&config);
    return EXIT_FAILURE;
  }
  result = config.mode == LAB_MODE_SEND
               ? run_send(&config, socket_fd, &relay, relay_len)
               : run_recv(&config, socket_fd, &relay, relay_len);
  close(socket_fd);
  lab_config_free(&config);
  return result ? EXIT_SUCCESS : EXIT_FAILURE;
}
