#pragma once

#include <cmath>
#include <sys/socket.h>
#include <netinet/in.h>

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace grid_meter {

static const char *const TAG = "grid_meter";

// EM24 Ethernet register map (dbus-modbus-client carlo_gavazzi.py, models 1648-1653)
// All multi-register values are Reg_s32l: little-endian word order (low word at lower address)
static const uint16_t REG_COUNT        = 80;   // dense array covers 0x0000-0x004F
static const uint16_t DEVICE_ID_EM24   = 1648; // EM24DINAV23XE1X (only EM24 IDs work over TCP)
static const uint8_t  MAX_CLIENTS      = 2;
static const uint16_t MAX_BUF          = 260;
static const uint32_t CLIENT_TIMEOUT_MS = 10000;

struct Client {
  int      fd{-1};
  uint8_t  buf[MAX_BUF];
  uint16_t buf_len{0};
  uint32_t last_recv_ms{0};
};

class GridMeterComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  // Setters called from __init__.py generated code -- all required
  void set_power_import(sensor::Sensor *s)     { power_import_ = s; }
  void set_power_export(sensor::Sensor *s)     { power_export_ = s; }
  void set_voltage(sensor::Sensor *s)          { voltage_ = s; }
  void set_current(sensor::Sensor *s)          { current_ = s; }
  void set_energy_import_t1(sensor::Sensor *s) { energy_import_t1_ = s; }
  void set_energy_import_t2(sensor::Sensor *s) { energy_import_t2_ = s; }
  void set_energy_export_t1(sensor::Sensor *s) { energy_export_t1_ = s; }
  void set_energy_export_t2(sensor::Sensor *s) { energy_export_t2_ = s; }

 protected:
  // Sensors
  sensor::Sensor *power_import_{nullptr};
  sensor::Sensor *power_export_{nullptr};
  sensor::Sensor *voltage_{nullptr};
  sensor::Sensor *current_{nullptr};
  sensor::Sensor *energy_import_t1_{nullptr};
  sensor::Sensor *energy_import_t2_{nullptr};
  sensor::Sensor *energy_export_t1_{nullptr};
  sensor::Sensor *energy_export_t2_{nullptr};

  // Last known good values for voltage and current (hold-on-NaN)
  // Stored as [low_word, high_word] (little-endian word order, matching Reg_s32l)
  uint16_t voltage_shadow_[2]{0, 0};
  uint16_t current_shadow_[2]{0, 0};

  // Dense register bank covering EM24 addresses 0x0000-0x004F
  uint16_t registers_[REG_COUNT]{};

  // TCP server
  int    server_fd_{-1};
  Client clients_[MAX_CLIENTS];

  // Helpers
  void refresh_sensors_();
  void accept_clients_();
  void process_client_(Client &c);
  void handle_frame_(Client &c, uint16_t frame_len);
  void send_response_(int fd, uint16_t txid, uint8_t uid, const uint8_t *pdu, uint8_t pdu_len);
  void send_exception_(int fd, uint16_t txid, uint8_t uid, uint8_t fc, uint8_t code);
  void close_client_(Client &c);

  // Sparse register lookup: returns value for any EM24 address, including out-of-dense-range
  static uint16_t get_register_(const uint16_t *regs, uint16_t addr);

  // Write a signed int32 as two little-endian uint16 registers (Reg_s32l: low word first)
  static void write_int32_(uint16_t *regs, uint8_t idx, int32_t val);
};

}  // namespace grid_meter
}  // namespace esphome
