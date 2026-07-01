#include "engine/AudioSystem.h"

#include "engine/Paths.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <filesystem>
#include <miniaudio.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ds {

namespace {
// One-shots are short; a modest cap keeps the active-voice list bounded without
// ever cutting off a sound mid-play (finished voices are reclaimed each call).
constexpr std::size_t kMaxActiveVoices = 64;
} // namespace

struct AudioSystem::Impl {
    ma_engine engine{};
    bool engineReady = false;

    ma_sound_group sfxGroup{};
    ma_sound_group musicGroup{};
    ma_sound_group uiGroup{};
    bool groupsReady = false;

    // Configured (un-ducked) music volume; duckMusic scales this so the base
    // set by settings is never lost. Seeded to 1 to match the master default.
    float baseMusicVolume = 1.f;

    // Decoded source sounds, cached by resolved path. Heap-allocated so the
    // address seen by miniaudio's node graph stays stable across map rehashes.
    std::unordered_map<std::string, std::unique_ptr<ma_sound>> sources;
    // Paths we already failed to load, so we log once and stay quiet after.
    std::unordered_set<std::string> missing;

    // Fire-and-forget voices we own and must free once finished.
    std::vector<ma_sound*> activeVoices;

    bool music = false;
    ma_sound musicSound{};

    void reapFinished() {
        for (auto it = activeVoices.begin(); it != activeVoices.end();) {
            ma_sound* s = *it;
            if (ma_sound_at_end(s)) {
                ma_sound_uninit(s);
                delete s;
                it = activeVoices.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Returns a loaded source sound for path, or nullptr if the file is absent
    // or failed to decode. Logs missing files exactly once.
    ma_sound* source(const std::string& path) {
        auto found = sources.find(path);
        if (found != sources.end())
            return found->second.get();

        if (missing.count(path))
            return nullptr;

        std::filesystem::path full = paths::assets() / path;
        std::error_code ec;
        if (!std::filesystem::exists(full, ec)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "AudioSystem: sound file missing, skipping: %s", full.string().c_str());
            missing.insert(path);
            return nullptr;
        }

        auto ptr    = std::make_unique<ma_sound>();
        ma_result r = ma_sound_init_from_file(&engine, full.string().c_str(),
                                              MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, nullptr,
                                              ptr.get());
        if (r != MA_SUCCESS) {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "AudioSystem: failed to load sound (%d): %s", (int)r,
                        full.string().c_str());
            missing.insert(path);
            return nullptr;
        }
        auto* raw     = ptr.get();
        sources[path] = std::move(ptr);
        return raw;
    }

    // Spawns an owned copy of a cached source. Caller configures + starts it.
    ma_sound* spawnVoice(ma_sound* src, ma_sound_group* group, bool spatial) {
        reapFinished();
        if (activeVoices.size() >= kMaxActiveVoices)
            return nullptr;

        ma_uint32 flags = spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
        ma_sound* voice = new ma_sound{};
        ma_result r     = ma_sound_init_copy(&engine, src, flags, group, voice);
        if (r != MA_SUCCESS) {
            delete voice;
            return nullptr;
        }
        activeVoices.push_back(voice);
        return voice;
    }
};

AudioSystem::AudioSystem() = default;

AudioSystem::~AudioSystem() {
    shutdown();
}

bool AudioSystem::init() {
    if (m_impl)
        return m_impl->engineReady;

    m_impl = std::make_unique<Impl>();

    ma_result r = ma_engine_init(nullptr, &m_impl->engine);
    if (r != MA_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "AudioSystem: ma_engine_init failed (%d); audio disabled", (int)r);
        m_impl.reset();
        return false;
    }
    m_impl->engineReady = true;

    // Separate buses let SFX and music carry independent volumes.
    bool sfxOk =
        ma_sound_group_init(&m_impl->engine, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &m_impl->sfxGroup) == MA_SUCCESS;
    bool musicOk = ma_sound_group_init(&m_impl->engine, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr,
                                       &m_impl->musicGroup) == MA_SUCCESS;
    bool uiOk =
        ma_sound_group_init(&m_impl->engine, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &m_impl->uiGroup) == MA_SUCCESS;
    m_impl->groupsReady = sfxOk && musicOk && uiOk;
    if (!m_impl->groupsReady)
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "AudioSystem: sound group init failed; using master bus");

    return true;
}

void AudioSystem::shutdown() {
    if (!m_impl)
        return;

    if (m_impl->engineReady) {
        if (m_impl->music) {
            ma_sound_uninit(&m_impl->musicSound);
            m_impl->music = false;
        }
        for (ma_sound* s : m_impl->activeVoices) {
            ma_sound_uninit(s);
            delete s;
        }
        m_impl->activeVoices.clear();

        for (auto& [path, snd] : m_impl->sources)
            ma_sound_uninit(snd.get());
        m_impl->sources.clear();

        if (m_impl->groupsReady) {
            ma_sound_group_uninit(&m_impl->sfxGroup);
            ma_sound_group_uninit(&m_impl->musicGroup);
            ma_sound_group_uninit(&m_impl->uiGroup);
        }
        ma_engine_uninit(&m_impl->engine);
    }
    m_impl.reset();
}

bool AudioSystem::initialized() const {
    return m_impl && m_impl->engineReady;
}

void AudioSystem::play(const std::string& path) {
    if (!initialized())
        return;
    ma_sound* src = m_impl->source(path);
    if (!src)
        return;
    ma_sound_group* group = m_impl->groupsReady ? &m_impl->sfxGroup : nullptr;
    ma_sound* voice       = m_impl->spawnVoice(src, group, false);
    if (voice)
        ma_sound_start(voice);
}

void AudioSystem::playUI(const std::string& path) {
    if (!initialized())
        return;
    ma_sound* src = m_impl->source(path);
    if (!src)
        return;
    // UI cues route through the dedicated UI bus when groups are available so
    // their volume is independent of SFX; otherwise fall back to the SFX bus.
    ma_sound_group* group = m_impl->groupsReady ? &m_impl->uiGroup : nullptr;
    ma_sound* voice       = m_impl->spawnVoice(src, group, false);
    if (voice)
        ma_sound_start(voice);
}

void AudioSystem::playAt(const std::string& path, const glm::vec3& position) {
    if (!initialized())
        return;
    ma_sound* src = m_impl->source(path);
    if (!src)
        return;
    ma_sound_group* group = m_impl->groupsReady ? &m_impl->sfxGroup : nullptr;
    ma_sound* voice       = m_impl->spawnVoice(src, group, true);
    if (!voice)
        return;
    ma_sound_set_position(voice, position.x, position.y, position.z);
    ma_sound_start(voice);
}

void AudioSystem::playMusic(const std::string& path) {
    if (!initialized())
        return;

    if (m_impl->music) {
        ma_sound_uninit(&m_impl->musicSound);
        m_impl->music = false;
    }

    std::filesystem::path full = paths::assets() / path;
    std::error_code ec;
    if (!std::filesystem::exists(full, ec)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "AudioSystem: music file missing, skipping: %s", full.string().c_str());
        return;
    }

    ma_sound_group* group = m_impl->groupsReady ? &m_impl->musicGroup : nullptr;
    ma_result r           = ma_sound_init_from_file(&m_impl->engine, full.string().c_str(),
                                                    MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION, group, nullptr,
                                                    &m_impl->musicSound);
    if (r != MA_SUCCESS) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "AudioSystem: failed to load music (%d): %s", (int)r,
                    full.string().c_str());
        return;
    }
    ma_sound_set_looping(&m_impl->musicSound, MA_TRUE);
    ma_sound_start(&m_impl->musicSound);
    m_impl->music = true;
}

void AudioSystem::stopMusic() {
    if (!initialized() || !m_impl->music)
        return;
    ma_sound_uninit(&m_impl->musicSound);
    m_impl->music = false;
}

void AudioSystem::setListener(const glm::vec3& position, const glm::vec3& forward) {
    if (!initialized())
        return;
    ma_engine_listener_set_position(&m_impl->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_impl->engine, 0, forward.x, forward.y, forward.z);
}

// All bus volumes are 0..1 multipliers. Clamp here at the boundary so a corrupt
// or hand-edited settings.cfg (e.g. a negative or >1 volume) can never reach
// miniaudio as a phase-inverting or grossly clipping gain.
void AudioSystem::setMasterVolume(float volume) {
    if (!initialized())
        return;
    ma_engine_set_volume(&m_impl->engine, std::clamp(volume, 0.f, 1.f));
}

void AudioSystem::setSfxVolume(float volume) {
    if (!initialized() || !m_impl->groupsReady)
        return;
    ma_sound_group_set_volume(&m_impl->sfxGroup, std::clamp(volume, 0.f, 1.f));
}

void AudioSystem::setMusicVolume(float volume) {
    if (!initialized())
        return;
    // Remember the configured base so duckMusic can scale relative to it.
    m_impl->baseMusicVolume = std::clamp(volume, 0.f, 1.f);
    if (m_impl->groupsReady)
        ma_sound_group_set_volume(&m_impl->musicGroup, m_impl->baseMusicVolume);
}

void AudioSystem::setUiVolume(float volume) {
    if (!initialized() || !m_impl->groupsReady)
        return;
    ma_sound_group_set_volume(&m_impl->uiGroup, std::clamp(volume, 0.f, 1.f));
}

void AudioSystem::duckMusic(float factor) {
    if (!initialized() || !m_impl->groupsReady)
        return;
    // baseMusicVolume is already clamped; clamp the duck factor too so the
    // product stays a sane attenuation.
    ma_sound_group_set_volume(&m_impl->musicGroup, m_impl->baseMusicVolume * std::clamp(factor, 0.f, 1.f));
}

} // namespace ds
