#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/unity.h"
#include "../src/lab.h"

static const char *const test_input_path = "tests/lab-test-input.bin";

void setUp(void)
{
}

void tearDown(void)
{
  (void)remove(test_input_path);
}

void test_lab_parse_null_values(void)
{
  char *null_session[] = {"myapp", "send", "-s", NULL, "relay", "file"};
  char *null_window[] = {"myapp", "send", "-s", "valid", "-w", NULL,
                         "relay", "file"};
  char *null_rate[] = {"myapp", "send", "-s", "valid", "-l", NULL,
                       "relay", "file"};
  LabConfig config;
  char error[128] = {0};

  TEST_ASSERT_FALSE(lab_parse_args(6, null_session, &config, error,
                                   sizeof(error)));
  TEST_ASSERT_FALSE(lab_parse_args(8, null_window, &config, error,
                                   sizeof(error)));
  TEST_ASSERT_FALSE(lab_parse_args(8, null_rate, &config, error,
                                   sizeof(error)));
}

void test_lab_parse_args_send_with_options(void)
{
  char *argv[] = {
      "myapp", "send", "-s", "session-1", "-w", "16", "-T", "500",
      "-l", "0.1", "-c", "0.2", "-d", "0.3", "-p", "5000",
      "relay.example", "input.bin"};
  LabConfig config;
  char error[128] = {0};

  TEST_ASSERT_TRUE(lab_parse_args((int)(sizeof(argv) / sizeof(argv[0])), argv,
                                  &config, error, sizeof(error)));
  TEST_ASSERT_EQUAL_INT(LAB_MODE_SEND, config.mode);
  TEST_ASSERT_EQUAL_STRING("session-1", config.session);
  TEST_ASSERT_EQUAL_STRING("relay.example", config.relay);
  TEST_ASSERT_EQUAL_STRING("input.bin", config.file);
  TEST_ASSERT_EQUAL_UINT(16U, config.window);
  TEST_ASSERT_EQUAL_UINT(500U, config.timeout_ms);
  TEST_ASSERT_TRUE(config.loss == 0.1);
  TEST_ASSERT_TRUE(config.corrupt == 0.2);
  TEST_ASSERT_TRUE(config.dup == 0.3);
  TEST_ASSERT_EQUAL_UINT(5000U, config.port);
  lab_config_free(&config);
}

void test_lab_parse_args_send_with_window_one(void)
{
  char *argv[] = {
      "myapp", "send", "-s", "session-1", "-w", "1",
      "relay.example", "input.bin"};
  LabConfig config;
  char error[128] = {0};

  TEST_ASSERT_TRUE(lab_parse_args((int)(sizeof(argv) / sizeof(argv[0])), argv,
                                  &config, error, sizeof(error)));
  TEST_ASSERT_EQUAL_UINT(1U, config.window);
  lab_config_free(&config);
}

void test_lab_parse_args_recv_defaults(void)
{
  char *argv[] = {"myapp", "recv", "-s", "abc-123", "localhost", "output.bin"};
  LabConfig config;
  char error[128] = {0};

  TEST_ASSERT_TRUE(lab_parse_args((int)(sizeof(argv) / sizeof(argv[0])), argv,
                                  &config, error, sizeof(error)));
  TEST_ASSERT_EQUAL_INT(LAB_MODE_RECV, config.mode);
  TEST_ASSERT_EQUAL_UINT(LAB_DEFAULT_WINDOW, config.window);
  TEST_ASSERT_EQUAL_UINT(LAB_DEFAULT_TIMEOUT_MS, config.timeout_ms);
  TEST_ASSERT_TRUE(config.loss == 0.0);
  TEST_ASSERT_TRUE(config.corrupt == 0.0);
  TEST_ASSERT_TRUE(config.dup == 0.0);
  TEST_ASSERT_EQUAL_UINT(LAB_DEFAULT_PORT, config.port);
  lab_config_free(&config);
}

void test_lab_parse_args_rejects_invalid_inputs(void)
{
  char *valid_tail[] = {"relay", "file"};
  char *argv[] = {"myapp", "send", "-s", "ok"};
  char error[128];
  LabConfig config;
  size_t i;

  TEST_ASSERT_FALSE(lab_parse_args(0, argv, &config, error, sizeof(error)));
  TEST_ASSERT_EQUAL_STRING("usage: myapp send|recv ...", error);

  {
    char *bad[] = {"myapp", "invalid"};
    TEST_ASSERT_FALSE(lab_parse_args(2, bad, &config, error, sizeof(error)));
    TEST_ASSERT_EQUAL_STRING("mode must be send or recv", error);
  }

  {
    char *bad[] = {"myapp", "send", "-s", "UPPER", valid_tail[0], valid_tail[1]};
    TEST_ASSERT_FALSE(lab_parse_args(6, bad, &config, error, sizeof(error)));
  }

  {
    char *bad[] = {"myapp", "send", "-s", "", valid_tail[0], valid_tail[1]};
    TEST_ASSERT_FALSE(lab_parse_args(6, bad, &config, error, sizeof(error)));
  }

  {
    char long_session[34];
    for (i = 0U; i < 33U; ++i) {
      long_session[i] = 'a';
    }
    long_session[33] = '\0';
    char *bad[] = {"myapp", "send", "-s", long_session, valid_tail[0],
                   valid_tail[1]};
    TEST_ASSERT_FALSE(lab_parse_args(6, bad, &config, error, sizeof(error)));
  }

  {
    char *bad[] = {"myapp", "send", "-s", "ok", "-w", "0", valid_tail[0],
                   valid_tail[1]};
    TEST_ASSERT_FALSE(lab_parse_args(8, bad, &config, error, sizeof(error)));
    TEST_ASSERT_EQUAL_STRING("invalid option or option value", error);
  }

  {
    char *bad[] = {"myapp", "send", "-s", "ok", "-w", "65", valid_tail[0],
                   valid_tail[1]};
    TEST_ASSERT_FALSE(lab_parse_args(8, bad, &config, error, sizeof(error)));
  }

  {
    char *bad[] = {"myapp", "send", "-s", "ok", "-T", "0", valid_tail[0],
                   valid_tail[1]};
    TEST_ASSERT_FALSE(lab_parse_args(8, bad, &config, error, sizeof(error)));
  }

  {
    char *bad[] = {"myapp", "send", "-s", "ok", "-l", "0.6", valid_tail[0],
                   valid_tail[1]};
    TEST_ASSERT_FALSE(lab_parse_args(8, bad, &config, error, sizeof(error)));
  }

  {
    char *bad[] = {"myapp", "send", "-s", "ok", "-p", "0", valid_tail[0],
                   valid_tail[1]};
    TEST_ASSERT_FALSE(lab_parse_args(8, bad, &config, error, sizeof(error)));
  }

  {
    char *bad[] = {"myapp", "send", "-s", "ok", "-w"};
    TEST_ASSERT_FALSE(lab_parse_args(5, bad, &config, error, sizeof(error)));
    TEST_ASSERT_EQUAL_STRING("option requires a value", error);
  }

  {
    char *bad[] = {"myapp", "send", "-s", "ok", "-x", "1", valid_tail[0],
                   valid_tail[1]};
    TEST_ASSERT_FALSE(lab_parse_args(8, bad, &config, error, sizeof(error)));
  }

  {
    char *bad[] = {"myapp", "send", "-s", "ok", valid_tail[0], valid_tail[1],
                   "extra"};
    TEST_ASSERT_FALSE(lab_parse_args(7, bad, &config, error, sizeof(error)));
    TEST_ASSERT_EQUAL_STRING("too many positional arguments", error);
  }

  {
    char *bad[] = {"myapp", "send", "-s", "ok", valid_tail[0]};
    TEST_ASSERT_FALSE(lab_parse_args(5, bad, &config, error, sizeof(error)));
  }

  TEST_ASSERT_FALSE(lab_parse_args(2, NULL, &config, error, sizeof(error)));
  TEST_ASSERT_FALSE(lab_parse_args(2, argv, NULL, error, sizeof(error)));
  lab_config_free(NULL);
}

void test_lab_parse_args_boundary_values(void)
{
  char session[33];
  char *argv[] = {"myapp", "send", "-s", session, "-w", "64", "-T", "1",
                  "-l", "0.5", "-c", "0", "-d", "0.5", "-p", "65535",
                  "relay", "file"};
  LabConfig config;
  char error[128] = {0};
  size_t i;

  for (i = 0U; i < 32U; ++i) {
    session[i] = (i % 3U == 0U) ? '7' : ((i % 3U == 1U) ? '-' : 'z');
  }
  session[32] = '\0';

  TEST_ASSERT_TRUE(lab_parse_args((int)(sizeof(argv) / sizeof(argv[0])), argv,
                                  &config, error, sizeof(error)));
  TEST_ASSERT_EQUAL_STRING(session, config.session);
  TEST_ASSERT_EQUAL_UINT(LAB_MAX_WINDOW, config.window);
  TEST_ASSERT_EQUAL_UINT(1U, config.timeout_ms);
  TEST_ASSERT_TRUE(config.loss == LAB_MAX_RATE);
  TEST_ASSERT_TRUE(config.corrupt == 0.0);
  TEST_ASSERT_TRUE(config.dup == LAB_MAX_RATE);
  TEST_ASSERT_EQUAL_UINT(65535U, config.port);
  lab_config_free(&config);
}

void test_lab_parse_args_rejects_numeric_overflow_and_bad_rates(void)
{
  char *base[] = {"myapp", "send", "-s", "valid", "relay", "file"};
  char error[128];
  LabConfig config;

  {
    char *bad[] = {"myapp", "send", "-s", "valid", "-w",
                   "42949672960", "relay", "file"};
    TEST_ASSERT_FALSE(lab_parse_args((int)(sizeof(bad) / sizeof(bad[0])), bad,
                                     &config, error, sizeof(error)));
  }
  {
    char *bad[] = {"myapp", "send", "-s", "valid", "-T",
                   "184467440737095516160", "relay", "file"};
    TEST_ASSERT_FALSE(lab_parse_args((int)(sizeof(bad) / sizeof(bad[0])), bad,
                                     &config, error, sizeof(error)));
  }
  {
    char *bad[] = {"myapp", "send", "-s", "valid", "-p",
                   "999999999999999999999999", "relay", "file"};
    TEST_ASSERT_FALSE(lab_parse_args((int)(sizeof(bad) / sizeof(bad[0])), bad,
                                     &config, error, sizeof(error)));
  }
  {
    char *bad[] = {"myapp", "send", "-s", "valid", "-l", "nan",
                   "relay", "file"};
    TEST_ASSERT_FALSE(lab_parse_args((int)(sizeof(bad) / sizeof(bad[0])), bad,
                                     &config, error, sizeof(error)));
  }
  {
    char *bad[] = {"myapp", "send", "-s", "valid", "-c", "inf",
                   "relay", "file"};
    TEST_ASSERT_FALSE(lab_parse_args((int)(sizeof(bad) / sizeof(bad[0])), bad,
                                     &config, error, sizeof(error)));
  }
  {
    char *bad[] = {"myapp", "send", "-s", "valid", "-d", "-0.1",
                   "relay", "file"};
    TEST_ASSERT_FALSE(lab_parse_args((int)(sizeof(bad) / sizeof(bad[0])), bad,
                                     &config, error, sizeof(error)));
  }
  {
    char *bad[] = {"myapp", "send", "-s", "valid", "-l", "0.5001",
                   "relay", "file"};
    TEST_ASSERT_FALSE(lab_parse_args((int)(sizeof(bad) / sizeof(bad[0])), bad,
                                     &config, error, sizeof(error)));
  }
  {
    char *bad[] = {"myapp", "send", "-s", "valid", "-w", "1.5",
                   "relay", "file"};
    TEST_ASSERT_FALSE(lab_parse_args((int)(sizeof(bad) / sizeof(bad[0])), bad,
                                     &config, error, sizeof(error)));
  }
  {
    char *bad[] = {"myapp", "send", "-s", "valid", "-T", "abc",
                   "relay", "file"};
    TEST_ASSERT_FALSE(lab_parse_args((int)(sizeof(bad) / sizeof(bad[0])), bad,
                                     &config, error, sizeof(error)));
  }
  TEST_ASSERT_TRUE(lab_parse_args((int)(sizeof(base) / sizeof(base[0])), base,
                                  &config, error, sizeof(error)));
  lab_config_free(&config);
}

void test_lab_config_free_clears_configuration(void)
{
  LabConfig config = {LAB_MODE_SEND, NULL, NULL, NULL, 1U, 2U, 0.1, 0.2,
                      0.3, 4U};
  config.session = malloc(4U);
  config.relay = malloc(4U);
  config.file = malloc(4U);
  TEST_ASSERT_NOT_NULL(config.session);
  TEST_ASSERT_NOT_NULL(config.relay);
  TEST_ASSERT_NOT_NULL(config.file);

  lab_config_free(&config);

  TEST_ASSERT_NULL(config.session);
  TEST_ASSERT_NULL(config.relay);
  TEST_ASSERT_NULL(config.file);
  TEST_ASSERT_EQUAL_UINT(0U, config.window);
  TEST_ASSERT_EQUAL_UINT(0U, config.port);
}

void test_lab_checksum(void)
{
  const unsigned char data[] = "abc";
  const unsigned char even_data[] = {0x00U, 0x01U};

  TEST_ASSERT_EQUAL_HEX32(0x3b9dU, lab_checksum(data, 3U));
  TEST_ASSERT_EQUAL_HEX32(0xfffeU, lab_checksum(even_data, sizeof(even_data)));
  TEST_ASSERT_EQUAL_HEX32(0xffffU, lab_checksum(NULL, 0U));
}

void test_lab_read_packets_and_free(void)
{
  const unsigned char contents[LAB_MAX_PAYLOAD + 2U] = {
      [0] = 'a', [LAB_MAX_PAYLOAD - 1U] = 'z',
      [LAB_MAX_PAYLOAD] = 'b', [LAB_MAX_PAYLOAD + 1U] = 'c'};
  FILE *file = fopen(test_input_path, "wb");
  LabPacketList list;

  TEST_ASSERT_NOT_NULL(file);
  TEST_ASSERT_EQUAL_size_t(sizeof(contents),
                           fwrite(contents, 1U, sizeof(contents), file));
  TEST_ASSERT_EQUAL_INT(0, fclose(file));

  TEST_ASSERT_TRUE(lab_read_packets(test_input_path, &list));
  TEST_ASSERT_EQUAL_size_t(2U, list.count);
  TEST_ASSERT_EQUAL_size_t(LAB_MAX_PAYLOAD, list.packets[0].length);
  TEST_ASSERT_EQUAL_size_t(2U, list.packets[1].length);
  TEST_ASSERT_EQUAL_UINT32(0U, list.packets[0].sequence);
  TEST_ASSERT_EQUAL_UINT32(1U, list.packets[1].sequence);
  TEST_ASSERT_EQUAL_MEMORY(contents, list.packets[0].data, LAB_MAX_PAYLOAD);
  TEST_ASSERT_EQUAL_MEMORY(contents + LAB_MAX_PAYLOAD, list.packets[1].data, 2U);
  lab_packet_list_free(&list);
  TEST_ASSERT_NULL(list.packets);
  TEST_ASSERT_EQUAL_size_t(0U, list.count);

  TEST_ASSERT_FALSE(lab_read_packets("does-not-exist.bin", &list));
  TEST_ASSERT_FALSE(lab_read_packets(NULL, &list));
  TEST_ASSERT_FALSE(lab_read_packets(test_input_path, NULL));
  lab_packet_list_free(NULL);
}

void test_lab_read_empty_file_creates_empty_packet(void)
{
  FILE *file = fopen(test_input_path, "wb");
  LabPacketList list;

  TEST_ASSERT_NOT_NULL(file);
  TEST_ASSERT_EQUAL_INT(0, fclose(file));
  TEST_ASSERT_TRUE(lab_read_packets(test_input_path, &list));
  TEST_ASSERT_EQUAL_size_t(1U, list.count);
  TEST_ASSERT_EQUAL_UINT32(0U, list.packets[0].sequence);
  TEST_ASSERT_EQUAL_size_t(0U, list.packets[0].length);
  TEST_ASSERT_NULL(list.packets[0].data);
  lab_packet_list_free(&list);
}

void test_sender_initialization_and_window(void)
{
  LabSenderState state;

  lab_sender_init(&state, 2U);
  TEST_ASSERT_EQUAL_size_t(0U, state.base);
  TEST_ASSERT_EQUAL_size_t(0U, state.next);
  TEST_ASSERT_EQUAL_UINT(2U, state.window);
  TEST_ASSERT_TRUE(lab_sender_can_send(&state, 1U));
  TEST_ASSERT_TRUE(lab_sender_can_send(&state, 2U));
  TEST_ASSERT_FALSE(lab_sender_can_send(&state, 0U));
  TEST_ASSERT_TRUE(lab_sender_can_send(&state, 3U));
  state.next = 2U;
  TEST_ASSERT_FALSE(lab_sender_can_send(&state, 3U));
  TEST_ASSERT_FALSE(lab_sender_can_send(NULL, 1U));
  lab_sender_init(NULL, 2U);
}

void test_sender_mark_sent_and_ack(void)
{
  LabPacket packets[3] = {{0U}, {1U}, {2U}};
  LabSenderState state;

  lab_sender_init(&state, 2U);
  lab_sender_mark_sent(&state, &packets[0], 100U);
  lab_sender_mark_sent(&state, &packets[1], 110U);
  TEST_ASSERT_EQUAL_size_t(2U, state.next);
  TEST_ASSERT_TRUE(packets[0].timer_active);
  TEST_ASSERT_TRUE(packets[1].timer_active);
  TEST_ASSERT_EQUAL_UINT64(100U, packets[0].sent_at_ms);

  lab_sender_ack(&state, packets, 3U, 0U);
  TEST_ASSERT_EQUAL_size_t(1U, state.base);
  TEST_ASSERT_FALSE(packets[0].timer_active);
  TEST_ASSERT_TRUE(packets[1].timer_active);

  lab_sender_ack(&state, packets, 3U, 1U);
  TEST_ASSERT_EQUAL_size_t(2U, state.base);
  TEST_ASSERT_FALSE(packets[0].timer_active);
  TEST_ASSERT_TRUE(packets[1].acknowledged);
  TEST_ASSERT_FALSE(packets[1].timer_active);
  TEST_ASSERT_TRUE(packets[0].acknowledged);

  lab_sender_ack(&state, packets, 3U, 3U);
  lab_sender_ack(&state, packets, 3U, 2U);
  TEST_ASSERT_EQUAL_size_t(2U, state.base);
  lab_sender_mark_sent(NULL, &packets[2], 200U);
  lab_sender_mark_sent(&state, NULL, 200U);
  packets[2].sequence = 9U;
  lab_sender_mark_sent(&state, &packets[2], 200U);
  TEST_ASSERT_EQUAL_size_t(2U, state.next);
}

void test_sender_timeout_and_reset(void)
{
  LabPacket packets[3] = {{0U}, {1U}, {2U}};
  LabSenderState state;

  lab_sender_init(&state, 3U);
  lab_sender_mark_sent(&state, &packets[0], 100U);
  lab_sender_mark_sent(&state, &packets[1], 110U);
  lab_sender_mark_sent(&state, &packets[2], 120U);
  TEST_ASSERT_FALSE(lab_sender_timed_out(&state, packets, 3U, 199U, 100U));
  TEST_ASSERT_TRUE(lab_sender_timed_out(&state, packets, 3U, 200U, 100U));
  TEST_ASSERT_FALSE(lab_sender_timed_out(&state, packets, 3U, 99U, 100U));

  packets[1].acknowledged = 1;
  state.base = 0U;
  lab_sender_reset_to_base(&state, packets, 3U);
  TEST_ASSERT_EQUAL_size_t(0U, state.next);
  TEST_ASSERT_FALSE(packets[0].timer_active);
  TEST_ASSERT_TRUE(packets[1].timer_active);
  TEST_ASSERT_FALSE(packets[2].timer_active);

  TEST_ASSERT_FALSE(lab_sender_timed_out(NULL, packets, 3U, 200U, 100U));
  TEST_ASSERT_FALSE(lab_sender_timed_out(&state, NULL, 3U, 200U, 100U));
  state.base = 3U;
  TEST_ASSERT_FALSE(lab_sender_timed_out(&state, packets, 3U, 200U, 100U));
  lab_sender_reset_to_base(NULL, packets, 3U);
  lab_sender_reset_to_base(&state, NULL, 3U);
}

void test_receiver_initialization_and_acceptance(void)
{
  const unsigned char first[] = "first";
  const unsigned char second[] = "second";
  LabReceiverState state;

  lab_receiver_init(&state);
  TEST_ASSERT_EQUAL_UINT32(0U, state.expected);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, state.last_ack);
  TEST_ASSERT_TRUE(lab_receiver_accept(&state, 0U, first, sizeof(first) - 1U,
                                       lab_checksum(first, sizeof(first) - 1U)));
  TEST_ASSERT_EQUAL_UINT32(1U, state.expected);
  TEST_ASSERT_EQUAL_UINT32(0U, state.last_ack);
  TEST_ASSERT_FALSE(lab_receiver_accept(&state, 2U, second, sizeof(second) - 1U,
                                        lab_checksum(second, sizeof(second) - 1U)));
  TEST_ASSERT_EQUAL_UINT32(1U, state.expected);
  TEST_ASSERT_FALSE(lab_receiver_accept(&state, 1U, second, sizeof(second) - 1U,
                                        0U));
  TEST_ASSERT_FALSE(lab_receiver_accept(&state, 1U, NULL, 1U,
                                        lab_checksum(second, sizeof(second) - 1U)));
  TEST_ASSERT_TRUE(lab_receiver_accept(&state, 1U, NULL, 0U,
                                       lab_checksum(NULL, 0U)));
  TEST_ASSERT_EQUAL_UINT32(2U, state.expected);
  lab_receiver_init(NULL);
  TEST_ASSERT_FALSE(lab_receiver_accept(NULL, 0U, NULL, 0U,
                                        lab_checksum(NULL, 0U)));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_lab_parse_args_send_with_options);
  RUN_TEST(test_lab_parse_null_values);
  RUN_TEST(test_lab_parse_args_send_with_window_one);
  RUN_TEST(test_lab_parse_args_recv_defaults);
  RUN_TEST(test_lab_parse_args_rejects_invalid_inputs);
  RUN_TEST(test_lab_parse_args_boundary_values);
  RUN_TEST(test_lab_parse_args_rejects_numeric_overflow_and_bad_rates);
  RUN_TEST(test_lab_config_free_clears_configuration);
  RUN_TEST(test_lab_checksum);
  RUN_TEST(test_lab_read_packets_and_free);
  RUN_TEST(test_lab_read_empty_file_creates_empty_packet);
  RUN_TEST(test_sender_initialization_and_window);
  RUN_TEST(test_sender_mark_sent_and_ack);
  RUN_TEST(test_sender_timeout_and_reset);
  RUN_TEST(test_receiver_initialization_and_acceptance);
  return UNITY_END();
}
