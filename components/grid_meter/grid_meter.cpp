#include "grid_meter.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome::grid_meter {

static const char *const TAG = "grid_meter";

// ---------- static helpers ----------

// Write signed int32 as two little-endian uint16 registers (Reg_s32l: low word at idx)
void GridMeterComponent::write_int32_(uint16_t *regs, uint8_t idx, int32_t val) {
  regs[idx]     = static_cast<uint16_t>(static_cast<uint32_t>(val) & 0xFFFF);  // low word first
  regs[idx + 1] = static_cast<uint16_t>(static_cast<uint32_t>(val) >> 16);     // high word second
}

// Sparse lookup for EM24 registers. Returns 0 for any unknown or unimplemented address.
uint16_t GridMeterComponent::get_register_(uint16_t addr) const {
  if (addr < REG_COUNT) return this->registers_[addr];
  if (addr == 0x0302) return 0x0100;  // HW version 1.0.0
  if (addr == 0x0304) return 0x0100;  // FW version 1.0.0
  if (addr == 0x1002) return this->phase_config_;  // PhaseConfig: 0 = 3P.n, 3 = 1P
  if (addr == 0xa000) return 7;       // Application = H mode
  if (addr == 0xa100) return 2;       // SwitchPos = '1' (active kWh, both directions)
  if (addr >= SERIAL_REG_BASE && addr < SERIAL_REG_BASE + SERIAL_REG_COUNT)
    return this->serial_regs_[addr - SERIAL_REG_BASE];
  return 0;
}

void GridMeterComponent::set_serial_number(const std::string &serial) {
  // Reg_text: 2 ASCII chars per register, first char in the high byte; null-padded
  for (uint8_t i = 0; i < SERIAL_REG_COUNT; i++) {
    uint8_t hi = (i * 2 < serial.size()) ? static_cast<uint8_t>(serial[i * 2]) : 0;
    uint8_t lo = (i * 2 + 1 < serial.size()) ? static_cast<uint8_t>(serial[i * 2 + 1]) : 0;
    this->serial_regs_[i] = static_cast<uint16_t>((hi << 8) | lo);
  }
}

// ---------- lifecycle ----------

void GridMeterComponent::setup() {
  // Register bank is zero-initialised at construction; set constant fields
  this->registers_[0x000B] = DEVICE_ID_EM24;  // Model ID register (probed by carlo_gavazzi.py)
  this->registers_[0x0033] = 500;             // Frequency: 50.0 Hz (Reg_u16, ÷10 Hz)

  // 3-phase emulation when L2/L3 sensors are configured (0 = 3P.n), else single phase (3 = 1P)
  bool three_phase = this->power_import_[1] != nullptr || this->power_import_[2] != nullptr;
  this->phase_config_ = three_phase ? 0 : 3;

  // Rebuild registers only when a sensor pushes a new state
  auto mark_dirty = [this](float) { this->registers_dirty_ = true; };
  for (uint8_t ph = 0; ph < NUM_PHASES; ph++) {
    for (auto *s : {this->voltage_[ph], this->current_[ph], this->power_import_[ph], this->power_export_[ph]}) {
      if (s != nullptr)
        s->add_on_state_callback(mark_dirty);
    }
  }
  for (auto *s : {this->energy_import_t1_, this->energy_import_t2_, this->energy_export_t1_, this->energy_export_t2_})
    s->add_on_state_callback(mark_dirty);

  // Open non-blocking TCP server socket
  this->server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (this->server_fd_ < 0) {
    ESP_LOGE(TAG, "socket() failed: %d", errno);
    this->mark_failed(LOG_STR("socket() failed"));
    return;
  }

  int opt = 1;
  ::setsockopt(this->server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(this->port_);

  if (::bind(this->server_fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
    ESP_LOGE(TAG, "bind() failed on port %u: %d", this->port_, errno);
    ::close(this->server_fd_);
    this->server_fd_ = -1;
    this->mark_failed(LOG_STR("bind() failed"));
    return;
  }

  if (::listen(this->server_fd_, 2) < 0) {
    ESP_LOGE(TAG, "listen() failed: %d", errno);
    ::close(this->server_fd_);
    this->server_fd_ = -1;
    this->mark_failed(LOG_STR("listen() failed"));
    return;
  }

  int flags = ::fcntl(this->server_fd_, F_GETFL, 0);
  ::fcntl(this->server_fd_, F_SETFL, flags | O_NONBLOCK);

  ESP_LOGI(TAG, "Grid meter Modbus TCP server listening on port %u (EM24 emulation, %s)", this->port_,
           three_phase ? "3-phase" : "single phase");
}

void GridMeterComponent::loop() {
  if (this->server_fd_ < 0)
    return;

  if (this->registers_dirty_) {
    this->refresh_sensors_();
    this->registers_dirty_ = false;
  }

  // Stale-data watchdog: when the power feed stops updating, refuse Modbus service so
  // the Cerbo marks the meter offline instead of acting on frozen values
  if (this->data_timeout_ms_ != 0 && this->has_valid_power_ &&
      millis() - this->last_valid_power_ms_ > this->data_timeout_ms_) {
    if (!this->stale_) {
      ESP_LOGW(TAG, "No valid power reading for %" PRIu32 " ms, suspending Modbus service", this->data_timeout_ms_);
      this->stale_ = true;
    }
    for (auto &c : this->clients_)
      this->close_client_(c);
    int fd;
    while ((fd = ::accept(this->server_fd_, nullptr, nullptr)) >= 0)
      ::close(fd);
    return;
  }
  if (this->stale_) {
    ESP_LOGI(TAG, "Power data valid again, resuming Modbus service");
    this->stale_ = false;
  }

  this->accept_clients_();

  for (auto &c : this->clients_) {
    if (c.fd >= 0)
      this->process_client_(c);
  }
}

void GridMeterComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Grid Meter (EM24 emulation over Modbus TCP):");
  ESP_LOGCONFIG(TAG, "  Port: %u", this->port_);
  ESP_LOGCONFIG(TAG, "  Phase config: %s", this->phase_config_ == 0 ? "3P.n" : "1P");
  ESP_LOGCONFIG(TAG, "  Data timeout: %" PRIu32 " ms", this->data_timeout_ms_);
  static const char *const PHASE_NAMES[NUM_PHASES] = {"L1", "L2", "L3"};
  for (uint8_t ph = 0; ph < NUM_PHASES; ph++) {
    if (this->power_import_[ph] == nullptr)
      continue;
    ESP_LOGCONFIG(TAG, "  Phase %s:", PHASE_NAMES[ph]);
    LOG_SENSOR("    ", "Power Import", this->power_import_[ph]);
    LOG_SENSOR("    ", "Power Export", this->power_export_[ph]);
    LOG_SENSOR("    ", "Voltage", this->voltage_[ph]);
    LOG_SENSOR("    ", "Current", this->current_[ph]);
  }
  LOG_SENSOR("  ", "Energy Import T1", this->energy_import_t1_);
  LOG_SENSOR("  ", "Energy Import T2", this->energy_import_t2_);
  LOG_SENSOR("  ", "Energy Export T1", this->energy_export_t1_);
  LOG_SENSOR("  ", "Energy Export T2", this->energy_export_t2_);
}

// ---------- sensor refresh ----------

// Per-phase registers, offset 2*(phase) from the L1 base:
//   Voltage (Reg_s32l, ÷10 V)   at 0x0000  -- hold last good on NaN
//   Current (Reg_s32l, ÷1000 A) at 0x000C  -- hold last good on NaN, always positive magnitude
//   Power   (Reg_s32l, ÷10 W)   at 0x0012  -- positive = import, negative = export; zero on NaN
int32_t GridMeterComponent::refresh_phase_(uint8_t phase) {
  const uint16_t off = 2 * phase;

  if (this->voltage_[phase] != nullptr) {
    float v = this->voltage_[phase]->get_state();
    if (!std::isnan(v)) {
      int32_t v_raw = static_cast<int32_t>(v * 10.0f + 0.5f);
      this->voltage_shadow_[phase][0] = static_cast<uint16_t>(static_cast<uint32_t>(v_raw) & 0xFFFF);
      this->voltage_shadow_[phase][1] = static_cast<uint16_t>(static_cast<uint32_t>(v_raw) >> 16);
    }
    this->registers_[0x0000 + off] = this->voltage_shadow_[phase][0];
    this->registers_[0x0001 + off] = this->voltage_shadow_[phase][1];
  }

  if (this->current_[phase] != nullptr) {
    float i = this->current_[phase]->get_state();
    if (!std::isnan(i)) {
      int32_t i_raw = static_cast<int32_t>(std::abs(i) * 1000.0f + 0.5f);
      this->current_shadow_[phase][0] = static_cast<uint16_t>(static_cast<uint32_t>(i_raw) & 0xFFFF);
      this->current_shadow_[phase][1] = static_cast<uint16_t>(static_cast<uint32_t>(i_raw) >> 16);
    }
    this->registers_[0x000C + off] = this->current_shadow_[phase][0];
    this->registers_[0x000D + off] = this->current_shadow_[phase][1];
  }

  int32_t pwr_raw = 0;
  if (this->power_import_[phase] != nullptr && this->power_export_[phase] != nullptr) {
    float imp = this->power_import_[phase]->get_state();
    float exp_pwr = this->power_export_[phase]->get_state();
    if (!std::isnan(imp) && !std::isnan(exp_pwr)) {
      float net = imp - exp_pwr;
      pwr_raw = static_cast<int32_t>(net * 10.0f + (net >= 0 ? 0.5f : -0.5f));
      this->last_valid_power_ms_ = millis();
      this->has_valid_power_ = true;
    }
  }
  write_int32_(this->registers_, static_cast<uint8_t>(0x12 + off), pwr_raw);
  return pwr_raw;
}

void GridMeterComponent::refresh_sensors_() {
  // Total power (Reg_s32l, ÷10 W) at 0x0028-0x0029 = sum of configured phases
  int32_t total_pwr = 0;
  for (uint8_t ph = 0; ph < NUM_PHASES; ph++)
    total_pwr += this->refresh_phase_(ph);
  write_int32_(this->registers_, 0x28, total_pwr);

  // Energy import total (Reg_s32l, ÷10 kWh) at 0x0034-0x0035 (total) and 0x0040-0x0041 (L1)
  // Hold last valid value on NaN to prevent transient zeroes corrupting Cerbo energy deltas.
  float ei1 = this->energy_import_t1_->get_state();
  float ei2 = this->energy_import_t2_->get_state();
  if (!std::isnan(ei1) && !std::isnan(ei2)) {
    double kwh = static_cast<double>(ei1) + static_cast<double>(ei2);
    double raw = kwh * 10.0;
    if (raw > static_cast<double>(INT32_MAX)) {
      ESP_LOGW(TAG, "Energy import value %.1f kWh exceeds INT32_MAX, clamping", kwh);
      raw = static_cast<double>(INT32_MAX);
    }
    raw = std::max(0.0, raw);
    int32_t ei_raw = static_cast<int32_t>(raw);
    this->energy_import_shadow_[0] = static_cast<uint16_t>(static_cast<uint32_t>(ei_raw) & 0xFFFF);
    this->energy_import_shadow_[1] = static_cast<uint16_t>(static_cast<uint32_t>(ei_raw) >> 16);
  }
  this->registers_[0x0034] = this->energy_import_shadow_[0];
  this->registers_[0x0035] = this->energy_import_shadow_[1];
  // DSMR provides only meter totals, so all energy is attributed to L1 (L2/L3 stay 0 in 3P mode)
  this->registers_[0x0040] = this->energy_import_shadow_[0];
  this->registers_[0x0041] = this->energy_import_shadow_[1];

  // Energy export total (Reg_s32l, ÷10 kWh) at 0x004E-0x004F
  // Hold last valid value on NaN to prevent transient zeroes corrupting Cerbo energy deltas.
  float ee1 = this->energy_export_t1_->get_state();
  float ee2 = this->energy_export_t2_->get_state();
  if (!std::isnan(ee1) && !std::isnan(ee2)) {
    double kwh = static_cast<double>(ee1) + static_cast<double>(ee2);
    double raw = kwh * 10.0;
    if (raw > static_cast<double>(INT32_MAX)) {
      ESP_LOGW(TAG, "Energy export value %.1f kWh exceeds INT32_MAX, clamping", kwh);
      raw = static_cast<double>(INT32_MAX);
    }
    raw = std::max(0.0, raw);
    int32_t ee_raw = static_cast<int32_t>(raw);
    this->energy_export_shadow_[0] = static_cast<uint16_t>(static_cast<uint32_t>(ee_raw) & 0xFFFF);
    this->energy_export_shadow_[1] = static_cast<uint16_t>(static_cast<uint32_t>(ee_raw) >> 16);
  }
  this->registers_[0x004E] = this->energy_export_shadow_[0];
  this->registers_[0x004F] = this->energy_export_shadow_[1];
  this->registers_[0x0046] = this->energy_export_shadow_[0];  // L1 energy reverse = total
  this->registers_[0x0047] = this->energy_export_shadow_[1];
}

// ---------- TCP server ----------

void GridMeterComponent::accept_clients_() {
  struct sockaddr_in client_addr{};
  socklen_t addr_len = sizeof(client_addr);
  int fd;
  while ((fd = ::accept(this->server_fd_, reinterpret_cast<struct sockaddr *>(&client_addr), &addr_len)) >= 0) {
    bool accepted = false;
    for (auto &c : this->clients_) {
      if (c.fd < 0) {
        int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int nodelay = 1;  // Modbus is small request/response frames; Nagle only adds latency
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
        c.fd = fd;
        c.buf_len = 0;
        c.last_recv_ms = millis();
        ESP_LOGD(TAG, "Client connected (fd=%d)", fd);
        accepted = true;
        break;
      }
    }
    if (!accepted) {
      ESP_LOGW(TAG, "Max clients reached, rejecting connection");
      ::close(fd);
    }
    addr_len = sizeof(client_addr);
  }
}

void GridMeterComponent::close_client_(Client &c) {
  if (c.fd >= 0) {
    ESP_LOGD(TAG, "Closing client (fd=%d)", c.fd);
    ::close(c.fd);
    c.fd = -1;
    c.buf_len = 0;
  }
}

void GridMeterComponent::process_client_(Client &c) {
  // Timeout check
  if (millis() - c.last_recv_ms > CLIENT_TIMEOUT_MS) {
    ESP_LOGD(TAG, "Client timeout (fd=%d)", c.fd);
    this->close_client_(c);
    return;
  }

  // Read into buffer
  int n = ::recv(c.fd, c.buf + c.buf_len, MAX_BUF - c.buf_len, 0);
  if (n == 0) {
    this->close_client_(c);
    return;
  }
  if (n < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK)
      this->close_client_(c);
    return;
  }
  c.buf_len += static_cast<uint16_t>(n);
  c.last_recv_ms = millis();

  // Process all complete frames in buffer
  while (c.buf_len >= 6) {
    uint16_t proto_id = (c.buf[2] << 8) | c.buf[3];
    uint16_t pdu_length = (c.buf[4] << 8) | c.buf[5];

    if (proto_id != 0x0000) {
      ESP_LOGD(TAG, "Invalid protocol ID %04X, closing", proto_id);
      this->close_client_(c);
      return;
    }

    // pdu_length must include at least unit-id + function-code bytes
    if (pdu_length < 2) {
      ESP_LOGD(TAG, "PDU length %u too short, closing", pdu_length);
      this->close_client_(c);
      return;
    }

    uint16_t frame_len = 6 + pdu_length;
    if (frame_len > MAX_BUF) {
      ESP_LOGD(TAG, "Frame too large (%u bytes), closing", frame_len);
      this->close_client_(c);
      return;
    }

    if (c.buf_len < frame_len)
      break;  // incomplete frame

    this->handle_frame_(c, frame_len);
    if (c.fd < 0)
      return;  // handle_frame_ closed the client (send failed)

    c.buf_len -= frame_len;
    if (c.buf_len > 0)
      memmove(c.buf, c.buf + frame_len, c.buf_len);
  }

  // Buffer full with no complete frame extractable: the client is sending garbage
  if (c.buf_len >= MAX_BUF) {
    ESP_LOGD(TAG, "Client buffer overflow (fd=%d), closing", c.fd);
    this->close_client_(c);
  }
}

void GridMeterComponent::handle_frame_(Client &c, uint16_t frame_len) {
  uint16_t txid = (c.buf[0] << 8) | c.buf[1];
  uint8_t uid = c.buf[6];
  uint8_t fc = c.buf[7];

  if (fc == 0x03 || fc == 0x04) {
    // FC03 / FC04: Read Holding/Input Registers
    if (frame_len < 12) {
      this->send_exception_(c, txid, uid, fc, 0x03);
      return;
    }
    uint16_t start = (c.buf[8] << 8) | c.buf[9];
    uint16_t count = (c.buf[10] << 8) | c.buf[11];
    ESP_LOGV(TAG, "FC%02X start=0x%04X count=%u", fc, start, count);

    if (count == 0 || count > 125) {
      this->send_exception_(c, txid, uid, fc, 0x03);  // Illegal Data Value
      return;
    }

    // Build response using sparse register lookup -- returns 0 for any unknown address
    uint8_t pdu[2 + 125 * 2];
    pdu[0] = fc;
    pdu[1] = static_cast<uint8_t>(count * 2);
    for (uint16_t i = 0; i < count; i++) {
      uint16_t addr = static_cast<uint16_t>(static_cast<uint32_t>(start) + i);
      uint16_t val = this->get_register_(addr);
      pdu[2 + i * 2]     = val >> 8;
      pdu[2 + i * 2 + 1] = val & 0xFF;
    }
    this->send_response_(c, txid, uid, pdu, static_cast<uint8_t>(2 + count * 2));

  } else if (fc == 0x06) {
    // FC06: Write Single Register -- accept as no-op (echo request back)
    if (frame_len < 12) {
      this->send_exception_(c, txid, uid, fc, 0x03);
      return;
    }
    uint16_t addr = (c.buf[8] << 8) | c.buf[9];
    uint16_t val = (c.buf[10] << 8) | c.buf[11];
    ESP_LOGD(TAG, "FC06 write addr=0x%04X val=0x%04X (ignored)", addr, val);
    uint8_t pdu[5] = {fc, c.buf[8], c.buf[9], c.buf[10], c.buf[11]};
    this->send_response_(c, txid, uid, pdu, 5);

  } else if (fc == 0x10) {
    // FC16: Write Multiple Registers -- accept as no-op (echo address + count)
    if (frame_len < 13) {
      this->send_exception_(c, txid, uid, fc, 0x03);
      return;
    }
    uint16_t addr = (c.buf[8] << 8) | c.buf[9];
    uint16_t count = (c.buf[10] << 8) | c.buf[11];
    ESP_LOGD(TAG, "FC16 write addr=0x%04X count=%u (ignored)", addr, count);
    uint8_t pdu[5] = {fc, c.buf[8], c.buf[9], c.buf[10], c.buf[11]};
    this->send_response_(c, txid, uid, pdu, 5);

  } else {
    ESP_LOGW(TAG, "FC%02X unsupported", fc);
    this->send_exception_(c, txid, uid, fc, 0x01);  // Illegal Function
  }
}

void GridMeterComponent::send_response_(Client &c, uint16_t txid, uint8_t uid, const uint8_t *pdu, uint8_t pdu_len) {
  uint8_t frame[7 + 125 * 2];
  frame[0] = txid >> 8;
  frame[1] = txid & 0xFF;
  frame[2] = 0x00;
  frame[3] = 0x00;
  frame[4] = static_cast<uint8_t>((1 + pdu_len) >> 8);
  frame[5] = static_cast<uint8_t>((1 + pdu_len) & 0xFF);
  frame[6] = uid;
  memcpy(frame + 7, pdu, pdu_len);
  int total = 7 + pdu_len;
  int sent = ::send(c.fd, frame, total, 0);
  // EAGAIN: socket buffer full — drop this response and keep the connection;
  // the client re-polls within a second, so losing one reply is harmless
  if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    return;
  if (sent != total) {
    ESP_LOGW(TAG, "send() short-write (fd=%d, expected=%d, got=%d), closing", c.fd, total, sent);
    this->close_client_(c);
  }
}

void GridMeterComponent::send_exception_(Client &c, uint16_t txid, uint8_t uid, uint8_t fc, uint8_t code) {
  uint8_t pdu[2] = {static_cast<uint8_t>(fc | 0x80), code};
  this->send_response_(c, txid, uid, pdu, 2);
}

}  // namespace esphome::grid_meter
