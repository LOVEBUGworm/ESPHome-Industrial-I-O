# ESPHome Modbus TCP Server

[![ESPHome](https://img.shields.io/badge/ESPHome-Component-blue?logo=esphome)](https://esphome.io/)
[![ESP32](https://img.shields.io/badge/Hardware-ESP32-E7352C?logo=espressif)](https://www.espressif.com/en/products/socs/esp32)
[![Modbus TCP](https://img.shields.io/badge/Protocol-Modbus%20TCP-orange)](https://modbus.org/)
[![License](https://img.shields.io/badge/License-MIT-green)](#license)

A lightweight **Modbus TCP Server component for ESPHome on ESP32**.

This component allows an ESP32 running ESPHome to act as a Modbus TCP server, allowing PLCs and other Modbus clients to communicate directly with ESPHome entities.

No external gateway.  
No additional Modbus library.  
Just ESP32 + ESPHome + Modbus TCP.

---

# Overview

```
Ethernet / Wi-Fi
        │
        │  Modbus TCP :502
        ▼
┌──────────────────────┐
│        PLC           │
│  Modbus TCP Client   │
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│        ESP32         │
│  Modbus TCP Server   │
│                      │
│  Coil 0  ───► Entity │
│  Coil 1  ───► Entity │
│  ...                 │
│  Coil 10 ───► Entity │
└──────────────────────┘
```

The project is intentionally simple and focused:

- Coil-based communication only
- One TCP client at a time
- Native ESP32 sockets
- Designed for PLC ↔ ESPHome communication

---

# Features

✅ ESPHome external component  
✅ Native ESP32 TCP sockets  
✅ Default Modbus TCP port (502)  
✅ Configurable Unit ID  
✅ 11 coil addresses (0–10)  
✅ Non-blocking socket handling  
✅ Proper Modbus exception responses  
✅ Single active client connection  

Supported entity types:

| ESPHome Entity | Read | Write |
|---|---|---|
| `binary_sensor` | ✅ | Internal state only |
| `switch` | ✅ | ✅ Controls switch |
| Unlinked coil | Internal state | Internal state |

---

# Supported Modbus Functions

| Function Code | Name | Status |
|---|---|---|
| `0x01` | Read Coils | ✅ Supported |
| `0x05` | Write Single Coil | ✅ Supported |
| `0x0F` | Write Multiple Coils | ✅ Supported |
| Other | Unsupported | ❌ Exception |

---

# Installation

## External Component

Example:

```yaml
external_components:
  - source:
      type: local
      path: components
```

(or use a Git repository source)

---

# ESPHome Configuration

## Minimal Example

```yaml
modbus_tcp_server:
  id: modbus_srv
```

Defaults:

- Port: `502`
- Unit ID: `1`
- Coil range: `0-10`

---

## Full Example

```yaml
modbus_tcp_server:
  id: modbus_srv
  port: 502
  unit_id: 1

  coils:
    - address: 0
      binary_sensor: pump_running

    - address: 1
      switch: pump_enable

    - address: 2
      binary_sensor: system_alarm

    - address: 3
      switch: interlock_reset


binary_sensor:
  - platform: gpio
    pin: GPIO27
    id: pump_running
    name: "Pump Running"


switch:
  - platform: gpio
    pin: GPIO13
    id: pump_enable
    name: "Pump Enable"
```

---

# Coil Map

| Address | Access | Notes |
|---|---|---|
| 0–10 | Read / Write* | Available coils |
| ≥11 | ❌ Invalid | Returns exception `0x02` |

\* Writing only affects linked switches.

---

# Coil Behaviour

| Linked Entity | Read Coil (`0x01`) | Write Coil (`0x05`, `0x0F`) |
|---|---|---|
| `binary_sensor` | Returns sensor state | Updates internal state only |
| `switch` | Returns switch state | Controls switch |
| None | Returns stored coil value | Updates stored value |

---

# Writing Coils

## Write Single Coil (`0x05`)

Supported values:

| Value | Result |
|---|---|
| `0xFF00` | ON |
| `0x0000` | OFF |

---

## Write Multiple Coils (`0x0F`)

Uses standard Modbus bit-packed format.

The component validates:

- Quantity
- Byte count
- Frame length

Invalid frames return a Modbus exception.

---

# Connection Behaviour

- Only one TCP client is accepted at a time
- Non-blocking sockets prevent ESPHome loop blocking
- Disconnecting clients immediately frees the server for another connection

---

# Error Handling

| Exception | Code | Cause |
|---|---|---|
| Illegal Function | `0x01` | Unsupported function code |
| Illegal Data Address | `0x02` | Coil outside valid range |
| Illegal Data Value | `0x03` | Invalid quantity, value, or frame |

---

# Project Structure

```
components/
└── modbus_tcp_server/
    ├── __init__.py
    ├── modbus_tcp_server.h
    └── modbus_tcp_server.cpp
```

---

# Current Limitations

- Maximum 11 coils (`0-10`)
- One TCP client at a time
- No holding registers
- No input registers
- No discrete inputs
- No authentication/encryption
- ESP32 only

---

# Roadmap

| Feature | Status |
|---|---|
| Coil communication | ✅ Done |
| ESPHome switches | ✅ Done |
| Read Coils (`0x01`) | ✅ Done |
| Write Single Coil (`0x05`) | ✅ Done |
| Write Multiple Coils (`0x0F`) | ✅ Done |
| Expand coil count | ☐ Planned |
| Holding registers | ☐ Planned |
| Input registers | ☐ Planned |
| Discrete inputs | ☐ Planned |
| Multiple clients | ☐ Planned |
| Better diagnostics | ☐ Planned |
| Configurable max clients | ☐ Planned |
| Additional function codes | ☐ Planned |

---

# Why This Exists

ESPHome is excellent for sensors and automation.

PLCs are excellent for industrial control.

This component provides a simple bridge:

```
ESPHome Device  <──── Modbus TCP ────>  PLC
```

No extra hardware.  
No gateway.  
No complicated setup.

Just a clean Modbus interface.

---

# License

MIT License — see `LICENSE`

---

# Status

**Development / Experimental**

The component is functional for its current feature set, but the Modbus implementation is intentionally limited.

Test thoroughly before using in safety-critical applications.

Built for:

- ESPHome
- ESP32
- Simple industrial automation

Built for people who just want Modbus TCP without the headache. ¯\_(ツ)_/¯
