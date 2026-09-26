// Regression tests for GAACE_Core's ringBuffer / charAllocate / commandProcessor.
//
// Runs on real hardware (the actual Arduino Stream/String classes GAACE_Core
// is built on, not a native shim) via:
//   pio test -e adafruit_qt_py_m0 --upload-port <port>
//
// These establish a baseline for EXISTING behavior before any new feature
// (the synchronous command-execute-and-capture-response primitive) touches
// commandProcessor's shared state. Run before that change to confirm they
// pass against unmodified code, and again after to confirm nothing
// regressed.
#include <Arduino.h>
#include <unity.h>
#include <string.h>
#include "ringBuffer.h"
#include "charAllocate.h"
#include "commandProcessor.h"

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// ringBuffer
// ============================================================================

static void feed(ringBuffer &rb, const char *s) {
  for (const char *p = s; *p; p++) rb.put(*p);
}

static void test_ringbuffer_put_get_roundtrip(void) {
  ringBuffer rb(64);
  rb.put('A');
  rb.put('B');
  TEST_ASSERT_EQUAL(2, rb.count());
  TEST_ASSERT_EQUAL('A', rb.get());
  TEST_ASSERT_EQUAL('B', rb.get());
  TEST_ASSERT_TRUE(rb.empty());
}

static void test_ringbuffer_empty_get_returns_sentinel(void) {
  ringBuffer rb(64);
  TEST_ASSERT_EQUAL((char)0xFF, rb.get());
}

static void test_ringbuffer_lines_and_getline(void) {
  ringBuffer rb(64);
  feed(rb, "HELLO\n");
  TEST_ASSERT_EQUAL(1, rb.lines());
  char buf[16];
  int n = rb.getLine(buf, sizeof(buf));
  TEST_ASSERT_EQUAL(5, n);
  TEST_ASSERT_EQUAL_STRING("HELLO", buf);
  TEST_ASSERT_EQUAL(0, rb.lines());
}

static void test_ringbuffer_gettoken_splits_on_delim(void) {
  ringBuffer rb(64);
  feed(rb, "AA,BB,CC\n");
  char tok[16];
  TEST_ASSERT_EQUAL(2, rb.getToken(tok, ','));
  TEST_ASSERT_EQUAL_STRING("AA", tok);
  TEST_ASSERT_EQUAL(2, rb.getToken(tok, ','));
  TEST_ASSERT_EQUAL_STRING("BB", tok);
  TEST_ASSERT_EQUAL(2, rb.getToken(tok, ','));
  TEST_ASSERT_EQUAL_STRING("CC", tok);
}

static void test_ringbuffer_tokenlength_without_complete_line(void) {
  ringBuffer rb(64);
  rb.put('A');
  rb.put('B');  // no delimiter, no EOL yet
  TEST_ASSERT_EQUAL(-1, rb.tokenLength(','));
}

static void test_ringbuffer_ignore_chars_are_dropped(void) {
  ringBuffer rb(64);
  rb.Ignore = "\r";
  feed(rb, "A\rB\r\n");
  char buf[16];
  int n = rb.getLine(buf, sizeof(buf));
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_EQUAL_STRING("AB", buf);
}

// ============================================================================
// charAllocate
// ============================================================================

static void test_charallocate_alloc_free_and_defrag_reclaims_space(void) {
  // free() only clears the block's alloc flag -- it does NOT coalesce with
  // neighboring free blocks (that's defrag()'s documented job). So right
  // after free(), available() (the largest single contiguous free block)
  // is smaller than before allocating, even though the freed bytes exist
  // somewhere in the buffer; only after defrag() does it return to `before`.
  charAllocate ca(64);
  int before = ca.available();
  char *p = ca.allocate(10);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_TRUE(ca.available() < before);

  ca.free(p);
  TEST_ASSERT_TRUE(ca.available() < before);  // still fragmented, not yet merged

  ca.defrag();
  TEST_ASSERT_EQUAL(before, ca.available());  // now fully reclaimed
}

static void test_charallocate_alloc_beyond_capacity_fails(void) {
  charAllocate ca(16);
  char *p = ca.allocate(1000);
  TEST_ASSERT_NULL(p);
}

static void test_charallocate_clear_resets_everything(void) {
  charAllocate ca(64);
  int before = ca.available();
  ca.allocate(10);
  ca.allocate(10);
  ca.clear();
  TEST_ASSERT_EQUAL(before, ca.available());
}

// ============================================================================
// commandProcessor
// ============================================================================

// Captures everything written to it (ACK/NAK bytes, printed values) so tests
// can inspect commandProcessor's output without a real PC on the other end.
class MockStream : public Stream {
public:
  uint8_t buf[128];
  size_t len = 0;

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t write(uint8_t b) override {
    if (len < sizeof(buf) - 1) buf[len++] = b;
    buf[len] = 0;
    return 1;
  }
  void reset() { len = 0; buf[0] = 0; }
};

static void feedLine(commandProcessor &cp, const char *s) {
  for (const char *p = s; *p; p++) cp.rb->put(*p);
}

static void test_cmdprocessor_set_and_get_int_with_range(void) {
  commandProcessor cp;
  MockStream ms;
  cp.registerStream(&ms);

  int value = 0;
  int range[2] = {0, 100};
  Command cmds[] = {
    {"?FOO", CMDint, -1, (void *)&value, (void *)range, "test int"},
    {NULL}
  };
  CommandList list = {cmds, NULL};
  cp.registerCommands(&list);

  ms.reset();
  feedLine(cp, "SFOO,42\n");
  TEST_ASSERT_TRUE(cp.processCommands());
  TEST_ASSERT_EQUAL(42, value);
  TEST_ASSERT_EQUAL(0x06, ms.buf[0]);  // ACK

  ms.reset();
  feedLine(cp, "GFOO\n");
  TEST_ASSERT_TRUE(cp.processCommands());
  TEST_ASSERT_EQUAL(0x06, ms.buf[0]);
  TEST_ASSERT_TRUE(strstr((char *)ms.buf, "42") != NULL);
}

static void test_cmdprocessor_set_int_out_of_range_is_nak_and_unchanged(void) {
  commandProcessor cp;
  MockStream ms;
  cp.registerStream(&ms);

  int value = 5;
  int range[2] = {0, 100};
  Command cmds[] = {
    {"?FOO", CMDint, -1, (void *)&value, (void *)range, "test int"},
    {NULL}
  };
  CommandList list = {cmds, NULL};
  cp.registerCommands(&list);

  ms.reset();
  feedLine(cp, "SFOO,150\n");
  cp.processCommands();
  TEST_ASSERT_EQUAL(0x15, ms.buf[0]);  // NAK
  TEST_ASSERT_EQUAL(5, value);         // unchanged
}

static void test_cmdprocessor_bool_true_false(void) {
  commandProcessor cp;
  MockStream ms;
  cp.registerStream(&ms);

  bool flag = false;
  Command cmds[] = {
    {"?FLAG", CMDbool, -1, (void *)&flag, NULL, "test bool"},
    {NULL}
  };
  CommandList list = {cmds, NULL};
  cp.registerCommands(&list);

  feedLine(cp, "SFLAG,TRUE\n");
  cp.processCommands();
  TEST_ASSERT_TRUE(flag);

  feedLine(cp, "SFLAG,FALSE\n");
  cp.processCommands();
  TEST_ASSERT_FALSE(flag);
}

static bool functionWasCalled = false;
static void testFunctionHandler(void) { functionWasCalled = true; }

static void test_cmdprocessor_function_dispatch(void) {
  commandProcessor cp;
  MockStream ms;
  cp.registerStream(&ms);

  functionWasCalled = false;
  Command cmds[] = {
    {"DOIT", CMDfunction, 0, (void *)testFunctionHandler, NULL, "test function"},
    {NULL}
  };
  CommandList list = {cmds, NULL};
  cp.registerCommands(&list);

  feedLine(cp, "DOIT\n");
  cp.processCommands();
  TEST_ASSERT_TRUE(functionWasCalled);
}

static void test_cmdprocessor_unknown_command_is_nak(void) {
  commandProcessor cp;
  MockStream ms;
  cp.registerStream(&ms);

  ms.reset();
  feedLine(cp, "NOSUCHCOMMAND\n");
  cp.processCommands();
  TEST_ASSERT_EQUAL(0x15, ms.buf[0]);
}

static void test_cmdprocessor_check_expected_args(void) {
  commandProcessor cp;
  MockStream ms;
  cp.registerStream(&ms);

  ms.reset();
  TEST_ASSERT_FALSE(cp.checkExpectedArgs(2));  // no command line processed yet -> numArgs 0
  TEST_ASSERT_EQUAL(0x15, ms.buf[0]);
}

// ============================================================================
// commandProcessor::executeLine() -- the new synchronous execute+capture
// primitive. registerStream()'s stream (ms) stays the *normal* output path
// throughout; these tests confirm executeLine() never touches it.
// ============================================================================

static void test_executeline_set_and_capture_ack(void) {
  commandProcessor cp;
  MockStream ms;
  cp.registerStream(&ms);

  int value = 0;
  int range[2] = {0, 100};
  Command cmds[] = {
    {"?FOO", CMDint, -1, (void *)&value, (void *)range, "test int"},
    {NULL}
  };
  CommandList list = {cmds, NULL};
  cp.registerCommands(&list);

  char response[32];
  ms.reset();
  bool handled = cp.executeLine("SFOO,42", response, sizeof(response));

  TEST_ASSERT_TRUE(handled);
  TEST_ASSERT_EQUAL(42, value);
  TEST_ASSERT_EQUAL(0x06, (uint8_t)response[0]);  // ACK captured
  TEST_ASSERT_EQUAL(0, ms.len);                    // the *real* stream saw nothing
}

static void test_executeline_get_capture_includes_value(void) {
  commandProcessor cp;
  MockStream ms;
  cp.registerStream(&ms);

  int value = 77;
  Command cmds[] = {
    {"?FOO", CMDint, -1, (void *)&value, NULL, "test int"},
    {NULL}
  };
  CommandList list = {cmds, NULL};
  cp.registerCommands(&list);

  char response[32];
  cp.executeLine("GFOO", response, sizeof(response));
  TEST_ASSERT_EQUAL(0x06, (uint8_t)response[0]);
  TEST_ASSERT_TRUE(strstr(response, "77") != NULL);
}

static void test_executeline_unknown_command_captures_nak(void) {
  commandProcessor cp;
  MockStream ms;
  cp.registerStream(&ms);

  char response[32];
  cp.executeLine("NOSUCHCOMMAND", response, sizeof(response));
  TEST_ASSERT_EQUAL(0x15, (uint8_t)response[0]);
}

static void test_executeline_does_not_disturb_partial_input_in_shared_rb(void) {
  // The scenario this whole design exists to avoid: a human/PC is
  // mid-typing a real command (no EOL yet) in the *shared* rb when a
  // script's executeLine() call happens. If executeLine() reused the
  // shared rb, the injected line would get spliced onto the partial one.
  commandProcessor cp;
  MockStream ms;
  cp.registerStream(&ms);

  int value = 0;
  Command cmds[] = {
    {"?FOO", CMDint, -1, (void *)&value, NULL, "test int"},
    {"?BAR", CMDint, -1, (void *)&value, NULL, "test int"},
    {NULL}
  };
  CommandList list = {cmds, NULL};
  cp.registerCommands(&list);

  // Partial real input: "SFOO," typed but not finished (no value, no EOL).
  const char *partial = "SFOO,";
  for (const char *p = partial; *p; p++) cp.rb->put(*p);
  TEST_ASSERT_EQUAL(0, cp.rb->lines());

  char response[32];
  cp.executeLine("GBAR", response, sizeof(response));
  TEST_ASSERT_EQUAL(0x06, (uint8_t)response[0]);  // the injected call itself worked

  // The partial real input must be exactly as it was left -- untouched.
  TEST_ASSERT_EQUAL(0, cp.rb->lines());
  TEST_ASSERT_EQUAL((int)strlen(partial), cp.rb->count());
}

static void test_executeline_ignores_mute_but_restores_it(void) {
  commandProcessor cp;
  MockStream ms;
  cp.registerStream(&ms);

  int value = 0;
  Command cmds[] = {
    {"?FOO", CMDint, -1, (void *)&value, NULL, "test int"},
    {NULL}
  };
  CommandList list = {cmds, NULL};
  cp.registerCommands(&list);

  // Turn mute on through the normal path (built-in ?MUTE command).
  feedLine(cp, "SMUTE,TRUE\n");
  cp.processCommands();

  // executeLine() must still capture a response despite mute being on...
  char response[32];
  cp.executeLine("SFOO,5", response, sizeof(response));
  TEST_ASSERT_EQUAL(0x06, (uint8_t)response[0]);

  // ...and must leave mute exactly as it found it: still on, so a normal
  // command through the real stream produces no output.
  ms.reset();
  feedLine(cp, "SFOO,6\n");
  cp.processCommands();
  TEST_ASSERT_EQUAL(0, ms.len);
}

void setup() {
  delay(2000);  // let the USB CDC serial monitor attach before results print
  UNITY_BEGIN();

  RUN_TEST(test_ringbuffer_put_get_roundtrip);
  RUN_TEST(test_ringbuffer_empty_get_returns_sentinel);
  RUN_TEST(test_ringbuffer_lines_and_getline);
  RUN_TEST(test_ringbuffer_gettoken_splits_on_delim);
  RUN_TEST(test_ringbuffer_tokenlength_without_complete_line);
  RUN_TEST(test_ringbuffer_ignore_chars_are_dropped);

  RUN_TEST(test_charallocate_alloc_free_and_defrag_reclaims_space);
  RUN_TEST(test_charallocate_alloc_beyond_capacity_fails);
  RUN_TEST(test_charallocate_clear_resets_everything);

  RUN_TEST(test_cmdprocessor_set_and_get_int_with_range);
  RUN_TEST(test_cmdprocessor_set_int_out_of_range_is_nak_and_unchanged);
  RUN_TEST(test_cmdprocessor_bool_true_false);
  RUN_TEST(test_cmdprocessor_function_dispatch);
  RUN_TEST(test_cmdprocessor_unknown_command_is_nak);
  RUN_TEST(test_cmdprocessor_check_expected_args);

  RUN_TEST(test_executeline_set_and_capture_ack);
  RUN_TEST(test_executeline_get_capture_includes_value);
  RUN_TEST(test_executeline_unknown_command_captures_nak);
  RUN_TEST(test_executeline_does_not_disturb_partial_input_in_shared_rb);
  RUN_TEST(test_executeline_ignores_mute_but_restores_it);

  UNITY_END();
}

void loop() {}
