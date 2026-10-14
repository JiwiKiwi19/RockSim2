#include "particlekernel.h"
#include <iostream>

#ifdef HIP_ENABLED
#include <hip/hip_runtime.h>

/*
 * GPU kernel for updating particles. We are using a grid-stride loop instead of a typical monolothic kernel
 * to make this more flexible. Instead of assuming we have enough threads, the threads are evenly spread out
 * across the array
 */
__global__ void updateParticlesKernel(Particle *particles, int count, float boundary, float damping, bool downwardsGravity)
{

    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = blockDim.x * gridDim.x;

    for (int i = idx; i < count; i += stride)
    {
        Particle &p = particles[i];

        // Update positions
        p.x += p.vx;
        p.y += p.vy;

        // Boundary collision detection and response
        bool hitX = (p.x < -boundary || p.x > boundary);
        bool hitY = (p.y < -boundary || p.y > boundary);

        if (hitX)
        {
            p.vx = -p.vx * damping; // Particle loses energy each time it bounces
            p.x = fmaxf(-boundary, fminf(boundary, p.x));
        }

        if (hitY)
        {
            p.vy = -p.vy * damping; // Particle loses energy each time it bounces
            p.y = fmaxf(-boundary, fminf(boundary, p.y));
        }

        // Gravity effect
        if (downwardsGravity)
        {
            p.vy -= 0.0001f;
        }
    }
}
#endif

/*
 * Constructor method for the ParticleKernel class. It checks if the HIP device is available and
 * prints out GPU details, otherwise it goes to CPU fallback. It also starts the
 */
ParticleKernel::ParticleKernel()
#ifdef HIP_ENABLED
    : d_particles(nullptr), h_pinnedParticles(nullptr), allocatedCount(0), hipInitialized(false), stream(nullptr)
#endif
{
#ifdef HIP_ENABLED
    // Check if HIP device is available
    int deviceCount = 0;
    hipError_t err = hipGetDeviceCount(&deviceCount);
    if (err == hipSuccess && deviceCount > 0)
    {
        // Set device properties for optimal performance
        hipDeviceProp_t prop;
        hipGetDeviceProperties(&prop, 0);

        std::cout << "HIP GPU: " << prop.name << std::endl;
        std::cout << "Compute Units: " << prop.multiProcessorCount << std::endl;
        std::cout << "Max Threads Per Block: " << prop.maxThreadsPerBlock << std::endl;
        std::cout << "Shared Memory Per Block: " << prop.sharedMemPerBlock / 1024 << " KB" << std::endl;

        hipInitialized = true;
        hipStreamCreate(&stream);

        std::cout << "HIP GPU acceleration initialized with async streams" << std::endl;
    }
    else
    {
        std::cout << "HIP not available, using CPU fallback" << std::endl;
    }
#endif
}

/*
 * Destructor method. Upon destruction of this object class, we free the memory in
 * our HIP device and destroy the stream.
 */
ParticleKernel::~ParticleKernel()
{
#ifdef HIP_ENABLED
    freeDeviceMemory();
    if (stream)
    {
        auto ret = hipStreamDestroy(stream);
        stream = nullptr;
    }
#endif
}

/*
 * Getter method to see if HIP plugin has been initialized.
 */
bool ParticleKernel::isHIPAvailable() const
{
#ifdef HIP_ENABLED
    return hipInitialized;
#else
    return false;
#endif
}

#ifdef HIP_ENABLED

/*
 * First we deallocate current particle's data and we reallocate memory for the new,
 * updated particles. Then for fast access to the data, we allocate memory in RAM for
 * fast access later on.
 */
void ParticleKernel::allocateDeviceMemory(int count)
{
    if (allocatedCount != count)
    {
        freeDeviceMemory();

        // Allocate device memory
        hipError_t err = hipMalloc(&d_particles, count * sizeof(Particle));
        if (err != hipSuccess)
        {
            std::cerr << "HIP malloc failed: " << hipGetErrorString(err) << std::endl;
            hipInitialized = false;
            return;
        }

        // Allocate pinned host memory for faster transfers
        err = hipHostMalloc(&h_pinnedParticles, count * sizeof(Particle), hipHostMallocDefault);
        if (err != hipSuccess)
        {
            std::cerr << "HIP host malloc failed: " << hipGetErrorString(err) << std::endl;
            hipFree(d_particles);
            d_particles = nullptr;
            hipInitialized = false;
            return;
        }

        allocatedCount = count;
        std::cout << "Allocated GPU memory for " << count << " particles (" << (count * sizeof(Particle)) / (1024 * 1024) << " MB)" << std::endl;
    }
}

/*
 * Free the device memory for the HIP device and that which is in the RAM. After which,
 * we make sure their pointers are null to ensure they're not pointing to random data.
 */
void ParticleKernel::freeDeviceMemory()
{
    if (d_particles)
    {
        hipFree(d_particles);
        d_particles = nullptr;
    }
    if (h_pinnedParticles)
    {
        hipHostFree(h_pinnedParticles);
        h_pinnedParticles = nullptr;
    }
    allocatedCount = 0;
}
#endif

/*
 * The main method which essentially starts off all the other methods to update the particles. The process in the method
 * is as follows:
 *
 * 1) Copy the particle data into pinned memory (RAM) for faster CPU access which can then quickly give to GPU
 * 2) Asynchronously copy from pinned memory into HIP device memory
 * 3) Calculate the parameters for the kernel function which is then launched
 * 4) Asynchronously copy from back from HIP device to pinned memory
 * 5) Wait for previous operations to complete to synchronize the stream
 * 6) Copy the particle data from pinned memory back into the original array
 */
bool ParticleKernel::updateParticlesGPU(Particle *particles, int count, float boundary, float damping, bool downwardsGravity)
{
#ifdef HIP_ENABLED
    if (!hipInitialized)
    {
        return false;
    }

    // Allocate device memory if needed (only happens on first call or size change)
    allocateDeviceMemory(count);
    if (!d_particles)
    {
        return false;
    }

    // Copy to pinned memory first (fast since it stays in RAM)
    memcpy(h_pinnedParticles, particles, count * sizeof(Particle));

    // Asynchronous copy from host to device (computer does't wait for this to complete, goes on to next operations since it's asynchronous)
    hipError_t err = hipMemcpyAsync(d_particles, h_pinnedParticles, count * sizeof(Particle),
                                    hipMemcpyHostToDevice, stream);
    if (err != hipSuccess)
    {
        std::cerr << "HIP memcpy H2D failed: " << hipGetErrorString(err) << std::endl;
        return false;
    }

    // Calculate optimal launch configuration using occupancy calculator
    int minGridSize, blockSize;
    hipOccupancyMaxPotentialBlockSize(&minGridSize, &blockSize, updateParticlesKernel, 0, 0);

    // Get device properties
    hipDeviceProp_t prop;
    hipGetDeviceProperties(&prop, 0);

    // Calculate grid size to maximize GPU utilization
    // Use more blocks than compute units to keep GPU fully busy
    int blocksPerGrid = std::min((count + blockSize - 1) / blockSize,
                                 prop.multiProcessorCount * 4);

    // Launch kernel on the stream (will execute after H2D transfer completes)
    hipLaunchKernelGGL(updateParticlesKernel,
                       dim3(blocksPerGrid),
                       dim3(blockSize),
                       0, stream,
                       d_particles, count, boundary, damping, downwardsGravity);

    // Asynchronous copy back from device to host (this is not needed to be async but in case we want to
    // add other operations in between and such, we will keep it async)
    err = hipMemcpyAsync(h_pinnedParticles, d_particles, count * sizeof(Particle),
                         hipMemcpyDeviceToHost, stream);
    if (err != hipSuccess)
    {
        std::cerr << "HIP memcpy D2H failed: " << hipGetErrorString(err) << std::endl;
        return false;
    }

    // Wait for all operations in the stream to complete
    hipStreamSynchronize(stream);

    // Copy from pinned memory back to original array
    memcpy(particles, h_pinnedParticles, count * sizeof(Particle));

    return true;
#else
    return false;
#endif
}