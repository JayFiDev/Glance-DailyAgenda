#pragma once

// Stub logging macros — map to Serial when available, no-op otherwise
#define LOG_ERR(tag, fmt, ...) do { if (Serial) Serial.printf("[%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#define LOG_DBG(tag, fmt, ...) do { if (Serial) Serial.printf("[%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#define LOG_INF(tag, fmt, ...) do { if (Serial) Serial.printf("[%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
