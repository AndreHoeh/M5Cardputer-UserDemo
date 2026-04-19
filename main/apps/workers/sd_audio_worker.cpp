#include "sd_audio_worker.h"
#include <hal.h>
#include <mooncake_log.h>
#include <cstdio>
#include <cstdlib>

static const char* _tag = "SdAudioWorker";

namespace workers {

SdAudioWorker::SdAudioWorker(std::string sdPath, float relative_volume, int channel)
    : _sd_path(std::move(sdPath)),
      _relative_volume(relative_volume),
      _volume_before(GetHAL().getSpeakerVolume()),
      _channel(channel)
{
}

void SdAudioWorker::onCreate()
{
    if (!GetHAL().ensureSdCardMounted()) {
        mclog::tagWarn(_tag, "SD card not mounted, skipping: {}", _sd_path);
        return;
    }

    FILE* fp = fopen(_sd_path.c_str(), "rb");
    if (!fp) {
        mclog::tagWarn(_tag, "file not found: {}", _sd_path);
        return;
    }

    fseek(fp, 0, SEEK_END);
    _wav_size = static_cast<size_t>(ftell(fp));
    rewind(fp);

    _wav_buf = static_cast<uint8_t*>(malloc(_wav_size));
    if (!_wav_buf) {
        mclog::tagWarn(_tag, "malloc failed ({} bytes)", _wav_size);
        fclose(fp);
        return;
    }

    fread(_wav_buf, 1, _wav_size, fp);
    fclose(fp);

    GetHAL().applyScaledSpeakerVolume(_relative_volume);
    GetHAL().speaker.playWav(_wav_buf, _wav_size, 1, _channel);
    mclog::tagInfo(_tag, "playing {} bytes from {}", _wav_size, _sd_path);
}

void SdAudioWorker::onDestroy()
{
    if (_wav_buf) {
        GetHAL().speaker.stop(static_cast<uint8_t>(_channel));
        GetHAL().delay(20);  // Let the speaker FreeRTOS task process the stop before freeing
        free(_wav_buf);
        _wav_buf = nullptr;
    }
    GetHAL().setSpeakerVolume(_volume_before);
}

}  // namespace workers
