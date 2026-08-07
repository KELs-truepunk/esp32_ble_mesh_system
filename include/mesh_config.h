#pragma once

#include <stdint.h>

// Имя устройства в эфире
#define DEVICE_NAME "BC_Node"

// Custom Company ID для Manufacturer Specific Data (0xFFFF — тестовый диапазон BLE)
#define MESH_COMPANY_ID 0xFFFF

// Размер кольцевого буфера дубликатов (r_cache)
#define ROUTER_CACHE_SIZE 25

// Базовый TTL для новых сообщений
#define DEFAULT_TTL 4

// Динамический ID отправителя по умолчанию (если не вытянули MAC)
#define DEFAULT_SENDER_ID 0x77777777