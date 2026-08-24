# Flooding Mesh Project

An autonomous, decentralized, hardware-agnostic BLE Flooding Mesh network implementation written in **Pure C** for **ESP32** using native **ESP-IDF** (Bluedroid stack).

Designed for resilient, off-grid text communication without reliance on standard BLE Mesh infrastructure, cellular networks, or central routers.

---

## 🛠 Features

* **Zero-Connection Flooding Mesh:** Operates strictly via raw BLE Advertisement bursts (`ADV_TYPE_NONCONN_IND`) over standard channels (37, 38, 39).
* **Embedded Web Console:** Integrated Wi-Fi SoftAP + HTTP Server (`192.168.4.1`) serving an embedded cyberpunk-styled UI directly from Flash memory (no SD card or external hosting required).
* **Smart Multi-Hop Relay:** Automatic packet re-broadcasting with `TTL` (Time-To-Live) decrement mechanism.
* **Duplication Suppression:** Integrated Ring Cache (`r_cache`) using FNV-1a packet hashing to prevent broadcast loops and RF saturation.
* **Transport Chunking & Reassembly:** Automatic payload slicing and multi-segment handling for messages exceeding single-frame limits.
* **Dual-Core Execution:** Core 0 dedicated to BLE Stack/GAP tasks; Core 1 handles Web Server and Async TX tasks.

---

## 📐 Architecture & Protocol

### Custom Advertising Packet Payload (`b_mesh_packet_t`)

The protocol uses standard BLE Manufacturer Specific Data (`0xFF`) with a custom Company ID (`0xFFFF`).

| Field | Size | Description |
| :--- | :--- | :--- |
| `message_id` | 2 Bytes | Unique sequence/message identifier |
| `sender_id` | 4 Bytes | Node identifier (derived from ESP32 Base MAC) |
| `packet_type`| 1 Byte | Packet type / flags (e.g., `0x01` MSG, `0x02` ACK) |
| `ttl` | 1 Byte | Time-To-Live counter (decremented on each hop) |
| `payload` | 15 Bytes | Encoded UTF-8 message segment data |

---

## 🌐 Network Topology

```text
[ Mobile Terminal A ]
        │ (Wi-Fi SoftAP / HTTP REST API)
        ▼
   [ Node #1 ]  ─── (BLE ADV Burst / Ch 37,38,39) ───►  [ Node #2 ] (Relay / Mesh Router)
                                                               │ (BLE ADV Burst)
                                                               ▼
                                                        [ Node #3 ]
                                                               │ (Wi-Fi SoftAP / HTTP REST API)
                                                               ▼
                                                     [ Mobile Terminal B ]
---

## 📂 Project Structure

```text
ble/
├── include/                   # Protocol & Transport Headers
│   ├── ble_mesh_protocol.h    # Packet structure (b_mesh_packet_t), bit-fields, FNV-1a hash
│   ├── ble_mesh_transport.h   # GAP constants, duplicate ring cache, routing prototypes
│   ├── gap_handler.h          # BLE GAP event handler interfaces
│   ├── mesh_config.h          # Global mesh timeouts, TTL limits, channel configs
│   └── mesh_hash.h            # Fast hashing algorithms for deduplication
├── src/                       # Core Source Code
│   ├── main.c                 # Entry point, dual-core task pinned init, NVS setup
│   ├── ble_mesh_transport.c   # Transport layer, chunk assembly, RX/TX queues
│   ├── gap_handler.c          # GAP scanner & advertiser handlers
│   ├── web_server.c           # HTTPD REST API server (/api/messages)
│   ├── wifi_app.c             # Wi-Fi SoftAP configuration manager
│   ├── index.html             # Web interface template (compiled into Flash)
│   └── CMakeLists.txt         # Auto-embed script generating C-headers from index.html
├── partitions.csv             # Custom Partition Table (3MB Factory App for Bluedroid stack)
├── platformio.ini             # PlatformIO config (ESP-IDF framework, esp32dev)
├── sdkconfig.esp32dev         # ESP-IDF SDK configuration
└── README.md                  # Project documentation
```
---
## 📜 License

This project is licensed under the BSD 3-Clause License - see the [LICENSE](LICENSE) file for details.