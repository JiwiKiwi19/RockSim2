#ifndef PARTICLEKERNEL_H
#define PARTICLEKERNEL_H

#ifdef HIP_ENABLED
#include <hip/hip_runtime.h>
#endif
#include <vector>

struct Particle
{
    float x, y;    // Position
    float vx, vy;  // Velocity
    float r, g, b; // Color
    float radius;  // Collision Radius
    float mass;
};

// Spatial grid for collision detection
struct GridCell
{
    std::vector<int> particleIndices;
};

class ParticleKernel
{
public:
    ParticleKernel();
    ~ParticleKernel();

    bool updateParticlesGPU(Particle *particles, int count, float boundary, float damping, bool downwardsGravity);
    bool isHIPAvailable() const;

private:
#ifdef HIP_ENABLED
    Particle *d_particles;       // Device memory pointer
    Particle *h_pinnedParticles; // Pinned host memory for faster transfers
    int allocatedCount;
    bool hipInitialized;

    hipStream_t stream; // For async operations

    void allocateDeviceMemory(int count);
    void freeDeviceMemory();
#endif
};

#endif // PARTICLEKERNEL_H