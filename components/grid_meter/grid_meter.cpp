#include "grid_meter.h"

#include <cstring>
#include <cmath>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <algorithm>

#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace grid_meter {

const char *const TAG = "grid_meter";

// ---------- static helpers ----------

// Write signed int32 as two little-endian uint16 registers (Reg_s32l: low word at idx)
void GridMeterComponent::write_int32_(uint16_t *regs, uint8_t idx, int32_t val) {
  regs[idx]     = (uint16_t)((uint32_t)val & 0xFFFF);  // low word first
  regs[idx + 1] = (uint16_t)((uint32_t)val >> 16);     // high word second
}

// Sparse lookup for EM24 registers. Returns 0 for any unknown or unimplemented address.
uint16_t GridMeterComponent::get_register_(const uint16_t *regs, uint16_t addr) {
  if (addr < REG_COUNT) return regs[addr];
  if (addr == 0x0302) return 0x0100;  // HW version 1.0.0
  if (addr == 0x0304) return 0x0100;  // FW version 1.0.0
  if (addr == 0x1002) return 3;       // PhaseConfig = 1P (single phase)
  if (addr == 0xa000) return 7;       // Application = H mode
  if (addr == 0xa100) return 2;       // SwitchPos = '1' (active kWh, both directions)
  return 0;
}

// ---------- lifecycle ----------

void GridMeterComponent::setup() {
  // 1. Validate required sensor pointers
  if (!power_import_ || !power_export_ || !voltage_ || !current_ ||
      !energy_import_t1_ || !energy_import_t2_ ||
      !energy_export_t1_ || !energy_export_t2_) {
    ESP_LOGE(TAG, "One or more required sensors are not configured");
    mark_failed();
    return;
  }

  // 2. Initialise register bank to zero, then set constant fields
  memset(registers_, 0, sizeof(registers_));
  registers_[0x000B] = DEVICE_ID_EM24;  // Model ID register (probed by carlo_gavazzi.py)
  registers_[0x0033] = 500;             // Frequency: 50.0 Hz (Reg_u16, ÷10 Hz)

  // 3. Open non-blocking TCP socket on port 502
  server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    ESP_LOGE(TAG, "socket() failed: %d", errno);
    mark_failed();
    return;
  }

  int opt = 1;
  ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port        = htons(502);

  if (::bind(server_fd_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    ESP_LOGE(TAG, "bind() failed on port 502: %d", errno);
    ::close(server_fd_);
    server_fd_ = -1;
    mark_failed();
    return;
  }

  if (::listen(server_fd_, 2) < 0) {
    ESP_LOGE(TAG, "listen() failed: %d", errno);
    ::close(server_fd_);
    server_fd_ = -1;
    mark_failed();
    return;
  }

  int flags = ::fcntl(server_fd_, F_GETFL, 0);
  ::fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);

  ESP_LOGI(TAG, "Grid meter Modbus TCP server listening on port 502 (EM24 emulation)");
}

void GridMeterComponent::loop() {
  if (server_fd_ < 0) return;

  accept_clients_();
  refresh_sensors_();

  for (auto &c : clients_) {
    if (c.fd >= 0) process_client_(c);
  }
}

// ---------- sensor refresh ----------

void GridMeterComponent::refresh_sensors_() {
  // L1 Voltage (Reg_s32l, ÷10 V) at 0x0000-0x0001 -- hold last good on NaN
  float v = voltage_->get_state();
  if (!std::isnan(v)) {
    int32_t v_raw = (int32_t)(v * 10.0f + 0.5f);
    voltage_shadow_[0] = (uint16_t)((uint32_t)v_raw & 0xFFFF);  // low word
    voltage_shadow_[1] = (uint16_t)((uint32_t)v_raw >> 16);     // high word
  }
  registers_[0x0000] = voltage_shadow_[0];
  registers_[0x0001] = voltage_shadow_[1];

  // L1 Current (Reg_s32l, ÷1000 A) at 0x000C-0x000D -- hold last good on NaN, always positive magnitude
  float i = current_->get_state();
  if (!std::isnan(i)) {
    int32_t i_raw = (int32_t)(std::abs(i) * 1000.0f + 0.5f);
    current_shadow_[0] = (uint16_t)((uint32_t)i_raw & 0xFFFF);  // low word
    current_shadow_[1] = (uint16_t)((uint32_t)i_raw >> 16);     // high word
  }
  registers_[0x000C] = current_shadow_[0];
  registers_[0x000D] = current_shadow_[1];

  // Net power (Reg_s32l, ÷10 W) -- positive = import, negative = export; zero on NaN
  // Written to 0x0012-0x0013 (L1 power) and 0x0028-0x0029 (total power — same for single phase)
  float imp = power_import_->get_state();
  float exp_pwr = power_export_->get_state();
  if (!std::isnan(imp) && !std::isnan(exp_pwr)) {
    float net = imp - exp_pwr;
    int32_t pwr_raw = (int32_t)(net * 10.0f + (net >= 0 ? 0.5f : -0.5f));
    write_int32_(registers_, 0x12, pwr_raw);
    write_int32_(registers_, 0x28, pwr_raw);
  } else {
    write_int32_(registers_, 0x12, 0);
    write_int32_(registers_, 0x28, 0);
  }

  // Energy import total (Reg_s32l, ÷10 kWh) at 0x0034-0x0035 (total) and 0x0040-0x0041 (L1)
  float ei1 = energy_import_t1_->get_state();
  float ei2 = energy_import_t2_->get_state();
  if (!std::isnan(ei1) && !std::isnan(ei2)) {
    double kwh = (double)ei1 + (double)ei2;
    double raw = kwh * 10.0;
    if (raw > (double)INT32_MAX) {
      ESP_LOGW(TAG, "Energy import value %.1f kWh exceeds INT32_MAX, clamping", kwh);
      raw = (double)INT32_MAX;
    }
    raw = std::max(0.0, raw);
    int32_t ei_raw = (int32_t)raw;
    write_int32_(registers_, 0x34, ei_raw);
    write_int32_(registers_, 0x40, ei_raw);  // L1 energy forward = total (single phase)
  } else {
    write_int32_(registers_, 0x34, 0);
    write_int32_(registers_, 0x40, 0);
  }

  // Energy export total (Reg_s32l, ÷10 kWh) at 0x004E-0x004F
  float ee1 = energy_export_t1_->get_state();
  float ee2 = energy_export_t2_->get_state();
  if (!std::isnan(ee1) && !std::isnan(ee2)) {
    double kwh = (double)ee1 + (double)ee2;
    double raw = kwh * 10.0;
    if (raw > (double)INT32_MAX) {
      ESP_LOGW(TAG, "Energy export value %.1f kWh exceeds INT32_MAX, clamping", kwh);
      raw = (double)INT32_MAX;
    }
    raw = std::max(0.0, raw);
    write_int32_(registers_, 0x4E, (int32_t)raw);
  } else {
    write_int32_(registers_, 0x4E, 0);
  }
}

// ---------- TCP server ----------

void GridMeterComponent::accept_clients_() {
  struct sockaddr_in client_addr{};
  socklen_t addr_len = sizeof(client_addr);
  int fd;
  while ((fd = ::accept(server_fd_, (struct sockaddr *)&client_addr, &addr_len)) >= 0) {
    bool accepted = false;
    for (auto &c : clients_) {
      if (c.fd < 0) {
        int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        c.fd           = fd;
        c.buf_len      = 0;
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
    c.fd      = -1;
    c.buf_len = 0;
  }
}

void GridMeterComponent::process_client_(Client &c) {
  // Timeout check
  if (millis() - c.last_recv_ms > CLIENT_TIMEOUT_MS) {
    ESP_LOGD(TAG, "Client timeout (fd=%d)", c.fd);
    close_client_(c);
    return;
  }

  // Read into buffer
  int n = ::recv(c.fd, c.buf + c.buf_len, MAX_BUF - c.buf_len, 0);
  if (n == 0) { close_client_(c); return; }
  if (n < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) close_client_(c);
    return;
  }
  c.buf_len     += (uint16_t)n;
  c.last_recv_ms = millis();

  // Buffer overflow: no complete frame in 260 bytes
  if (c.buf_len >= MAX_BUF) {
    ESP_LOGD(TAG, "Client buffer overflow (fd=%d), closing", c.fd);
    close_client_(c);
    return;
  }

  // Process all complete frames in buffer
  while (c.buf_len >= 6) {
    uint16_t proto_id   = (c.buf[2] << 8) | c.buf[3];
    uint16_t pdu_length = (c.buf[4] << 8) | c.buf[5];

    if (proto_id != 0x0000) {
      ESP_LOGD(TAG, "Invalid protocol ID %04X, closing", proto_id);
      close_client_(c);
      return;
    }

    // pdu_length must include at least unit-id + function-code bytes
    if (pdu_length < 2) {
      ESP_LOGD(TAG, "PDU length %u too short, closing", pdu_length);
      close_client_(c);
      return;
    }

    uint16_t frame_len = 6 + pdu_length;
    if (frame_len > MAX_BUF) {
      ESP_LOGD(TAG, "Frame too large (%u bytes), closing", frame_len);
      close_client_(c);
      return;
    }

    if (c.buf_len < frame_len) break;  // incomplete frame

    handle_frame_(c, frame_len);

    c.buf_len -= frame_len;
    if (c.buf_len > 0) memmove(c.buf, c.buf + frame_len, c.buf_len);
  }
}

void GridMeterComponent::handle_frame_(Client &c, uint16_t frame_len) {
  uint16_t txid = (c.buf[0] << 8) | c.buf[1];
  uint8_t  uid  = c.buf[6];
  uint8_t  fc   = c.buf[7];

  if (fc == 0x03 || fc == 0x04) {
    // FC03 / FC04: Read Holding/Input Registers
    if (frame_len < 12) { send_exception_(c.fd, txid, uid, fc, 0x03); return; }
    uint16_t start = (c.buf[8]  << 8) | c.buf[9];
    uint16_t count = (c.buf[10] << 8) | c.buf[11];
    ESP_LOGI(TAG, "FC%02X start=0x%04X count=%u", fc, start, count);

    if (count == 0 || count > 125) {
      send_exception_(c.fd, txid, uid, fc, 0x03);  // Illegal Data Value
      return;
    }

    // Build response using sparse register lookup -- returns 0 for any unknown address
    uint8_t pdu[2 + 125 * 2];
    pdu[0] = fc;
    pdu[1] = (uint8_t)(count * 2);
    for (uint16_t i = 0; i < count; i++) {
      uint16_t addr = (uint16_t)((uint32_t)start + i);
      uint16_t val  = get_register_(registers_, addr);
      pdu[2 + i * 2]     = val >> 8;
      pdu[2 + i * 2 + 1] = val & 0xFF;
    }
    send_response_(c.fd, txid, uid, pdu, (uint8_t)(2 + count * 2));

  } else if (fc == 0x06) {
    // FC06: Write Single Register -- accept as no-op (echo request back)
    if (frame_len < 12) { send_exception_(c.fd, txid, uid, fc, 0x03); return; }
    uint16_t addr = (c.buf[8] << 8) | c.buf[9];
    uint16_t val  = (c.buf[10] << 8) | c.buf[11];
    ESP_LOGI(TAG, "FC06 write addr=0x%04X val=0x%04X (ignored)", addr, val);
    uint8_t pdu[5] = { fc, c.buf[8], c.buf[9], c.buf[10], c.buf[11] };
    send_response_(c.fd, txid, uid, pdu, 5);

  } else if (fc == 0x10) {
    // FC16: Write Multiple Registers -- accept as no-op (echo address + count)
    if (frame_len < 13) { send_exception_(c.fd, txid, uid, fc, 0x03); return; }
    uint16_t addr  = (c.buf[8]  << 8) | c.buf[9];
    uint16_t count = (c.buf[10] << 8) | c.buf[11];
    ESP_LOGI(TAG, "FC16 write addr=0x%04X count=%u (ignored)", addr, count);
    uint8_t pdu[5] = { fc, c.buf[8], c.buf[9], c.buf[10], c.buf[11] };
    send_response_(c.fd, txid, uid, pdu, 5);

  } else {
    ESP_LOGW(TAG, "FC%02X unsupported", fc);
    send_exception_(c.fd, txid, uid, fc, 0x01);  // Illegal Function
  }
}

void GridMeterComponent::send_response_(int fd, uint16_t txid, uint8_t uid,
                                         const uint8_t *pdu, uint8_t pdu_len) {
  uint8_t frame[7 + 125 * 2];
  frame[0] = txid >> 8;   frame[1] = txid & 0xFF;
  frame[2] = 0x00;        frame[3] = 0x00;
  frame[4] = (uint8_t)((1 + pdu_len) >> 8);
  frame[5] = (uint8_t)((1 + pdu_len) & 0xFF);
  frame[6] = uid;
  memcpy(frame + 7, pdu, pdu_len);
  int total = 7 + pdu_len;
  int sent = ::send(fd, frame, total, 0);
  if (sent != total) {
    ESP_LOGW(TAG, "send() short-write (fd=%d, expected=%d, got=%d), closing", fd, total, sent);
    ::close(fd);
  }
}

void GridMeterComponent::send_exception_(int fd, uint16_t txid, uint8_t uid,
                                          uint8_t fc, uint8_t code) {
  uint8_t pdu[2] = { (uint8_t)(fc | 0x80), code };
  send_response_(fd, txid, uid, pdu, 2);
}

}  // namespace grid_meter
}  // namespace esphome
