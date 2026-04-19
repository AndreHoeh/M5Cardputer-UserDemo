/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "speaker_arbiter.h"

#include <atomic>

namespace audio {
namespace {
std::atomic<int> g_speaker_owner{static_cast<int>(SpeakerOwner::None)};
}

bool try_acquire_speaker(SpeakerOwner owner)
{
    if (owner == SpeakerOwner::None) {
        return false;
    }

    int expected        = static_cast<int>(SpeakerOwner::None);
    const int requested = static_cast<int>(owner);
    if (g_speaker_owner.compare_exchange_strong(expected, requested)) {
        return true;
    }

    return expected == requested;
}

void release_speaker(SpeakerOwner owner)
{
    if (owner == SpeakerOwner::None) {
        return;
    }

    int expected = static_cast<int>(owner);
    g_speaker_owner.compare_exchange_strong(expected, static_cast<int>(SpeakerOwner::None));
}

bool can_use_speaker(SpeakerOwner owner)
{
    const SpeakerOwner current = current_speaker_owner();
    return current == SpeakerOwner::None || current == owner;
}

bool is_speaker_owned_by(SpeakerOwner owner)
{
    return current_speaker_owner() == owner;
}

SpeakerOwner current_speaker_owner()
{
    return static_cast<SpeakerOwner>(g_speaker_owner.load());
}

const char* describe_speaker_owner(SpeakerOwner owner)
{
    switch (owner) {
        case SpeakerOwner::Mp3Playback:
            return "MP3 playback";
        case SpeakerOwner::Recorder:
            return "Recorder";
        case SpeakerOwner::SdAudioWorker:
            return "SD audio worker";
        case SpeakerOwner::None:
        default:
            return "none";
    }
}

}  // namespace audio