#pragma once
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <time.h>
#include "FirmwareValidation.h"

extern const uint8_t otaCaBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t otaCaBundleEnd[] asm("_binary_x509_crt_bundle_end");

struct FirmwareUpdateStatus {
  bool busy = false;
  bool restarting = false;
  uint32_t written = 0;
  uint32_t total = 0;
  String message = "Ready for a GitHub application firmware link";
};

class FirmwareUpdater {
 public:
  void begin(SemaphoreHandle_t networkLock) {
    networkLock_ = networkLock;
    stateLock_ = xSemaphoreCreateMutex();
    char token[17];
    snprintf(token, sizeof(token), "%08lx%08lx", (unsigned long)esp_random(),
             (unsigned long)esp_random());
    token_ = token;
  }

  const String &token() const { return token_; }

  FirmwareUpdateStatus status() {
    if (!stateLock_) return state_;
    xSemaphoreTake(stateLock_, portMAX_DELAY);
    FirmwareUpdateStatus copy = state_;
    xSemaphoreGive(stateLock_);
    return copy;
  }

  bool active() {
    if (!stateLock_) return false;
    xSemaphoreTake(stateLock_, portMAX_DELAY);
    bool busy = state_.busy || state_.restarting;
    xSemaphoreGive(stateLock_);
    return busy;
  }

  bool start(String url, String &error) {
    url.trim();
    if (!stateLock_ || !networkLock_) { error = "Update service unavailable"; return false; }
    if (url.length() != strlen(url.c_str()) || !githubFirmwareUrl(url.c_str())) {
      error = "Use a public HTTPS GitHub release download or raw .bin URL";
      return false;
    }
    if (!WiFi.isConnected()) { error = "Connect home Wi-Fi before updating"; return false; }
    if (time(nullptr) < 1700000000) {
      error = "Wait for internet time synchronization, then retry";
      return false;
    }
    xSemaphoreTake(stateLock_, portMAX_DELAY);
    if (state_.busy || state_.restarting) {
      xSemaphoreGive(stateLock_);
      error = "An update is already in progress";
      return false;
    }
    url_ = url;
    state_ = FirmwareUpdateStatus();
    state_.busy = true;
    state_.message = "Waiting for any email check to finish";
    xSemaphoreGive(stateLock_);
    TaskHandle_t task = nullptr;
    if (xTaskCreate(taskEntry, "firmware_update", 12288, this, 1, &task) != pdPASS) {
      finish(false, "Not enough memory to start an update");
      error = "Not enough memory to start an update";
      return false;
    }
    return true;
  }

 private:
  SemaphoreHandle_t stateLock_ = nullptr;
  SemaphoreHandle_t networkLock_ = nullptr;
  FirmwareUpdateStatus state_;
  String url_;
  String token_;

  void progress(uint32_t written, uint32_t total, const String &message) {
    xSemaphoreTake(stateLock_, portMAX_DELAY);
    state_.written = written;
    state_.total = total;
    state_.message = message;
    xSemaphoreGive(stateLock_);
  }

  void finish(bool success, const String &message) {
    xSemaphoreTake(stateLock_, portMAX_DELAY);
    state_.busy = false;
    state_.restarting = success;
    state_.message = message;
    xSemaphoreGive(stateLock_);
  }

  static void taskEntry(void *argument) {
    auto *self = static_cast<FirmwareUpdater *>(argument);
    xSemaphoreTake(self->networkLock_, portMAX_DELAY);
    String error;
    bool ok = self->download(error);
    xSemaphoreGive(self->networkLock_);
    self->finish(ok, ok ? "Firmware installed. Restarting..." : error);
    Serial.printf("[update] %s\n", ok ? "Installed; restarting" : error.c_str());
    if (ok) {
      vTaskDelay(pdMS_TO_TICKS(2500));
      ESP.restart();
    }
    vTaskDelete(nullptr);
  }

  bool download(String &error) {
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    if (!next) { error = "No OTA slot: install the USB OTA-layout firmware first"; return false; }
    WiFiClientSecure client;
    client.setCACertBundle(otaCaBundleStart, otaCaBundleEnd - otaCaBundleStart);
    client.setTimeout(15000);
    client.setHandshakeTimeout(15);
    HTTPClient http;
    http.setTimeout(15000);
    http.setConnectTimeout(15000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    const char *headers[] = {"Location", "Content-Type"};
    http.collectHeaders(headers, 2);
    String current = url_;
    int response = -1;
    for (int redirects = 0; redirects <= 5; ++redirects) {
      if (!githubFirmwareUrl(current.c_str(), false)) {
        error = "Download redirect must remain on HTTPS GitHub download hosts";
        return false;
      }
      progress(0, 0, "Connecting to GitHub over verified HTTPS");
      if (!http.begin(client, current)) { error = "Could not open firmware URL"; return false; }
      response = http.GET();
      if (response != 301 && response != 302 && response != 303 && response != 307 && response != 308) break;
      String location = http.header("Location");
      http.end();
      if (location.startsWith("/") && !location.startsWith("//")) {
        int slash = current.indexOf('/', 8);
        location = current.substring(0, slash) + location;
      }
      current = location;
      if (redirects == 5) { error = "Too many firmware download redirects"; return false; }
    }
    if (response != HTTP_CODE_OK) {
      error = "Firmware download returned HTTP " + String(response) + ". Use a public release asset.";
      return false;
    }
    const int length = http.getSize();
    if (length < 288 || (uint32_t)length > next->size) {
      error = "Application image is missing, has no content length, or exceeds the OTA slot";
      return false;
    }
    String contentType = http.header("Content-Type");
    contentType.toLowerCase();
    if (contentType.startsWith("text/html")) { error = "The link returned a web page, not application firmware"; return false; }
    NetworkClient *stream = http.getStreamPtr();
    if (!stream) { error = "The firmware download stream closed before transfer"; return false; }
    constexpr uint32_t idleTimeout = 15000;
    const auto clock = [] { return millis(); };
    const auto pause = [] { vTaskDelay(pdMS_TO_TICKS(1)); };
    uint8_t prefix[288];
    for (size_t offset = 0; offset < sizeof(prefix);) {
      const auto chunk = readFirmwareChunk(*stream, prefix + offset, sizeof(prefix) - offset, idleTimeout, clock, pause);
      if (chunk.status != FirmwareReadStatus::Data) {
        error = chunk.status == FirmwareReadStatus::TimedOut ? "Timed out waiting for the firmware header" :
            "Connection closed before the firmware header arrived";
        return false;
      }
      offset += chunk.count;
    }
    if (!esp32c3ApplicationHeader(prefix, sizeof(prefix))) {
      error = "Use an ESP32-C3 application OTA .bin, not the merged USB installer";
      return false;
    }
    UpdateClass writer;
    if (!writer.begin(length, U_FLASH)) { error = String("Cannot start update: ") + writer.errorString(); return false; }
    uint32_t written = writer.write(prefix, sizeof(prefix));
    if (written != sizeof(prefix)) {
      error = "Could not write firmware header";
      writer.abort();
      return false;
    }
    uint8_t buffer[1024];
    while (written < (uint32_t)length) {
      const size_t capacity = min(sizeof(buffer), size_t(length - written));
      const auto chunk = readFirmwareChunk(*stream, buffer, capacity, idleTimeout, clock, pause);
      if (chunk.status != FirmwareReadStatus::Data) {
        error = chunk.status == FirmwareReadStatus::TimedOut ?
            "Firmware download timed out. Current firmware remains selected." :
            "Firmware download interrupted. Current firmware remains selected.";
        writer.abort();
        return false;
      }
      if (writer.write(buffer, chunk.count) != chunk.count) {
        error = String("Firmware write failed: ") + writer.errorString();
        writer.abort();
        return false;
      }
      written += chunk.count;
      progress(written, length, "Downloading and writing the inactive firmware slot");
      pause();
    }
    http.end();
    progress(written, length, "Validating the downloaded application");
    if (!writer.end()) { error = String("Firmware validation failed: ") + writer.errorString(); return false; }
    return true;
  }
};
