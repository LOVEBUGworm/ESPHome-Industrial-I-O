#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"

#ifdef USE_ESP32
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#endif

namespace esphome {
namespace modbus_tcp_server {

static const char *const TAG = "modbus_tcp_server";

class ModbusTCPServer : public Component {
 public:
  void set_port(uint16_t port) { this->port_ = port; }
  void set_unit_id(uint8_t unit_id) { this->unit_id_ = unit_id; }

  void setup() override;
  void loop() override;

  float get_setup_priority() const override {
    return setup_priority::AFTER_WIFI;
  }

  void set_coil(uint16_t address, bool value);

  void register_coil(uint16_t address, binary_sensor::BinarySensor *sensor);
  void register_switch(uint16_t address, switch_::Switch *switch_);

 protected:
  uint16_t port_{502};
  uint8_t unit_id_{1};

  // Coils 0–10 (11 total)
  static const uint16_t MAX_COILS = 11;

  bool coils_[MAX_COILS]{};
  binary_sensor::BinarySensor *coil_sensors_[MAX_COILS]{};
  switch_::Switch *coil_switches_[MAX_COILS]{};

#ifdef USE_ESP32
  int server_fd_{-1};
  int client_fd_{-1};

  static const size_t RX_BUFFER_SIZE = 512;
  uint8_t rx_buffer_[RX_BUFFER_SIZE]{};
  size_t rx_buffer_len_{0};

  void close_client_();
  void close_server_();
  void process_rx_buffer_();
  void handle_complete_frame_(uint8_t *buffer, size_t len);
  bool send_all_(const uint8_t *data, size_t len);
#endif

  void handle_request_(uint8_t *buffer, size_t len);
  void send_response_(uint8_t *request, uint8_t *response_pdu, size_t pdu_len);
  void send_exception_(uint8_t *request, uint8_t function, uint8_t exception_code);
};

}  // namespace modbus_tcp_server
}  // namespace esphome
