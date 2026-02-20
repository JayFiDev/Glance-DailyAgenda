/*
 * Glance — Bluetooth module implementation
 * BLE server setup, callbacks, advertising, and two-way completion sync.
 */

#include "bluetooth.h"
#include "globals.h"
#include "helpers.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>

// ============================================================================
// BLE CALLBACKS (static instances to avoid heap leaks across start/stop cycles)
// ============================================================================

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    bleConnected = true;
    lastActivityTime = millis();
    if (Serial) Serial.println("[BLE] Client connected");
  }

  void onDisconnect(BLEServer* pServer) override {
    bleConnected = false;
    if (Serial) Serial.println("[BLE] Client disconnected");

    if (!bleDataReady && bleBufferPos > 0) {
      bleBufferPos = 0;
      bleBuffer[0] = '\0';
      if (Serial) Serial.println("[BLE] Cleared incomplete buffer");
    }

    if (bleEnabled) {
      BLEDevice::startAdvertising();
    }
  }
};

class CharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    if (bleDataReady) return;

    std::string value = pCharacteristic->getValue();
    if (value.length() == 0) return;

    lastActivityTime = millis();

    if (bleBufferPos >= BLE_BUFFER_SIZE - 1) {
      bleBufferPos = 0;
      bleBuffer[0] = '\0';
      if (Serial) Serial.println("[BLE] Buffer full, resetting");
      return;
    }

    size_t remaining = BLE_BUFFER_SIZE - bleBufferPos - 1;
    size_t copyLen = (value.length() < remaining) ? value.length() : remaining;

    memcpy(bleBuffer + bleBufferPos, value.c_str(), copyLen);
    bleBufferPos += copyLen;
    bleBuffer[bleBufferPos] = '\0';

    if (Serial) Serial.printf("[BLE] Received %d bytes (total: %d)\n",
                               (int)value.length(), (int)bleBufferPos);

    if (isCompleteJSON(bleBuffer, bleBufferPos)) {
      if (Serial) Serial.printf("[BLE] Complete JSON (%d bytes)\n",
                                 (int)bleBufferPos);
      bleDataReady = true;
    }
  }
};

static ServerCallbacks serverCallbacks;
static CharacteristicCallbacks characteristicCallbacks;
static BLE2902 ble2902Descriptor;

// ============================================================================
// BLE CONTROL
// ============================================================================

void startBLE() {
  if (bleEnabled) return;

  if (Serial) Serial.println("[BLE] Starting BLE...");

  // Initialize BLE stack and server only once — deinit/reinit crashes on ESP32
  if (!pServer) {
    BLEDevice::init(DEVICE_NAME);
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(&serverCallbacks);

    BLEService* pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ);
    pCharacteristic->setCallbacks(&characteristicCallbacks);
    pCharacteristic->addDescriptor(&ble2902Descriptor);

    pService->start();

    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
  }

  updateBLECompletionValue();
  BLEDevice::startAdvertising();

  bleEnabled = true;
  bleStartTime = millis();
  needsDisplayUpdate = true;

  if (Serial) Serial.println("[BLE] Advertising started");
}

void stopBLE() {
  if (!bleEnabled) return;

  if (Serial) Serial.println("[BLE] Stopping BLE...");

  BLEDevice::getAdvertising()->stop();

  // Disconnect any connected client
  if (bleConnected && pServer) {
    pServer->disconnect(pServer->getConnId());
  }

  bleConnected = false;
  bleEnabled = false;
  bleBufferPos = 0;
  bleBuffer[0] = '\0';
  bleDataReady = false;
  needsDisplayUpdate = true;

  if (Serial) {
    Serial.println("[BLE] BLE stopped");
    Serial.printf("[MEM] Free heap: %d bytes\n", ESP.getFreeHeap());
  }
}

// ============================================================================
// COMPLETION VALUE (Two-Way Sync)
// ============================================================================

void updateBLECompletionValue() {
  if (!pCharacteristic) return;

  if (completedCount == 0) {
    pCharacteristic->setValue("{\"completedIds\":[]}");
  } else {
    char buf[768];
    JsonDocument doc;
    JsonArray arr = doc["completedIds"].to<JsonArray>();
    for (int i = 0; i < completedCount; i++) {
      arr.add(completedIds[i]);
    }
    size_t len = serializeJson(doc, buf, sizeof(buf) - 1);
    buf[len] = '\0';
    pCharacteristic->setValue(buf);
  }
}
