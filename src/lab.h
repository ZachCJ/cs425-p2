#ifndef LAB_H
#define LAB_H

#include <stddef.h>
#include <stdint.h>

#define LAB_DEFAULT_WINDOW 8U
#define LAB_MAX_WINDOW 64U
#define LAB_DEFAULT_TIMEOUT_MS 250U
#define LAB_DEFAULT_PORT 4250U
#define LAB_MAX_PAYLOAD 1024U
#define LAB_MAX_RATE 0.5

typedef enum {
  LAB_MODE_SEND,
  LAB_MODE_RECV
} LabMode;

typedef struct {
  LabMode mode;
  char *session;
  char *relay;
  char *file;
  unsigned int window;
  unsigned int timeout_ms;
  double loss;
  double corrupt;
  double dup;
  unsigned int port;
} LabConfig;

typedef struct {
  uint32_t sequence;
  size_t length;
  unsigned char *data;
  int acknowledged;
  int timer_active;
  uint64_t sent_at_ms;
} LabPacket;

typedef struct {
  LabPacket *packets;
  size_t count;
  size_t capacity;
} LabPacketList;

typedef struct {
  size_t base;
  size_t next;
  unsigned int window;
} LabSenderState;

typedef struct {
  uint32_t expected;
  uint32_t last_ack;
} LabReceiverState;

int lab_parse_args(int argc, char *argv[], LabConfig *config,
                   char *error, size_t error_size);
void lab_config_free(LabConfig *config);

int lab_read_packets(const char *path, LabPacketList *list);
void lab_packet_list_free(LabPacketList *list);
uint32_t lab_checksum(const unsigned char *data, size_t length);

void lab_sender_init(LabSenderState *state, unsigned int window);
int lab_sender_can_send(const LabSenderState *state, size_t packet_count);
void lab_sender_mark_sent(LabSenderState *state, LabPacket *packet,
                          uint64_t now_ms);
void lab_sender_ack(LabSenderState *state, LabPacket *packets,
                    size_t packet_count, uint32_t sequence);
int lab_sender_timed_out(const LabSenderState *state, const LabPacket *packets,
                         size_t packet_count, uint64_t now_ms,
                         unsigned int timeout_ms);
void lab_sender_reset_to_base(LabSenderState *state, LabPacket *packets,
                              size_t packet_count);

void lab_receiver_init(LabReceiverState *state);
int lab_receiver_accept(LabReceiverState *state, uint32_t sequence,
                        const unsigned char *data, size_t length,
                        uint32_t checksum);

#endif
