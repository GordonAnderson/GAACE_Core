/**
 * DiagnosticDemo
 *
 * Minimal GAACE_Core application. Wires up commandProcessor and the debug
 * module on the USB serial port, and registers one application-specific
 * command (BLINK / ?LED) to show how a project's own commands sit alongside
 * the built-in debug command table.
 *
 * Connect a serial terminal at 115200 baud, line ending "\n", and try:
 *   GCMDS          - list every registered command with its help string
 *   UPTIME         - minutes since boot
 *   RAM            - approximate free heap/stack
 *   CPUTEMP        - on-chip temperature (SAMD21)
 *   UUID           - 128-bit hardware UUID
 *   SLED,TRUE      - turn the onboard LED on
 *   GLED           - read the onboard LED state
 *   BLINK,5        - blink the onboard LED 5 times, then restore its state
 */
#include <Arduino.h>
#include <commandProcessor.h>
#include <debug.h>

commandProcessor cp;
debug dbg(&cp);

static bool ledState = false;

static void blink(void)
{
  int count;
  if (!cp.checkExpectedArgs(1)) return;
  if (!cp.getValue(&count, 1, 50)) { cp.sendNAK(); return; }

  for (int i = 0; i < count; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
  }
  digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
  cp.sendACK();
}

static Command appCmds[] = {
  {"?LED",  CMDbool,     -1, &ledState,    NULL, "Onboard LED state (TRUE/FALSE)"},
  {"BLINK", CMDfunction,  1, (void*)blink, NULL, "Blink the onboard LED N times"},
  {NULL}
};
static CommandList appList = {appCmds, NULL};

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.begin(115200);
  cp.registerStream(&Serial);
  cp.registerCommands(&appList);
  cp.registerCommands(dbg.debugCommands());
}

void loop()
{
  cp.processStreams();
  cp.processCommands();
  digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
}
