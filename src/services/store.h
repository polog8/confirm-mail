// Thread-safe shared state between the network tasks (core 0) and the UI
// loop (core 1), plus the queue of Spotify commands.
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "../model/state.h"

struct SpotifyRequest {
  SpotifyCmd cmd;
  int32_t arg;
};

class Store {
 public:
  void begin();

  // Scoped lock: `{ Store::Guard g(store); store.data().play... }`
  class Guard {
   public:
    explicit Guard(Store& s) : s_(s) { s_.lock(); }
    ~Guard() { s_.unlock(); }

   private:
    Store& s_;
  };
  void lock() { xSemaphoreTake(mutex_, portMAX_DELAY); }
  void unlock() { xSemaphoreGive(mutex_); }

  AppSnapshot& data() { return *snap_; }  // hold the lock
  // Writers call this after modifying data() so the UI redraws.
  void touch() { ++version_; }
  uint32_t version() const { return version_; }

  // Cover art: the store takes ownership of a heap (PSRAM) buffer.
  void setArt(uint8_t* jpg, size_t len);
  uint32_t artSerial() const { return artSerial_; }
  // Copies the art into `out` (resized PSRAM buffer). Returns its length.
  size_t copyArt(uint8_t** out, size_t* cap);

  // Spotify command queue
  bool pushCommand(SpotifyCmd cmd, int32_t arg);
  bool popCommand(SpotifyRequest* req, uint32_t waitMs);
  // Playback fields the UI changed optimistically are kept until this time.
  volatile uint32_t optimisticUntilMs = 0;

  volatile bool musicVisible = false;
  volatile bool refreshRequested = false;
  volatile bool rtcNeedsSync = false;
  // Authorization code pasted in the web portal, consumed by the Spotify task.
  char pendingAuthCode[400] = {0};
  volatile bool authCodeReady = false;

 private:
  SemaphoreHandle_t mutex_ = nullptr;
  QueueHandle_t queue_ = nullptr;
  AppSnapshot* snap_ = nullptr;
  volatile uint32_t version_ = 1;
  uint8_t* art_ = nullptr;
  size_t artLen_ = 0;
  volatile uint32_t artSerial_ = 0;
};

extern Store g_store;
