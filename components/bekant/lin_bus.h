#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/uart.h"

namespace esphome::bekant {

enum class LinResult : uint8_t {
  OK,
  NO_ECHO,         ///< our own header never came back: transceiver or wiring problem
  NO_RESPONSE,     ///< header went out, no slave answered
  SHORT_RESPONSE,  ///< slave stopped mid-frame
  BAD_CHECKSUM,
};

/// Minimal LIN master on an ESP-IDF UART behind a LIN transceiver (MCP2003B).
///
/// The bus is half duplex: everything we transmit comes back on RX, which `request()` uses to
/// verify the header actually made it onto the bus.
class LinBus {
 public:
  bool begin(uart_port_t port, int tx_pin, int rx_pin);

  /// Master-to-slave frame: break, sync, PID, data, checksum.
  void send(uint8_t id, const uint8_t *data, size_t len);
  /// Master header only; a slave fills in `len` data bytes plus checksum.
  LinResult request(uint8_t id, uint8_t *data, size_t len);

  static uint8_t protected_id(uint8_t id);
  static uint8_t checksum(uint8_t id, uint8_t pid, const uint8_t *data, size_t len);

 protected:
  void send_header_(uint8_t pid);
  int read_byte_(int64_t deadline_us);

  uart_port_t port_{UART_NUM_1};
};

}  // namespace esphome::bekant
