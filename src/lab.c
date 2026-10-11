#include "lab.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Testable application logic shared by the network executable and Unity
 * tests. This module owns CLI configuration, file packetization, checksum
 * calculation, and the sender/receiver protocol state machines.
 */

/* Allocate a private copy of a string for an owning configuration field. */
static char *lab_strdup(const char *value)
{
  size_t length;
  char *copy;

  if (value == NULL) {
    return NULL;
  }
  length = strlen(value);
  copy = malloc(length + 1U);
  if (copy != NULL) {
    memcpy(copy, value, length + 1U);
  }
  return copy;
}

/* Copy a validation error into the caller's optional error buffer. */
static void set_error(char *error, size_t error_size, const char *message)
{
  if (error != NULL && error_size > 0U) {
    (void)snprintf(error, error_size, "%s", message);
  }
}

/* Parse a complete base-10 unsigned integer within the host unsigned range. */
static int parse_unsigned(const char *text, unsigned int *value)
{
  char *end;
  unsigned long parsed;

  if (text == NULL || text[0] == '\0') {
    return 0;
  }
  errno = 0;
  parsed = strtoul(text, &end, 10);
  if (errno != 0 || *end != '\0' || parsed > (unsigned long)UINT_MAX) {
    return 0;
  }
  *value = (unsigned int)parsed;
  return 1;
}

/* Parse a complete packet-damage rate in the inclusive range [0, 0.5]. */
static int parse_rate(const char *text, double *value)
{
  char *end;
  double parsed;

  if (text == NULL || text[0] == '\0') {
    return 0;
  }
  errno = 0;
  parsed = strtod(text, &end);
  if (errno != 0 || *end != '\0' || !isfinite(parsed) || parsed < 0.0 ||
      parsed > LAB_MAX_RATE) {
    return 0;
  }
  *value = parsed;
  return 1;
}

/* Return whether a session name satisfies the relay's character and length rules. */
static int valid_session(const char *session)
{
  size_t i;
  size_t length;

  if (session == NULL) {
    return 0;
  }
  length = strlen(session);
  if (length == 0U || length > 32U) {
    return 0;
  }
  for (i = 0U; i < length; ++i) {
    if (!((session[i] >= 'a' && session[i] <= 'z') ||
          (session[i] >= '0' && session[i] <= '9') || session[i] == '-')) {
      return 0;
    }
  }
  return 1;
}

/*
 * Parse send/recv command-line arguments into an owning LabConfig.
 * Returns nonzero on success; on failure, writes an error when possible and
 * releases any configuration fields allocated before the failure.
 */
int lab_parse_args(int argc, char *argv[], LabConfig *config,
                   char *error, size_t error_size)
{
  int index;
  const char *mode;
  const char *positional[2];
  size_t positional_count = 0U;

  if (config == NULL || argv == NULL || argc < 2) {
    set_error(error, error_size, "usage: myapp send|recv ...");
    return 0;
  }
  memset(config, 0, sizeof(*config));
  config->window = LAB_DEFAULT_WINDOW;
  config->timeout_ms = LAB_DEFAULT_TIMEOUT_MS;
  config->port = LAB_DEFAULT_PORT;
  mode = argv[1];
  if (strcmp(mode, "send") == 0) {
    config->mode = LAB_MODE_SEND;
  } else if (strcmp(mode, "recv") == 0) {
    config->mode = LAB_MODE_RECV;
  } else {
    set_error(error, error_size, "mode must be send or recv");
    return 0;
  }

  for (index = 2; index < argc; ++index) {
    const char *option = argv[index];
    if (option[0] == '-') {
      unsigned int number;
      double rate;
      if (index + 1 >= argc) {
        set_error(error, error_size, "option requires a value");
        lab_config_free(config);
        return 0;
      }
      if (strcmp(option, "-s") == 0) {
        free(config->session);
        config->session = lab_strdup(argv[++index]);
      } else if (strcmp(option, "-w") == 0 &&
                 parse_unsigned(argv[++index], &number) &&
                 number >= 1U && number <= LAB_MAX_WINDOW) {
        config->window = number;
      } else if (strcmp(option, "-T") == 0 &&
                 parse_unsigned(argv[++index], &number) && number > 0U) {
        config->timeout_ms = number;
      } else if ((strcmp(option, "-l") == 0 || strcmp(option, "-c") == 0 ||
                  strcmp(option, "-d") == 0) &&
                 parse_rate(argv[index + 1], &rate)) {
        ++index;
        if (strcmp(option, "-l") == 0) {
          config->loss = rate;
        } else if (strcmp(option, "-c") == 0) {
          config->corrupt = rate;
        } else {
          config->dup = rate;
        }
      } else if (strcmp(option, "-p") == 0 &&
                 parse_unsigned(argv[++index], &number) && number > 0U) {
        config->port = number;
      } else {
        set_error(error, error_size, "invalid option or option value");
        lab_config_free(config);
        return 0;
      }
    } else if (positional_count < 2U) {
      positional[positional_count++] = option;
    } else {
      set_error(error, error_size, "too many positional arguments");
      lab_config_free(config);
      return 0;
    }
  }

  if (positional_count == 2U) {
    config->relay = lab_strdup(positional[0]);
    config->file = lab_strdup(positional[1]);
  }
  if (!valid_session(config->session) || config->relay == NULL ||
      config->file == NULL || (config->mode == LAB_MODE_SEND &&
                               positional_count != 2U) ||
      (config->mode == LAB_MODE_RECV && positional_count != 2U)) {
    set_error(error, error_size,
              "expected -s session followed by relay and file");
    lab_config_free(config);
    return 0;
  }
  return 1;
}

/* Release all strings owned by a configuration and reset it to zero state. */
void lab_config_free(LabConfig *config)
{
  if (config != NULL) {
    free(config->session);
    free(config->relay);
    free(config->file);
    memset(config, 0, sizeof(*config));
  }
}

/*
 * Calculate the RFC 1071 Internet checksum over a byte sequence. Odd-length
 * input is padded with a zero low byte; the result is the 16-bit checksum
 * zero-extended through the uint32_t API.
 */
uint32_t lab_checksum(const unsigned char *data, size_t length)
{
  size_t i = 0U;
  uint32_t sum = 0U;

  while (i < length) {
    uint32_t word = (uint32_t)data[i] << 8U;
    if (i + 1U < length) {
      word |= (uint32_t)data[i + 1U];
    }
    sum += word;
    sum = (sum & 0xffffU) + (sum >> 16U);
    i += 2U;
  }
  sum = (sum & 0xffffU) + (sum >> 16U);
  return (~sum) & 0xffffU;
}

/* Append one independently allocated payload packet to a packet list. */
static int append_packet(LabPacketList *list, const unsigned char *data,
                         size_t length)
{
  LabPacket *packet;
  unsigned char *copy = NULL;

  if (list->count == list->capacity) {
    size_t new_capacity = list->capacity == 0U ? 16U : list->capacity * 2U;
    // GCOVR_EXCL_START
    LabPacket *grown = realloc(list->packets,
                               new_capacity * sizeof(*list->packets));
    if (grown == NULL) {
      return 0;
    }
    // GCOVR_EXCL_STOP
    list->packets = grown;
    list->capacity = new_capacity;
  }
  if (length > 0U) {
    // GCOVR_EXCL_START
    copy = malloc(length);
    if (copy == NULL) {
      return 0;
    }
    // GCOVR_EXCL_STOP
    memcpy(copy, data, length);
  }
  packet = &list->packets[list->count];
  memset(packet, 0, sizeof(*packet));
  packet->sequence = (uint32_t)list->count;
  packet->length = length;
  packet->data = copy;
  ++list->count;
  return 1;
}

/*
 * Read a file in LAB_MAX_PAYLOAD-byte chunks. Every packet owns its payload;
 * an empty file produces one zero-length packet so it can still be transferred.
 */
int lab_read_packets(const char *path, LabPacketList *list)
{
  FILE *file;
  unsigned char buffer[LAB_MAX_PAYLOAD];
  size_t length;

  if (path == NULL || list == NULL) {
    return 0;
  }
  memset(list, 0, sizeof(*list));
  file = fopen(path, "rb");
  if (file == NULL) {
    return 0;
  }
  while ((length = fread(buffer, 1U, sizeof(buffer), file)) > 0U) {
    // GCOVR_EXCL_START
    if (!append_packet(list, buffer, length)) {
      fclose(file);
      lab_packet_list_free(list);
      return 0;
    }
    // GCOVR_EXCL_STOP
  }
  // GCOVR_EXCL_START
  if (ferror(file) != 0) {
    fclose(file);
    lab_packet_list_free(list);
    return 0;
  }
  // GCOVR_EXCL_STOP
  // GCOVR_EXCL_START
  if (list->count == 0U && !append_packet(list, NULL, 0U)) {
    fclose(file);
    lab_packet_list_free(list);
    return 0;
  }
  // GCOVR_EXCL_STOP
  fclose(file);
  return 1;
}

/* Release every packet payload and the list storage, then reset the list. */
void lab_packet_list_free(LabPacketList *list)
{
  size_t i;
  if (list != NULL) {
    for (i = 0U; i < list->count; ++i) {
      free(list->packets[i].data);
    }
    free(list->packets);
    memset(list, 0, sizeof(*list));
  }
}

/* Initialize a sender with an empty Go-Back-N window. */
void lab_sender_init(LabSenderState *state, unsigned int window)
{
  if (state != NULL) {
    state->base = 0U;
    state->next = 0U;
    state->window = window;
  }
}

/* Return nonzero when another packet fits in the current sender window. */
int lab_sender_can_send(const LabSenderState *state, size_t packet_count)
{
  return state != NULL && state->next < packet_count &&
         state->next < state->base + state->window;
}

/*
 * Mark the packet at state->next as sent, start its timer, and advance next.
 * Packets with a different sequence are ignored to preserve sender invariants.
 */
void lab_sender_mark_sent(LabSenderState *state, LabPacket *packet,
                          uint64_t now_ms)
{
  if (state != NULL && packet != NULL && packet->sequence == state->next) {
    packet->sent_at_ms = now_ms;
    packet->timer_active = 1;
    ++state->next;
  }
}

/*
 * Apply a cumulative ACK for one sequence number and advance base across all
 * contiguous acknowledged packets. Outstanding packets retain active timers.
 */
void lab_sender_ack(LabSenderState *state, LabPacket *packets,
                    size_t packet_count, uint32_t sequence)
{
  size_t i;
  if (state == NULL || packets == NULL || sequence >= packet_count ||
      sequence >= state->next) {
    return;
  }
  for (i = 0U; i <= (size_t)sequence; ++i) {
    packets[i].acknowledged = 1;
    packets[i].timer_active = 0;
  }
  while (state->base < packet_count && packets[state->base].acknowledged) {
    ++state->base;
  }
  for (i = state->base; i < packet_count && i < state->next; ++i) {
    if (!packets[i].acknowledged) {
      packets[i].timer_active = 1;
    }
  }
}

/* Return nonzero when the current base packet has exceeded its timeout. */
int lab_sender_timed_out(const LabSenderState *state, const LabPacket *packets,
                         size_t packet_count, uint64_t now_ms,
                         unsigned int timeout_ms)
{
  if (state == NULL || packets == NULL || state->base >= packet_count ||
      !packets[state->base].timer_active || now_ms < packets[state->base].sent_at_ms) {
    return 0;
  }
  return now_ms - packets[state->base].sent_at_ms >= timeout_ms;
}

/*
 * Rewind transmission to base after a timeout and clear timer state for
 * unacknowledged packets so retransmission can restart cleanly.
 */
void lab_sender_reset_to_base(LabSenderState *state, LabPacket *packets,
                              size_t packet_count)
{
  size_t i;
  if (state == NULL || packets == NULL) {
    return;
  }
  state->next = state->base;
  for (i = state->base; i < packet_count; ++i) {
    if (!packets[i].acknowledged) {
      packets[i].timer_active = 0;
    }
  }
}

/* Initialize receiver sequencing at packet zero with no prior ACK. */
void lab_receiver_init(LabReceiverState *state)
{
  if (state != NULL) {
    state->expected = 0U;
    state->last_ack = UINT32_MAX;
  }
}

/*
 * Accept exactly the next sequence number with a valid checksum. Rejected
 * packets do not advance receiver state, allowing the caller to discard them.
 */
int lab_receiver_accept(LabReceiverState *state, uint32_t sequence,
                        const unsigned char *data, size_t length,
                        uint32_t checksum)
{
  if (state == NULL || (length > 0U && data == NULL) ||
      lab_checksum(data, length) != checksum) {
    return 0;
  }
  if (sequence != state->expected) {
    return 0;
  }
  state->last_ack = sequence;
  ++state->expected;
  return 1;
}
