#include "lin_bus.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

namespace esphome::bekant {

static constexpr uint32_t LIN_BAUD = 19200;
static constexpr int64_t BIT_US = 1000000 / LIN_BAUD;
// LIN has no UART break primitive, so send 0x00 at half the bit rate: start bit + 8 zero bits
// at 9600 baud keep the bus dominant for 18 LIN bit times (spec: >= 13), and the stop bit
// becomes a 2 bit-time break delimiter.
static constexpr uint32_t BREAK_BAUD = LIN_BAUD / 2;
static constexpr uint8_t SYNC = 0x55;
// Diagnostic frames use the classic checksum (data only), all others the enhanced one (PID + data).
static constexpr uint8_t ID_MASTER_REQUEST = 0x3C;
static constexpr uint8_t ID_SLAVE_RESPONSE = 0x3D;
static constexpr int RX_BUFFER_SIZE = 256;  // must exceed the 128-byte hardware FIFO

bool LinBus::begin(uart_port_t port, int tx_pin, int rx_pin) {
  this->port_ = port;

  uart_config_t config{};
  config.baud_rate = LIN_BAUD;
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;

  if (uart_param_config(port, &config) != ESP_OK)
    return false;
  if (uart_set_pin(port, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
    return false;
  if (uart_driver_install(port, RX_BUFFER_SIZE, 0, 0, nullptr, 0) != ESP_OK)
    return false;

  // Hand every received byte to the driver immediately. The default idle timeout waits ~10 byte
  // times before moving data out of the FIFO, which is longer than a whole 5 ms LIN slot.
  uart_set_rx_full_threshold(port, 1);
  uart_set_rx_timeout(port, 1);
  return true;
}

void LinBus::send(uint8_t id, const uint8_t *data, size_t len) {
  const uint8_t pid = protected_id(id);
  this->send_header_(pid);
  if (len > 0)
    uart_write_bytes(this->port_, data, len);
  const uint8_t cs = checksum(id, pid, data, len);
  uart_write_bytes(this->port_, &cs, 1);
  uart_wait_tx_done(this->port_, pdMS_TO_TICKS(10));
}

LinResult LinBus::request(uint8_t id, uint8_t *data, size_t len) {
  const uint8_t pid = protected_id(id);
  this->send_header_(pid);
  uart_wait_tx_done(this->port_, pdMS_TO_TICKS(5));

  // A response may take 1.4x its nominal length (LIN 2.x); add 1 ms of scheduling slack.
  const int64_t response_us = 14 * 10 * static_cast<int64_t>(len + 1) * BIT_US / 10;
  const int64_t deadline = esp_timer_get_time() + response_us + 1000;

  // Skip the echoed break and look for our own sync + PID.
  int b;
  do {
    b = this->read_byte_(deadline);
  } while (b >= 0 && b != SYNC);
  if (b < 0 || this->read_byte_(deadline) != pid)
    return LinResult::NO_ECHO;

  for (size_t i = 0; i < len; i++) {
    b = this->read_byte_(deadline);
    if (b < 0)
      return i == 0 ? LinResult::NO_RESPONSE : LinResult::SHORT_RESPONSE;
    data[i] = static_cast<uint8_t>(b);
  }
  b = this->read_byte_(deadline);
  if (b < 0)
    return LinResult::SHORT_RESPONSE;
  if (b != checksum(id, pid, data, len))
    return LinResult::BAD_CHECKSUM;
  return LinResult::OK;
}

uint8_t LinBus::protected_id(uint8_t id) {
  id &= 0x3F;
  auto bit = [id](int n) { return (id >> n) & 1; };
  const uint8_t p0 = bit(0) ^ bit(1) ^ bit(2) ^ bit(4);
  const uint8_t p1 = (bit(1) ^ bit(3) ^ bit(4) ^ bit(5)) ^ 1;
  return id | (p0 << 6) | (p1 << 7);
}

uint8_t LinBus::checksum(uint8_t id, uint8_t pid, const uint8_t *data, size_t len) {
  uint16_t sum = (id == ID_MASTER_REQUEST || id == ID_SLAVE_RESPONSE) ? 0 : pid;
  for (size_t i = 0; i < len; i++) {
    sum += data[i];
    if (sum > 0xFF)
      sum -= 0xFF;  // add the carry back in
  }
  return static_cast<uint8_t>(~sum);
}

void LinBus::send_header_(uint8_t pid) {
  uart_flush_input(this->port_);  // drop leftovers (echo) of the previous frame

  uart_set_baudrate(this->port_, BREAK_BAUD);
  const uint8_t brk = 0x00;
  uart_write_bytes(this->port_, &brk, 1);
  uart_wait_tx_done(this->port_, pdMS_TO_TICKS(5));
  uart_set_baudrate(this->port_, LIN_BAUD);

  const uint8_t header[2] = {SYNC, pid};
  uart_write_bytes(this->port_, header, sizeof(header));
}

int LinBus::read_byte_(int64_t deadline_us) {
  const int64_t remaining_us = deadline_us - esp_timer_get_time();
  // Past the deadline still take a byte that is already buffered, just don't wait for one.
  TickType_t ticks = 0;
  if (remaining_us > 0) {
    ticks = pdMS_TO_TICKS((remaining_us + 999) / 1000);
    if (ticks == 0)
      ticks = 1;
  }
  uint8_t b;
  return uart_read_bytes(this->port_, &b, 1, ticks) == 1 ? b : -1;
}

}  // namespace esphome::bekant
