#include "mirage.h"
#include "esphome/core/log.h"
#include "esphome/components/remote_base/mirage_protocol.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace mirage {

static const char *const TAG = "mirage.climate";

MirageClimate::MirageClimate() {
  ESP_LOGI(TAG, "MirageClimate constructor called");
}

const uint8_t MIRAGE_STATE_LENGTH = 14;

// Modes
const uint8_t MIRAGE_HEAT = 0x10;
const uint8_t MIRAGE_COOL = 0x20;  
const uint8_t MIRAGE_DRY = 0x30;
const uint8_t MIRAGE_AUTO = 0x40;
const uint8_t MIRAGE_FAN = 0x50;

// Fan speeds
const uint8_t MIRAGE_FAN_AUTO = 0;
const uint8_t MIRAGE_FAN_HIGH = 1;
const uint8_t MIRAGE_FAN_MED = 3;
const uint8_t MIRAGE_FAN_LOW = 2;

// Swing modes (byte 5)
const uint8_t MIRAGE_SWING_OFF = 0x00;
const uint8_t MIRAGE_SWING_HORIZONTAL = 0x01;
const uint8_t MIRAGE_SWING_VERTICAL = 0x02;
const uint8_t MIRAGE_SWING_BOTH = 0x03;

// Power
const uint8_t MIRAGE_POWER_OFF = 0xC0;  // Top two bits (0xC0-0xC3)
const uint8_t MIRAGE_TEMP_OFFSET = 0x5C;

// Presets
const uint8_t MIRAGE_SLEEP = 0x08;    // Byte 6
const uint8_t MIRAGE_BOOST = 0x80;    // Byte 8
const uint8_t MIRAGE_ECO = 0x10;      // Byte 5

void MirageClimate::transmit_state() {
  this->last_transmit_time_ = millis();
  
  // Update current temperature from sensor if available
  if (this->sensor_) {
    this->current_temperature = this->sensor_->state;
  }

  uint8_t remote_state[MIRAGE_STATE_LENGTH] = {0};
  
  // Header and temperature
  remote_state[0] = 0x56;
  remote_state[1] = MIRAGE_TEMP_OFFSET;

  // Power state
  if (this->mode == climate::CLIMATE_MODE_OFF) {
    remote_state[5] = (remote_state[5] & 0x03) | MIRAGE_POWER_OFF;  // Preserve swing bits
  } else {
    remote_state[5] &= ~MIRAGE_POWER_OFF;  // Clear power off bits
  }

  // Mode
  switch (this->mode) {
    case climate::CLIMATE_MODE_HEAT_COOL:
      remote_state[4] |= MIRAGE_AUTO;
      break;
    case climate::CLIMATE_MODE_HEAT:
      remote_state[4] |= MIRAGE_HEAT;
      break;
    case climate::CLIMATE_MODE_COOL:
      remote_state[4] |= MIRAGE_COOL;
      break; 
    case climate::CLIMATE_MODE_DRY:
      remote_state[4] |= MIRAGE_DRY;
      break;
    case climate::CLIMATE_MODE_FAN_ONLY:
      remote_state[4] |= MIRAGE_FAN;
      break;
    case climate::CLIMATE_MODE_OFF:
      // Handled above in power state
      break;
    default:
      break;
  }

  // Temperature
  auto temp = (uint8_t)roundf(clamp(this->target_temperature, 16.0f, 32.0f));
  remote_state[1] += temp;

  // Fan speed
  switch (this->fan_mode.value()) {
    case climate::CLIMATE_FAN_LOW:
      remote_state[4] |= MIRAGE_FAN_LOW;
      break;
    case climate::CLIMATE_FAN_MEDIUM:
      remote_state[4] |= MIRAGE_FAN_MED;
      break;
    case climate::CLIMATE_FAN_HIGH:
      remote_state[4] |= MIRAGE_FAN_HIGH;
      break;
    default:  // Auto
      remote_state[4] |= MIRAGE_FAN_AUTO;
      break;
  }

  // Swing mode (clear bits 0-1 first)
  remote_state[5] &= ~0x03;
  switch (this->swing_mode) {
    case climate::CLIMATE_SWING_OFF:
      remote_state[5] |= MIRAGE_SWING_OFF;
      break;
    case climate::CLIMATE_SWING_HORIZONTAL:
      remote_state[5] |= MIRAGE_SWING_HORIZONTAL;
      break;
    case climate::CLIMATE_SWING_VERTICAL:
      remote_state[5] |= MIRAGE_SWING_VERTICAL;
      break;
    case climate::CLIMATE_SWING_BOTH:
      remote_state[5] |= MIRAGE_SWING_BOTH;
      break;
  }

  // Presets
  switch (this->preset.value()) {
    case climate::CLIMATE_PRESET_SLEEP:
      remote_state[6] |= MIRAGE_SLEEP;
      break;
    case climate::CLIMATE_PRESET_BOOST:
      remote_state[8] |= MIRAGE_BOOST;
      break;
    case climate::CLIMATE_PRESET_ECO:
      remote_state[5] |= MIRAGE_ECO;
      break;
    default:  // None
      remote_state[6] &= ~MIRAGE_SLEEP;
      remote_state[8] &= ~MIRAGE_BOOST;
      remote_state[5] &= ~MIRAGE_ECO;
      break;
  }

  ESP_LOGI(TAG, "Sending: %02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X",
           remote_state[0], remote_state[1], remote_state[2], remote_state[3],
           remote_state[4], remote_state[5], remote_state[6], remote_state[7],
           remote_state[8], remote_state[9], remote_state[10], remote_state[11],
           remote_state[12], remote_state[13]);

  esphome::remote_base::MirageData in;
  for (uint8_t i = 0; i < MIRAGE_STATE_LENGTH; i++) {
    in.data.push_back(remote_state[i]);
  }
  
  auto transmit = this->transmitter_->transmit();
  auto *data = transmit.get_data();
  esphome::remote_base::MirageProtocol().encode(data, in);
  transmit.perform();
}

bool MirageClimate::on_receive(remote_base::RemoteReceiveData data) {
  if (millis() - this->last_transmit_time_ < 500) {
    ESP_LOGV(TAG, "Blocked receive because of current transmission");
    return false;
  }

  auto optional_data_decoded = esphome::remote_base::MirageProtocol().decode(data);
  if (!optional_data_decoded) {
    ESP_LOGV(TAG, "Wrong data");
    return false;
  }

  const auto &data_decoded = *optional_data_decoded;
  esphome::remote_base::MirageProtocol().dump(data_decoded);

  // Power state (check top two bits only)
  if ((data_decoded.data[5] & 0xC0) == MIRAGE_POWER_OFF) {
    this->mode = climate::CLIMATE_MODE_OFF;
  } else {
    // Mode
    auto mode = data_decoded.data[4] & 0x70;
    switch (mode) {
      case MIRAGE_HEAT:
        this->mode = climate::CLIMATE_MODE_HEAT;
        break;
      case MIRAGE_COOL:
        this->mode = climate::CLIMATE_MODE_COOL;
        break;
      case MIRAGE_DRY:
        this->mode = climate::CLIMATE_MODE_DRY;
        break;
      case MIRAGE_FAN:
        this->mode = climate::CLIMATE_MODE_FAN_ONLY;
        break;
      case MIRAGE_AUTO:
        this->mode = climate::CLIMATE_MODE_HEAT_COOL;
        break;
    }
  }

  // Temperature
  this->target_temperature = data_decoded.data[1] - MIRAGE_TEMP_OFFSET;

  // Fan speed
  auto fan = data_decoded.data[4] & 0x03;
  switch (fan) {
    case MIRAGE_FAN_HIGH:
      this->fan_mode = climate::CLIMATE_FAN_HIGH;
      break;
    case MIRAGE_FAN_MED:
      this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
      break;
    case MIRAGE_FAN_LOW:
      this->fan_mode = climate::CLIMATE_FAN_LOW;
      break;
    default:
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
      break;
  }

  // Swing mode
  uint8_t swing_byte = data_decoded.data[5] & 0x03;
  switch (swing_byte) {
    case MIRAGE_SWING_OFF:
      this->swing_mode = climate::CLIMATE_SWING_OFF;
      break;
    case MIRAGE_SWING_HORIZONTAL:
      this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
      break;
    case MIRAGE_SWING_VERTICAL:
      this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
      break;
    case MIRAGE_SWING_BOTH:
      this->swing_mode = climate::CLIMATE_SWING_BOTH;
      break;
  }

  // Presets
  if (data_decoded.data[6] & MIRAGE_SLEEP) {
    this->preset = climate::CLIMATE_PRESET_SLEEP;
  } else if (data_decoded.data[8] & MIRAGE_BOOST) {
    this->preset = climate::CLIMATE_PRESET_BOOST;
  } else if (data_decoded.data[5] & MIRAGE_ECO) {
    this->preset = climate::CLIMATE_PRESET_ECO;
  } else {
    this->preset = climate::CLIMATE_PRESET_NONE;
  }

  this->publish_state();
  return true;
}

}  // namespace mirage
}  // namespace esphome
