#pragma once

#include <cmath>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome::grid_meter {

// EM24 Ethernet register map (dbus-modbus-client carlo_gavazzi.py, models 1648-1653)
// All multi-register values are Reg_s32l: little-endian word order (low word at lower address)
static constexpr uint16_t REG_COUNT = 80;           // dense array covers 0x0000-0x004F
static constexpr uint16_t DEVICE_ID_EM24 = 1648;    // EM24DINAV23XE1X (only EM24 IDs work over TCP)
static constexpr uint8_t MAX_CLIENTS = 2;
static constexpr uint16_t MAX_BUF = 260;
static constexpr uint32_t CLIENT_TIMEOUT_MS = 10000;
static constexpr uint8_t NUM_PHASES = 3;
static constexpr uint16_t SERIAL_REG_BASE = 0x5000;  // Reg_text(0x5000, 7, '/Serial')
static constexpr uint8_t SERIAL_REG_COUNT = 7;

struct Client {
  int fd{-1};
  uint8_t buf[MAX_BUF];
  uint16_t buf_len{0};
  uint32_t last_recv_ms{0};
};

class GridMeterComponent : public Component {
 public:
  GridMeterComponent(sensor::Sensor *power_import, sensor::Sensor *power_export,
                     sensor::Sensor *voltage, sensor::Sensor *current,
                     sensor::Sensor *energy_import_t1, sensor::Sensor *energy_import_t2,
                     sensor::Sensor *energy_export_t1, sensor::Sensor *energy_export_t2,
                     uint16_t port)
      : voltage_{voltage, nullptr, nullptr},
        current_{current, nullptr, nullptr},
        power_import_{power_import, nullptr, nullptr},
        power_export_{power_export, nullptr, nullptr},
        energy_import_t1_(energy_import_t1),
        energy_import_t2_(energy_import_t2),
        energy_export_t1_(energy_export_t1),
        energy_export_t2_(energy_export_t2),
        port_(port) {}

  // Optional L2/L3 inputs (phase index 1 = L2, 2 = L3); presence enables 3-phase emulation
  void set_phase_sensors(uint8_t phase, sensor::Sensor *voltage, sensor::Sensor *current,
                         sensor::Sensor *power_import, sensor::Sensor *power_export) {
    this->voltage_[phase] = voltage;
    this->current_[phase] = current;
    this->power_import_[phase] = power_import;
    this->power_export_[phase] = power_export;
  }
  void set_serial_number(const std::string &serial);
  void set_data_timeout(uint32_t timeout_ms) { this->data_timeout_ms_ = timeout_ms; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

 protected:
  // Per-phase inputs; index 0 = L1 (required, set via constructor), 1-2 = L2/L3 (optional)
  sensor::Sensor *voltage_[NUM_PHASES];
  sensor::Sensor *current_[NUM_PHASES];
  sensor::Sensor *power_import_[NUM_PHASES];
  sensor::Sensor *power_export_[NUM_PHASES];
  sensor::Sensor *energy_import_t1_;
  sensor::Sensor *energy_import_t2_;
  sensor::Sensor *energy_export_t1_;
  sensor::Sensor *energy_export_t2_;

  // Last known good values — hold-on-NaN for all measured quantities
  // Stored as [low_word, high_word] (little-endian word order, matching Reg_s32l)
  uint16_t voltage_shadow_[NUM_PHASES][2]{};
  uint16_t current_shadow_[NUM_PHASES][2]{};
  uint16_t energy_import_shadow_[2]{0, 0};
  uint16_t energy_export_shadow_[2]{0, 0};

  // Dense register bank covering EM24 addresses 0x0000-0x004F
  uint16_t registers_[REG_COUNT]{};

  // Sparse identification registers
  uint16_t serial_regs_[SERIAL_REG_COUNT]{};  // 0x5000-0x5006, 2 ASCII chars per register
  uint16_t phase_config_{3};                  // 0x1002: 0 = 3P.n, 3 = 1P (set in setup())

  // Stale-data watchdog: refuse Modbus service when the power feed stops updating,
  // so the Cerbo marks the meter offline instead of acting on frozen values
  uint32_t data_timeout_ms_{30000};  // 0 disables the watchdog
  uint32_t last_valid_power_ms_{0};
  bool has_valid_power_{false};
  bool stale_{false};

  // Registers are rebuilt only when a sensor pushes a new state
  bool registers_dirty_{true};

  // TCP server
  uint16_t port_;
  int server_fd_{-1};
  Client clients_[MAX_CLIENTS];

  // Helpers
  void refresh_sensors_();
  int32_t refresh_phase_(uint8_t phase);  // returns net power raw (÷10 W), 0 if unavailable
  void accept_clients_();
  void process_client_(Client &c);
  void handle_frame_(Client &c, uint16_t frame_len);
  void send_response_(Client &c, uint16_t txid, uint8_t uid, const uint8_t *pdu, uint8_t pdu_len);
  void send_exception_(Client &c, uint16_t txid, uint8_t uid, uint8_t fc, uint8_t code);
  void close_client_(Client &c);

  // Sparse register lookup: returns value for any EM24 address, including out-of-dense-range
  uint16_t get_register_(uint16_t addr) const;

  // Write a signed int32 as two little-endian uint16 registers (Reg_s32l: low word first)
  static void write_int32_(uint16_t *regs, uint8_t idx, int32_t val);
};

}  // namespace esphome::grid_meter
