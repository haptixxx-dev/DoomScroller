#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace ds {

// Thin internal audio facade over a backend (currently miniaudio). No backend
// types leak through this header so the implementation can be swapped (e.g.
// FMOD) without touching callers. All calls are no-ops when init() failed or a
// requested sound file is missing, so gameplay code never has to guard them.
//
// Sound paths passed to play/playAt/playMusic are resolved relative to the
// asset root (ds::paths::assets()), e.g. "sfx/weapon_fire.wav".
class AudioSystem {
  public:
    AudioSystem();
    ~AudioSystem();

    AudioSystem(const AudioSystem&)            = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // Brings up the audio device. Returns false (and logs) on failure; the
    // object stays usable as a silent no-op in that case.
    bool init();
    void shutdown();
    bool initialized() const;

    // Fire-and-forget 2D one-shot. Decoded sounds are cached by path.
    void play(const std::string& path);

    // Fire-and-forget 3D spatialised one-shot at a world position. The listener
    // is positioned via setListener(). Missing files are logged once and skipped.
    void playAt(const std::string& path, const glm::vec3& position);

    // Starts (or restarts) a looping music track on the dedicated music bus.
    void playMusic(const std::string& path);
    void stopMusic();

    // Updates the 3D listener; call once per frame from the camera.
    void setListener(const glm::vec3& position, const glm::vec3& forward);

    // 0..1 multipliers. SFX volume covers play()/playAt(); music is separate.
    void setMasterVolume(float volume);
    void setSfxVolume(float volume);
    void setMusicVolume(float volume);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ds
