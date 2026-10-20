// Spotify Web API worker (playback state, transport commands, cover art,
// "liked" state). Runs in its own FreeRTOS task on core 0.
#pragma once

#include <stddef.h>

namespace spotify {

constexpr const char* kRedirectUri = "http://127.0.0.1:8888/callback";

void startTask();
// Builds the authorization URL for the web portal; `state` protects the flow.
void authorizeUrl(char* out, size_t n, const char* state);

}  // namespace spotify
