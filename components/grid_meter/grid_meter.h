#pragma once

#include <cmath>
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
                     sensor::Sensor *energy_export_t1, sensor::Sensor *energy_export_t2)
      : power_import_(power_import),
        power_export_(power_export),
        voltage_(voltage),
        current_(current),
        energy_import_t1_(energy_import_t1),
        energy_import_t2_(energy_import_t2),
        energy_export_t1_(energy_export_t1),
        energy_export_t2_(energy_export_t2) {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

 protected:
  // Sensors (all required, set via constructor)
  sensor::Sensor *power_import_;
  sensor::Sensor *power_export_;
  sensor::Sensor *voltage_;
  sensor::Sensor *current_;
  sensor::Sensor *energy_import_t1_;
  sensor::Sensor *energy_import_t2_;
  sensor::Sensor *energy_export_t1_;
  sensor::Sensor *energy_export_t2_;

  // Last known good values — hold-on-NaN for all measured quantities
  // Stored as [low_word, high_word] (little-endian word order, matching Reg_s32l)
  uint16_t voltage_shadow_[2]{0, 0};
  uint16_t current_shadow_[2]{0, 0};
  uint16_t energy_import_shadow_[2]{0, 0};
  uint16_t energy_export_shadow_[2]{0, 0};

  // Dense register bank covering EM24 addresses 0x0000-0x004F
  uint16_t registers_[REG_COUNT]{};

  // TCP server
  int server_fd_{-1};
  Client clients_[MAX_CLIENTS];

  // Helpers
  void refresh_sensors_();
  void accept_clients_();
  void process_client_(Client &c);
  void handle_frame_(Client &c, uint16_t frame_len);
  void send_response_(Client &c, uint16_t txid, uint8_t uid, const uint8_t *pdu, uint8_t pdu_len);
  void send_exception_(Client &c, uint16_t txid, uint8_t uid, uint8_t fc, uint8_t code);
  void close_client_(Client &c);

  // Sparse register lookup: returns value for any EM24 address, including out-of-dense-range
  static uint16_t get_register_(const uint16_t *regs, uint16_t addr);

  // Write a signed int32 as two little-endian uint16 registers (Reg_s32l: low word first)
  static void write_int32_(uint16_t *regs, uint8_t idx, int32_t val);
};

}  // namespace esphome::grid_meter
