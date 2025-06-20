#pragma once

#include "esphome/components/climate_ir/climate_ir.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace mirage {

/// Simple enum to represent models.

// Temperature
const float MIRAGE_TEMP_MAX = 32.0;
const float MIRAGE_TEMP_MIN = 16.0;

class MirageClimate : public esphome::climate_ir::ClimateIR {
 public:
  uint8_t swing_position = 0;
  MirageClimate()
      : climate_ir::ClimateIR(MIRAGE_TEMP_MIN, MIRAGE_TEMP_MAX, 1.0f, true, true,
                              {esphome::climate::CLIMATE_FAN_AUTO, esphome::climate::CLIMATE_FAN_LOW, esphome::climate::CLIMATE_FAN_MEDIUM,
                                esphome::climate::CLIMATE_FAN_HIGH},
                              {esphome::climate::CLIMATE_SWING_OFF, esphome::climate::CLIMATE_SWING_VERTICAL, esphome::climate::CLIMATE_SWING_HORIZONTAL,
                                esphome::climate::CLIMATE_SWING_BOTH},
                              {esphome::climate::CLIMATE_PRESET_NONE, esphome::climate::CLIMATE_PRESET_ECO, esphome::climate::CLIMATE_PRESET_SLEEP,
                                esphome::climate::CLIMATE_PRESET_BOOST}) {}

  void set_sensor(sensor::Sensor *sensor) { 
    this->sensor_ = sensor; 
    this->sensor_->add_on_state_callback([this](float state) {
      this->current_temperature = state;
      this->publish_state();
    });
  }                              

  void setup() override {
    esphome::climate_ir::ClimateIR::setup();
    // Initialize current temperature if sensor is available
    if (this->sensor_) {
      this->current_temperature = this->sensor_->state;
    }
  }

 protected:
  /// Transmit via IR the state of this climate controller.
  void transmit_state() override;
  /// Handle received IR Buffer
  bool on_receive(esphome::remote_base::RemoteReceiveData data) override;
  /// Set the time of the last transmission.
  int32_t last_transmit_time_{};
  /// The sensor used for getting current temperature
  sensor::Sensor *sensor_{nullptr};
};

}  // namespace mirage
}  // namespace esphome
