#pragma once
#include <mooncake.h>
#include <cstdint>
#include <string>

namespace workers {

/**
 * @brief Loads and plays a WAV file from the SD card as a mooncake WorkerAbility.
 *
 * Usage:
 *   int id = GetMooncake().createExtension(
 *       std::make_unique<workers::SdAudioWorker>("/sdcard/my_sound.wav", volume, channel));
 *   // Drive it each frame:
 *   GetMooncake().extensionManager()->updateAbilities();
 *   // When done:
 *   GetMooncake().destroyExtension(id);
 *   GetMooncake().extensionManager()->updateAbilities();
 *
 * The WAV buffer is heap-allocated on onCreate() and freed on onDestroy().
 * The speaker channel is stopped before freeing so the FreeRTOS speaker task
 * can clear its data pointer safely.
 */
class SdAudioWorker : public mooncake::WorkerAbility {
public:
    /**
     * @param sdPath  Full VFS path, e.g. "/sdcard/boot_sfx.wav"
     * @param volume  Reference volume scaled against the current master volume
     * @param channel Speaker channel index (default 0); pass -1 for auto-select
     */
    SdAudioWorker(std::string sdPath, uint8_t volume = 128, int channel = 0);

    void onCreate() override;
    void onDestroy() override;

private:
    std::string _sd_path;
    uint8_t _volume;
    int _channel;
    uint8_t* _wav_buf = nullptr;
    size_t _wav_size  = 0;
};

}  // namespace workers
