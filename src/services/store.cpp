#include "store.h"

#include <esp_heap_caps.h>

#include <new>

Store g_store;

void Store::begin() {
  mutex_ = xSemaphoreCreateMutex();
  queue_ = xQueueCreate(8, sizeof(SpotifyRequest));
  void* mem = heap_caps_malloc(sizeof(AppSnapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!mem) mem = malloc(sizeof(AppSnapshot));
  snap_ = new (mem) AppSnapshot();
}

void Store::setArt(uint8_t* jpg, size_t len) {
  Guard g(*this);
  free(art_);
  art_ = jpg;
  artLen_ = jpg ? len : 0;
  ++artSerial_;
  ++version_;
}

size_t Store::copyArt(uint8_t** out, size_t* cap) {
  Guard g(*this);
  if (!art_ || !artLen_) return 0;
  if (*cap < artLen_) {
    free(*out);
    *out = static_cast<uint8_t*>(heap_caps_malloc(artLen_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    *cap = *out ? artLen_ : 0;
    if (!*out) return 0;
  }
  memcpy(*out, art_, artLen_);
  return artLen_;
}

bool Store::pushCommand(SpotifyCmd cmd, int32_t arg) {
  SpotifyRequest r{cmd, arg};
  return xQueueSend(queue_, &r, 0) == pdTRUE;
}

bool Store::popCommand(SpotifyRequest* req, uint32_t waitMs) {
  return xQueueReceive(queue_, req, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}
