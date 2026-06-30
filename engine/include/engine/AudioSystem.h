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

    // Fire-and-forget 2D one-shot on the SFX bus. Decoded sounds are cached by path.
    void play(const std::string& path);

    // Fire-and-forget 2D one-shot on the dedicated UI bus (menu clicks, slider
    // ticks, rank-up sting). Routed through the UI volume, independent of SFX.
    void playUI(const std::string& path);

    // Fire-and-forget 3D spatialised one-shot at a world position. The listener
    // is positioned via setListener(). Missing files are logged once and skipped.
    void playAt(const std::string& path, const glm::vec3& position);

    // Starts (or restarts) a looping music track on the dedicated music bus.
    void playMusic(const std::string& path);
    void stopMusic();

    // Updates the 3D listener; call once per frame from the camera.
    void setListener(const glm::vec3& position, const glm::vec3& forward);

    // 0..1 multipliers. SFX volume covers play()/playAt(); music + UI are
    // separate named buses. setUiVolume drives playUI().
    void setMasterVolume(float volume);
    void setSfxVolume(float volume);
    void setMusicVolume(float volume);
    void setUiVolume(float volume);

    // Music ducking (task 44): scales the *current* music volume by `factor`
    // (0..1) without losing the configured base volume. duckMusic(0.4f) drops
    // the track to 40% for stingers / death screens; duckMusic(1.f) restores it.
    // The configured base is whatever was last passed to setMusicVolume.
    void duckMusic(float factor);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ds
