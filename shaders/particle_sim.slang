// GPU compute particle simulation (task 39, Enhanced tier only).
//
// Integrates a pool of particles on the GPU and writes the per-instance
// billboard buffer consumed by particle.slang's vertMain. This removes the
// per-frame synchronous CPU re-upload of the instance buffer on the Enhanced
// tier; the Minimum tier keeps the CPU ParticleSystem path.
//
// Bindings (SDL3 GPU compute convention):
//   * read-only storage buffer  slot 0 : GpuParticle pool (state)
//   * read-write storage buffer slot 0 : Instance pool (billboard output)
//   * uniform                   slot 0 : SimParams (dt, gravity, count)
//
// State advancement is owned by the CPU pool (ParticleSystem::update already
// integrated the pool to this frame before the snapshot upload); this pass is a
// pure pass-through PACK — it copies each live particle's already-current
// position/size/color into the instance buffer WITHOUT advancing it. This makes
// the GPU compute output identical to the CPU fallback path
// (ParticleSystem::buildInstances), which also packs p.position unchanged. An
// earlier version extrapolated one extra dt step here (pos += vel*dt), which
// made the GPU-drawn positions lead the CPU pool by one frame (task 58). Keeping
// state CPU-side avoids a second RW buffer + ping-pong while still moving the
// instance-buffer fill (the hot per-frame upload) onto the GPU.
//
// NOTE: dt/gravity are retained in SimParams for ABI stability (the host still
// uploads them) but are unused now that the pass no longer integrates.

struct GpuParticle {
    float3 position;
    float size;
    float4 color;
    float3 velocity;
    float life; // remaining seconds; <= 0 means dead
};

struct Instance {
    float3 position;
    float size;
    float4 color;
};

struct SimParams {
    float dt;
    float gravity;
    uint count; // number of valid particles in the pool
    float pad;
};

[vk::binding(0, 0)]
StructuredBuffer<GpuParticle> gParticles; // read-only state

[vk::binding(0, 1)]
RWStructuredBuffer<Instance> gInstances; // read-write billboard output

[vk::binding(0, 2)]
ConstantBuffer<SimParams> gParams;

[shader("compute")]
[numthreads(64, 1, 1)]
void compMain(uint3 tid : SV_DispatchThreadID) {
    uint i = tid.x;
    if (i >= gParams.count)
        return;

    GpuParticle p = gParticles[i];

    Instance inst;
    if (p.life <= 0.0) {
        // Dead particle: collapse to a zero-size, transparent instance so the
        // billboard quad degenerates and contributes nothing.
        inst.position = float3(0.0, 0.0, 0.0);
        inst.size     = 0.0;
        inst.color    = float4(0.0, 0.0, 0.0, 0.0);
    } else {
        // Pass-through pack: emit the particle's already-integrated state
        // unchanged so the compute path matches the CPU buildInstances() output
        // exactly (no extra dt extrapolation — see the header note, task 58).
        inst.position = p.position;
        inst.size     = p.size;
        inst.color    = p.color;
    }

    gInstances[i] = inst;
}
