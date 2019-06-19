#include <OneWire.h>
#include <DS18B20.h>
#include <BME280.h>
#include <SimpleSIM.h>

#include <SH1106_I2C_Adaptor.h>
#include <glcd_fonts.h>

#include "nv_utils.h"

#define DS18B20PIN 17
#define GSM_BAUDS 9600
#define GSM_MAX_ERRS 10
#define GSM_RST_PIN  2
#define ON_PIN 16

#define ON_ADDR   0x10
#define REP_ADDR  0x14
#define PIN_ADDR  0x20
#define PEER_ADDR 0x30
#define PIN_MAX_LEN 14
#define PEER_LEN    14
#define PEER_OFF    6

// 128x64 OLED display adapter
// https://github.com/olegv142/Display
static SH1106_I2C_Adaptor g_display;

// BME280 environmental unit controller
// https://github.com/olegv142/BME280
static BME280Sensor g_bme;

// DS18B20 temperature sensor controller
// https://github.com/olegv142/OneWire
static OneWire      g_w(DS18B20PIN);
// https://github.com/olegv142/DS18B20
static DS18B20      g_t(g_w);
static bool         g_t_present;

// SIM800L GSM module controller
// https://github.com/olegv142/SimpleSIM
static SimpleSIM    g_gsm(Serial, GSM_RST_PIN);
// GSM module context
static bool         g_gsm_started;
static int8_t       g_gsm_err_cnt;
static SIMHook      g_gsm_csq("+CSQ");
static SIMHook      g_gsm_cmt("+CMT");
static SIMHook      g_gsm_msg("#");

// Current temperature and humidity readings
static String g_tstr;
static String g_hstr;
static String g_tstr2;

// AC switch On flag
static int8_t g_on;
// The reporting period in hours
static int8_t g_rep;
static bool   g_rep_valid;
// The pin/password
static char   g_pin[PIN_MAX_LEN+1];
static bool   g_pin_valid;  
// The peer address (phone number)
static char   g_peer[PEER_LEN+1];
static bool   g_peer_valid;
// The last report timestamp in milliseconds
static uint32_t g_last_rep;

static void init_sensors()
{
  g_bme.begin();
  g_bme.init(sp_62_5ms, sf_16);
  g_bme.start(md_Normal, sx_1x, sx_16x, sx_1x);

  uint8_t rom[8];
  g_t_present = g_t.probe(rom);
  if (g_t_present)
    g_t.convert();
}

static void read_sensors()
{
  int32_t  T; // temperature in 0.01 DegC units
  uint32_t P; // pressure in Pa with Q24.8 format (24 integer bits and 8 fractional bits)
  uint32_t H; // humidity in %% with Q22.10 format (22 integer and 10 fractional bits)
  if (g_bme.read32(&T, &P, &H))
  {
    char str[16] = {0};
    String tstr(T/100., 1);
    tstr += 'C';
    g_tstr = tstr;
    String hstr(H/1024);
    hstr += '%';
    g_hstr = hstr;
  }
  if (g_t_present) {
    int16_t v;
    g_t.read(v);
    g_t.convert();
    String tstr(v/16., 1);
    tstr += 'C';
    g_tstr2 = tstr;
  }
}

static void update_display()
{
  uint8_t  W = g_display.width();
  glcd_print_str_r(&g_display, 0, 0, W/2, g_tstr.c_str(), &g_font_Tahoma19x20, 1);
  glcd_print_str_r(&g_display, 0, 4, W/2, g_hstr.c_str(), &g_font_Tahoma19x20, 1);
  glcd_print_str_r(&g_display, W/2, 0, W/2, g_tstr2.c_str(), &g_font_Tahoma19x20, 1);
  glcd_print_str_r(&g_display, W/2, 4, W/2, g_on ? "On" : "Off", &g_font_Tahoma19x20, 1);
}

static void init_gsm()
{
  Serial.begin(GSM_BAUDS);
  g_gsm.add_hook(&g_gsm_csq);
  g_gsm.add_hook(&g_gsm_cmt);
  g_gsm.add_hook(&g_gsm_msg);
  g_gsm.begin();
}

static void reset_gsm()
{
  g_gsm.reset();
  g_gsm_err_cnt = 0;
  g_gsm_started = false;
}

static void on_gsm_err()
{
  if (++g_gsm_err_cnt >= GSM_MAX_ERRS) {
    reset_gsm();
  }
}

static inline String peer_addr()
{
  return g_gsm_cmt.str().substring(PEER_OFF, PEER_OFF + PEER_LEN);
}

static inline const char* peer_addr_ptr()
{
  return g_gsm_cmt.c_str() + PEER_OFF;
}

static bool send_report()
{
  String sms_cmd("+CMGS=");
  sms_cmd += peer_addr();
  String resp('#');
  resp += g_on;
  resp += ' ';
  resp += g_tstr;
  resp += ' ';
  resp += g_hstr;
  resp += ' ';
  resp += g_tstr2;
  resp += ' ';
  resp += g_rep;
  resp += 'h';
  resp += ' ';
  if (sim_ok != g_gsm.send_cmd("+CSQ"))
    return false;
  resp += g_gsm_csq.str();
  if (
    sim_prompt == g_gsm.send_cmd(sms_cmd.c_str()) &&
    sim_ok     == g_gsm.send_msg(resp.c_str())
  ) {
    g_last_rep = millis();
    return true;
  }
  return false;
}

static void update_output()
{  
  digitalWrite(LED_BUILTIN, g_on);  
  digitalWrite(ON_PIN, g_on);  
}

// Set AC switch state
static void set_output(bool on)
{
  g_on = on;
  nv_put(&g_on, sizeof(g_on), ON_ADDR);
  update_output();
}

// Set new PIN
static void set_pin(const char* pin)
{
  strncpy(g_pin, pin, PIN_MAX_LEN);
  nv_put(&g_pin, PIN_MAX_LEN, PIN_ADDR);
  g_pin_valid = (g_pin[0] != '\0');
}

// Set reporting interval
static void set_reporting_interval(const char* arg)
{
  g_rep = atoi(arg);
  nv_put(&g_rep, sizeof(g_rep), REP_ADDR);
  g_rep_valid = g_rep > 0;
}

// Save sender address so it will be
// implicitely authenticated without PIN
static void save_peer_address()
{
  const char* peer = peer_addr_ptr();
  if (g_peer_valid && !strncmp(g_peer, peer, PEER_LEN))
    return;
  memcpy(g_peer, peer, PEER_LEN);
  nv_put(&g_peer, PEER_LEN, PEER_ADDR);
  g_peer_valid = true;
}

/*
 * The incoming SMS message parser.
 * The message always starts from the # symbol.
 * It may be followed by the following tokens separated by the space or comma:
 *  1    turn on  AC switch
 *  0    turn off AC switch
 *  /n   set reporting interval to n hours
 *  pPIN authenticate with PIN
 *  PPIN set new PIN
 * The response will be sent to any authenticated message, even the empty one
 * (consisting from the single # symbol)
 */
static bool process_message()
{
  char* ptr = g_gsm_msg.str().begin() + 1;
  char *sptr = 0, *eptr = 0;
  // The sender is authenticated implicitly unless the PIN is set and the sender address differs
  // from the address used previously. In such case the new sender should provide PIN in order
  // to be authenticated.
  bool auth = !g_pin_valid || !g_peer_valid || !strncmp(g_peer, peer_addr_ptr(), PEER_LEN);
  for (bool done = false; !done; ++ptr) {
    if (*ptr) {
      if (!sptr) {
        // No current token
        switch (*ptr) {
        case '0':
        case '1':
          // AC switch control
          if (auth)
            set_output(*ptr == '1');
          break;
        case 'p':
        case 'P':
        case '/':
          // Start collecting argument string
          sptr = ptr;
          break;
        default:
          ;
        }
      } else {
        switch (*ptr) {
        case ' ':
        case ',':
        case '\r':
        case '\n':
          // Token delimiter found
          eptr = ptr;
          break;
        }
      }
    } else {
      // End of the message string reached
      // Terminate current token if any
      if (sptr)
        eptr = ptr;
      done = true;
    }
    if (eptr) {
      // We have the token to process
      char* arg = sptr + 1;
      // Zero terminate token
      *eptr = 0;
      switch (*sptr) {
      case 'p':
        // Authenticate with PIN
        auth = !g_pin_valid || !strcmp(g_pin, arg);
        break;
      case 'P':
        if (auth) {
          set_pin(arg);
        }
        break;
      case '/':
        if (auth) {
          set_reporting_interval(arg);
        }
        break;
      }
      // We have done with current token
      sptr = eptr = 0;
    }
  }
  if (auth) {
    // Save peer address and respond
    save_peer_address();
    return send_report();
  }
  return true;
}

static void process_gsm()
{
  if (!g_gsm_started) {
    if (g_gsm.start(GSM_BAUDS) == sim_ok) {
      g_gsm_started = true;
    } else {
      on_gsm_err();
    }
  } else {
    if (g_gsm_msg) {
      if (process_message()) {
        g_gsm_msg.reset();
      } else {
        on_gsm_err();
      }
    } else if (g_rep_valid && g_last_rep + g_rep * 3600000ULL < millis()) {
      if (!send_report()) {
        on_gsm_err();
      }
    }
  }
}

static void init_output()
{
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(ON_PIN, OUTPUT);
  update_output();
}

// Read parameters from non-volatile memory
static void init_params()
{
  nv_get(&g_on, sizeof(g_on), ON_ADDR);
  g_rep_valid  = nv_get(&g_rep, sizeof(g_rep), REP_ADDR) && g_rep > 0;
  g_pin_valid  = nv_get(&g_pin, PIN_MAX_LEN, PIN_ADDR) && g_pin[0] != '\0';
  g_peer_valid = nv_get(&g_peer, PEER_LEN, PEER_ADDR) && g_peer[0] == '"' && g_peer[PEER_LEN-1] == '"';
}

void init_display()
{
  g_display.init();
  uint8_t  W = g_display.width();
  glcd_print_str(&g_display, 0, 0, "Starting..", &g_font_Tahoma19x20, 1);
  glcd_print_str_r(&g_display, W/2, 4, W/2, g_on ? "On" : "Off", &g_font_Tahoma19x20, 1);
}

void setup() {
  init_params();
  init_output();
  init_display();
  init_gsm();
  init_sensors();
}

void loop() {
  g_gsm.wait(1000);
  read_sensors();
  update_display();
  process_gsm();
}
