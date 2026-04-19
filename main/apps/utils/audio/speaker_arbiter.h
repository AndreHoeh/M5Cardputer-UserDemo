/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

namespace audio {

enum class SpeakerOwner {
    None = 0,
    Mp3Playback,
    Recorder,
    SdAudioWorker,
};

bool try_acquire_speaker(SpeakerOwner owner);

void release_speaker(SpeakerOwner owner);

bool can_use_speaker(SpeakerOwner owner);

bool is_speaker_owned_by(SpeakerOwner owner);

SpeakerOwner current_speaker_owner();

const char* describe_speaker_owner(SpeakerOwner owner);

}  // namespace audio