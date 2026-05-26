#pragma once

#include "Board/BoardSpec.h"

// Waveshare ESP32-S3-ETH-8DI-8RO
//
// Hardware summary:
//   MCU       : ESP32-S3 (dual-core LX7, 240 MHz, 16 MB flash, 8 MB PSRAM)
//   Ethernet  : W5500 via SPI — MOSI=GPIO13, MISO=GPIO14, SCLK=GPIO15, CS=GPIO16,
//               INT=GPIO12, RST=GPIO9   (not modelled in BoardSpec; configure in the Ethernet driver)
//   Relays    : 8× NO/NC via TCA9554PWR I2C expander at I2C address 0x20
//               pin field = expander channel index 0-7
//   DI        : 8× optocoupler-isolated digital inputs, active-low — GPIO4-11
//   RS485     : Isolated half-duplex — UART1 RX=GPIO18, TX=GPIO17
//   I2C       : SDA=GPIO42, SCL=GPIO41 (shared by TCA9554 relay expander)
//   Power     : 7-36 V DC industrial input or 5 V USB-C

namespace BoardProfiles {

inline constexpr uint32_t kWaveshareS3ETHIoI2cHz = 400000U;

// IO capacity: 8 digital inputs, 8 digital outputs, no real analog.
// Minimums imposés par SystemLimits : analogEndpoints >= 1, analogConfigSlots >= 6.
inline constexpr IoCapacitySpec kWaveshareS3ETHIoCapacity{2, 8, 8, 6, 8, 8};
inline constexpr MqttCapacitySpec kWaveshareS3ETHMqttCapacity{5712, 8, 8, 48, 24, 16, 2, 80, 80, 80, 60};
inline constexpr MqttBufferSpec kWaveshareS3ETHMqttBuffers{
    64, 32, 32, 15, 15, 70, 160, 128, 384, 1536, 1024, 1536, 1536, 64, 320, 32
};
inline constexpr HaCapacitySpec kWaveshareS3ETHHaCapacity{20, 8, 10, 8, 20, 6};

inline constexpr UartSpec kWaveshareS3ETHUarts[] = {
    // {name, uartIndex, rxPin, txPin, baud, primary, enableRxPin}
    {"log",   0, 44, 43, 115200, true,  -1}, // UART0 console (RX=GPIO44, TX=GPIO43 — header pins 15/13).
    {"rs485", 1, 18, 17, 115200, false, -1}, // Isolated RS485 on UART1 (RX=GPIO18, TX=GPIO17).
};

inline constexpr I2cBusSpec kWaveshareS3ETHI2c[] = {
    // {name, sdaPin, sclPin, frequencyHz}
    {"io", 42, 41, kWaveshareS3ETHIoI2cHz}, // IO expander bus: TCA9554 relay driver at 0x20 (SDA=GPIO42, SCL=GPIO41).
};

// Relay outputs — TCA9554 I2C expander channels (pin = expander bit index 0-7).
// Digital inputs — direct ESP32-S3 GPIOs via optocoupler (active-low, GPIO4-11).
inline constexpr IoPointSpec kWaveshareS3ETHIoPoints[] = {
    // {name, capability, signal, pin, momentary, pulseMs}
    {"relay1",      IoCapability::DigitalOut, BoardSignal::Relay1,    0,  false, 0}, // TCA9554 P0 — filtration pump relay.
    {"relay2",      IoCapability::DigitalOut, BoardSignal::Relay2,    1,  false, 0}, // TCA9554 P1 — pH pump relay.
    {"relay3",      IoCapability::DigitalOut, BoardSignal::Relay3,    2,  false, 0}, // TCA9554 P2 — chlorine pump relay.
    {"relay4",      IoCapability::DigitalOut, BoardSignal::Relay4,    3,  false, 0}, // TCA9554 P3 — aux relay (chlorine generator).
    {"relay5",      IoCapability::DigitalOut, BoardSignal::Relay5,    4,  false, 0}, // TCA9554 P4 — robot relay.
    {"relay6",      IoCapability::DigitalOut, BoardSignal::Relay6,    5,  false, 0}, // TCA9554 P5 — lights relay.
    {"relay7",      IoCapability::DigitalOut, BoardSignal::Relay7,    6,  false, 0}, // TCA9554 P6 — fill pump relay.
    {"relay8",      IoCapability::DigitalOut, BoardSignal::Relay8,    7,  false, 0}, // TCA9554 P7 — water heater relay.
    {"digital_in1", IoCapability::DigitalIn,  BoardSignal::DigitalIn1, 4,  false, 0}, // DI1 GPIO4  — pool level sensor.
    {"digital_in2", IoCapability::DigitalIn,  BoardSignal::DigitalIn2, 5,  false, 0}, // DI2 GPIO5  — pH tank level sensor.
    {"digital_in3", IoCapability::DigitalIn,  BoardSignal::DigitalIn3, 6,  false, 0}, // DI3 GPIO6  — chlorine tank level sensor.
    {"digital_in4", IoCapability::DigitalIn,  BoardSignal::DigitalIn4, 7,  false, 0}, // DI4 GPIO7  — water counter pulse.
    {"digital_in5", IoCapability::DigitalIn,  BoardSignal::DigitalIn5, 8,  false, 0}, // DI5 GPIO8  — spare input.
    {"digital_in6", IoCapability::DigitalIn,  BoardSignal::DigitalIn6, 9,  false, 0}, // DI6 GPIO9  — spare input.
    {"digital_in7", IoCapability::DigitalIn,  BoardSignal::DigitalIn7, 10, false, 0}, // DI7 GPIO10 — spare input.
    {"digital_in8", IoCapability::DigitalIn,  BoardSignal::DigitalIn8, 11, false, 0}, // DI8 GPIO11 — spare input.
};

inline constexpr BoardSpec kWaveshareS3ETH8DI8RO{
    "WaveshareS3ETH8DI8RO",
    "flowio-core",
    kWaveshareS3ETHUarts,
    (uint8_t)(sizeof(kWaveshareS3ETHUarts) / sizeof(kWaveshareS3ETHUarts[0])),
    kWaveshareS3ETHI2c,
    (uint8_t)(sizeof(kWaveshareS3ETHI2c) / sizeof(kWaveshareS3ETHI2c[0])),
    nullptr, // no 1-Wire buses
    0,
    kWaveshareS3ETHIoPoints,
    (uint8_t)(sizeof(kWaveshareS3ETHIoPoints) / sizeof(kWaveshareS3ETHIoPoints[0])),
    kWaveshareS3ETHIoCapacity,
    kWaveshareS3ETHMqttCapacity,
    kWaveshareS3ETHMqttBuffers,
    kWaveshareS3ETHHaCapacity,
    nullptr // no supervisor spec
};

}  // namespace BoardProfiles
