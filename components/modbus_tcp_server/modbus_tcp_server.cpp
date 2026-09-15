#include "modbus_tcp_server.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include <cstring>
#endif

namespace esphome {
namespace modbus_tcp_server {

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void ModbusTCPServer::setup() {
  ESP_LOGI(TAG, "Starting Modbus TCP server on port %u, unit ID %u, %u coils",
           this->port_, this->unit_id_, MAX_COILS);

#ifdef USE_ESP32
  this->server_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (this->server_fd_ < 0) {
    ESP_LOGE(TAG, "Failed to create TCP socket");
    this->mark_failed();
    return;
  }

  int reuse = 1;
  setsockopt(this->server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in server_address {};
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_port = htons(this->port_);

  if (bind(this->server_fd_, reinterpret_cast<struct sockaddr *>(&server_address),
           sizeof(server_address)) < 0) {
    ESP_LOGE(TAG, "Failed to bind TCP port %u, errno=%d", this->port_, errno);
    close(this->server_fd_);
    this->server_fd_ = -1;
    this->mark_failed();
    return;
  }

  if (listen(this->server_fd_, 1) < 0) {
    ESP_LOGE(TAG, "Failed to listen on TCP port %u, errno=%d", this->port_, errno);
    close(this->server_fd_);
    this->server_fd_ = -1;
    this->mark_failed();
    return;
  }

  int flags = fcntl(this->server_fd_, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(this->server_fd_, F_SETFL, flags | O_NONBLOCK);
  }

  this->rx_buffer_len_ = 0;
  ESP_LOGI(TAG, "Modbus TCP server listening on port %u", this->port_);
#endif
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

void ModbusTCPServer::loop() {
#ifdef USE_ESP32
  if (this->server_fd_ < 0)
    return;

  // Accept new client if none is connected
  if (this->client_fd_ < 0) {
    struct sockaddr_in client_address {};
    socklen_t client_length = sizeof(client_address);

    int new_client = accept(this->server_fd_,
                            reinterpret_cast<struct sockaddr *>(&client_address),
                            &client_length);

    if (new_client >= 0) {
      this->client_fd_ = new_client;
      this->rx_buffer_len_ = 0;

      int flags = fcntl(this->client_fd_, F_GETFL, 0);
      if (flags >= 0) {
        fcntl(this->client_fd_, F_SETFL, flags | O_NONBLOCK);
      }

      ESP_LOGI(TAG, "Modbus TCP client connected");
    }
  }

  // Receive data
  if (this->client_fd_ >= 0) {
    uint8_t temp_buffer[256];
    int len = recv(this->client_fd_, temp_buffer, sizeof(temp_buffer), 0);

    if (len > 0) {
      if (this->rx_buffer_len_ + static_cast<size_t>(len) > RX_BUFFER_SIZE) {
        ESP_LOGW(TAG, "RX buffer overflow - dropping client");
        this->close_client_();
        return;
      }

      memcpy(&this->rx_buffer_[this->rx_buffer_len_], temp_buffer, len);
      this->rx_buffer_len_ += len;

      this->process_rx_buffer_();

    } else if (len == 0) {
      ESP_LOGI(TAG, "Modbus TCP client disconnected");
      this->close_client_();

    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGW(TAG, "Modbus TCP receive error: %d", errno);
        this->close_client_();
      }
    }
  }
#endif
}

#ifdef USE_ESP32

// ---------------------------------------------------------------------------
// Frame processing
// ---------------------------------------------------------------------------

void ModbusTCPServer::process_rx_buffer_() {
  // MBAP header = 7 bytes, Length field = Unit ID + PDU
  while (this->rx_buffer_len_ >= 7) {
    uint16_t protocol_id = (static_cast<uint16_t>(this->rx_buffer_[2]) << 8) | this->rx_buffer_[3];
    uint16_t length = (static_cast<uint16_t>(this->rx_buffer_[4]) << 8) | this->rx_buffer_[5];

    if (protocol_id != 0) {
      ESP_LOGW(TAG, "Invalid Modbus protocol ID: %u", protocol_id);
      this->close_client_();
      return;
    }

    if (length < 2) {
      ESP_LOGW(TAG, "Invalid Modbus MBAP length: %u", length);
      this->close_client_();
      return;
    }

    if (length > 254) {
      ESP_LOGW(TAG, "Modbus frame too large: %u", length);
      this->close_client_();
      return;
    }

    size_t frame_length = 6 + static_cast<size_t>(length);

    if (this->rx_buffer_len_ < frame_length)
      return;  // incomplete frame

    this->handle_complete_frame_(this->rx_buffer_, frame_length);

    // Remove processed frame
    size_t remaining = this->rx_buffer_len_ - frame_length;
    if (remaining > 0) {
      memmove(this->rx_buffer_, &this->rx_buffer_[frame_length], remaining);
    }
    this->rx_buffer_len_ = remaining;
  }
}

void ModbusTCPServer::handle_complete_frame_(uint8_t *buffer, size_t len) {
  // Only log non-FC01 frames (FC01 is the normal polling traffic)
  if (len >= 8 && buffer[7] != 0x01) {
    ESP_LOGI(TAG, "========== NON-FC01 MODBUS FRAME ==========");
    ESP_LOGI(TAG, "Function: 0x%02X | Length: %u bytes", buffer[7], static_cast<unsigned>(len));

    char hex[260 * 3 + 1];
    size_t pos = 0;
    for (size_t i = 0; i < len && pos < sizeof(hex) - 4; i++) {
      pos += snprintf(&hex[pos], sizeof(hex) - pos, "%02X ", buffer[i]);
    }
    hex[pos] = '\0';

    ESP_LOGI(TAG, "RAW: %s", hex);
    ESP_LOGI(TAG, "============================================");
  }

  this->handle_request_(buffer, len);
}

bool ModbusTCPServer::send_all_(const uint8_t *data, size_t len) {
  size_t sent_total = 0;

  while (sent_total < len) {
    int result = send(this->client_fd_, data + sent_total, len - sent_total, 0);

    if (result < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        delay(1);
        continue;
      }
      ESP_LOGW(TAG, "TCP send failed: errno=%d", errno);
      return false;
    }

    if (result == 0)
      return false;

    sent_total += result;
  }
  return true;
}

#endif  // USE_ESP32

// ---------------------------------------------------------------------------
// Coil registration / state
// ---------------------------------------------------------------------------

void ModbusTCPServer::register_coil(uint16_t address, binary_sensor::BinarySensor *sensor) {
  if (address >= MAX_COILS) {
    ESP_LOGW(TAG, "Ignoring binary_sensor coil %u - maximum is %u", address, MAX_COILS - 1);
    return;
  }
  this->coil_sensors_[address] = sensor;
  ESP_LOGI(TAG, "Registered coil %u (binary_sensor)", address);
}

void ModbusTCPServer::register_switch(uint16_t address, switch_::Switch *switch_) {
  if (address >= MAX_COILS) {
    ESP_LOGW(TAG, "Ignoring switch coil %u - maximum is %u", address, MAX_COILS - 1);
    return;
  }
  this->coil_switches_[address] = switch_;
  ESP_LOGI(TAG, "Registered writable coil %u (switch)", address);
}

void ModbusTCPServer::set_coil(uint16_t address, bool value) {
  if (address >= MAX_COILS)
    return;

  this->coils_[address] = value;

  if (this->coil_switches_[address] != nullptr) {
    if (value) {
      this->coil_switches_[address]->turn_on();
    } else {
      this->coil_switches_[address]->turn_off();
    }
  }

  if (this->coil_sensors_[address] != nullptr) {
    this->coil_sensors_[address]->publish_state(value);
  }

  ESP_LOGD(TAG, "Coil %u set to %s", address, value ? "ON" : "OFF");
}

// ---------------------------------------------------------------------------
// Request handling
// ---------------------------------------------------------------------------

void ModbusTCPServer::handle_request_(uint8_t *buffer, size_t len) {
  if (len < 8)
    return;

  uint16_t transaction_id = (static_cast<uint16_t>(buffer[0]) << 8) | buffer[1];
  uint16_t protocol_id = (static_cast<uint16_t>(buffer[2]) << 8) | buffer[3];
  uint16_t mbap_length = (static_cast<uint16_t>(buffer[4]) << 8) | buffer[5];
  uint8_t unit_id = buffer[6];
  uint8_t function = buffer[7];

  if (protocol_id != 0)
    return;
  if (unit_id != this->unit_id_)
    return;

  // -----------------------------------------------------------------------
  // FC01 - Read Coils
  // -----------------------------------------------------------------------
  if (function == 0x01) {
    if (len != 12) {
      ESP_LOGW(TAG, "FC01 invalid length: %u", static_cast<unsigned>(len));
      this->send_exception_(buffer, function, 0x03);
      return;
    }

    uint16_t start = (static_cast<uint16_t>(buffer[8]) << 8) | buffer[9];
    uint16_t quantity = (static_cast<uint16_t>(buffer[10]) << 8) | buffer[11];

    if (quantity == 0 || quantity > MAX_COILS || start >= MAX_COILS ||
        start + quantity > MAX_COILS) {
      ESP_LOGW(TAG, "FC01 illegal address: start=%u quantity=%u", start, quantity);
      this->send_exception_(buffer, function, 0x02);
      return;
    }

    uint8_t byte_count = (quantity + 7) / 8;
    uint8_t response[2 + ((MAX_COILS + 7) / 8)]{};
    response[0] = 0x01;
    response[1] = byte_count;

    for (uint16_t i = 0; i < quantity; i++) {
      bool value = false;
      if (this->coil_sensors_[start + i] != nullptr) {
        value = this->coil_sensors_[start + i]->state;
      } else {
        value = this->coils_[start + i];
      }

      if (value) {
        uint8_t byte_index = i / 8;
        uint8_t bit_index = i % 8;
        response[2 + byte_index] |= (1 << bit_index);
      }
    }

    this->send_response_(buffer, response, 2 + byte_count);
    return;
  }

  // -----------------------------------------------------------------------
  // FC05 - Write Single Coil
  // -----------------------------------------------------------------------
  if (function == 0x05) {
    if (len != 12) {
      ESP_LOGW(TAG, "FC05 invalid length: %u", static_cast<unsigned>(len));
      this->send_exception_(buffer, function, 0x03);
      return;
    }

    uint16_t address = (static_cast<uint16_t>(buffer[8]) << 8) | buffer[9];
    uint16_t value = (static_cast<uint16_t>(buffer[10]) << 8) | buffer[11];

    ESP_LOGD(TAG, "FC05 Write Single Coil: address=%u value=0x%04X", address, value);

    if (address >= MAX_COILS) {
      ESP_LOGW(TAG, "FC05 illegal address: %u", address);
      this->send_exception_(buffer, function, 0x02);
      return;
    }

    if (value != 0x0000 && value != 0xFF00) {
      ESP_LOGW(TAG, "FC05 illegal value: 0x%04X", value);
      this->send_exception_(buffer, function, 0x03);
      return;
    }

    bool state = (value == 0xFF00);
    this->set_coil(address, state);

    // Echo the request PDU
    uint8_t response[] = {0x05, buffer[8], buffer[9], buffer[10], buffer[11]};
    this->send_response_(buffer, response, sizeof(response));
    return;
  }

  // -----------------------------------------------------------------------
  // FC15 - Write Multiple Coils
  // -----------------------------------------------------------------------
  if (function == 0x0F) {
    if (len < 13) {
      ESP_LOGW(TAG, "FC15 frame too short: %u bytes", static_cast<unsigned>(len));
      this->send_exception_(buffer, function, 0x03);
      return;
    }

    uint16_t start = (static_cast<uint16_t>(buffer[8]) << 8) | buffer[9];
    uint16_t quantity = (static_cast<uint16_t>(buffer[10]) << 8) | buffer[11];
    uint8_t byte_count = buffer[12];

    ESP_LOGD(TAG, "FC15 Write Multiple Coils: start=%u quantity=%u byte_count=%u",
             start, quantity, byte_count);

    if (quantity == 0 || quantity > MAX_COILS || start >= MAX_COILS ||
        start + quantity > MAX_COILS) {
      ESP_LOGW(TAG, "FC15 illegal address: start=%u quantity=%u", start, quantity);
      this->send_exception_(buffer, function, 0x02);
      return;
    }

    uint8_t expected_byte_count = (quantity + 7) / 8;
    if (byte_count != expected_byte_count) {
      ESP_LOGW(TAG, "FC15 invalid byte count: got=%u expected=%u", byte_count, expected_byte_count);
      this->send_exception_(buffer, function, 0x03);
      return;
    }

    size_t expected_frame_length = 13 + static_cast<size_t>(byte_count);
    if (len != expected_frame_length) {
      ESP_LOGW(TAG, "FC15 invalid frame length: got=%u expected=%u",
               static_cast<unsigned>(len), static_cast<unsigned>(expected_frame_length));
      this->send_exception_(buffer, function, 0x03);
      return;
    }

    for (uint16_t i = 0; i < quantity; i++) {
      uint8_t byte_index = i / 8;
      uint8_t bit_index = i % 8;
      bool state = (buffer[13 + byte_index] & (1 << bit_index)) != 0;

      ESP_LOGD(TAG, "FC15 coil %u -> %s", start + i, state ? "ON" : "OFF");
      this->set_coil(start + i, state);
    }

    uint8_t response[] = {0x0F, buffer[8], buffer[9], buffer[10], buffer[11]};
    this->send_response_(buffer, response, sizeof(response));
    return;
  }

  // -----------------------------------------------------------------------
  // Unsupported function
  // -----------------------------------------------------------------------
  ESP_LOGW(TAG, "Unsupported Modbus function: 0x%02X", function);
  this->send_exception_(buffer, function, 0x01);
}

// ---------------------------------------------------------------------------
// Response helpers
// ---------------------------------------------------------------------------

void ModbusTCPServer::send_response_(uint8_t *request, uint8_t *response_pdu, size_t pdu_len) {
#ifdef USE_ESP32
  if (this->client_fd_ < 0)
    return;

  uint8_t response[260]{};

  // MBAP
  response[0] = request[0];  // Transaction ID
  response[1] = request[1];
  response[2] = 0;           // Protocol ID
  response[3] = 0;

  uint16_t length = 1 + static_cast<uint16_t>(pdu_len);
  response[4] = static_cast<uint8_t>(length >> 8);
  response[5] = static_cast<uint8_t>(length & 0xFF);
  response[6] = this->unit_id_;

  memcpy(&response[7], response_pdu, pdu_len);
  size_t total_length = 7 + pdu_len;

  // Debug TX
  char hex[260 * 3 + 1];
  size_t pos = 0;
  for (size_t i = 0; i < total_length && pos < sizeof(hex) - 4; i++) {
    pos += snprintf(&hex[pos], sizeof(hex) - pos, "%02X ", response[i]);
  }
  hex[pos] = '\0';

  ESP_LOGD(TAG, "TCP TX (%u bytes): %s", static_cast<unsigned>(total_length), hex);

  if (!this->send_all_(response, total_length)) {
    ESP_LOGW(TAG, "Failed to send Modbus TCP response");
    this->close_client_();
  } else {
    ESP_LOGD(TAG, "Modbus TCP response sent");
  }
#endif
}

void ModbusTCPServer::send_exception_(uint8_t *request, uint8_t function, uint8_t exception_code) {
  uint8_t response[] = {
      static_cast<uint8_t>(function | 0x80),
      exception_code
  };

  ESP_LOGW(TAG, "Modbus exception: function=0x%02X code=0x%02X", function, exception_code);
  this->send_response_(request, response, sizeof(response));
}

#ifdef USE_ESP32

void ModbusTCPServer::close_client_() {
  if (this->client_fd_ >= 0) {
    close(this->client_fd_);
    this->client_fd_ = -1;
  }
  this->rx_buffer_len_ = 0;
}

void ModbusTCPServer::close_server_() {
  if (this->server_fd_ >= 0) {
    close(this->server_fd_);
    this->server_fd_ = -1;
  }
}

#endif

}  // namespace modbus_tcp_server
}  // namespace esphome
