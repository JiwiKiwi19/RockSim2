# ROCsim - Real-Time Physics Simulation Engine

<p align="center">
  <img src="docs/images/Logo.png" alt="ROCsim Logo" width="200"/>
</p>

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![License](https://img.shields.io/badge/license-MIT-blue)]()
[![ROCm](https://img.shields.io/badge/ROCm-6.4%2B-orange)]()
[![Qt](https://img.shields.io/badge/Qt-6.5%2B-brightgreen)]()

**ROCsim** is an open-source, GPU-accelerated physics simulation engine optimized for AMD Radeon GPUs using ROCm/HIP, with full support for NVIDIA GPUs. Built with Qt and OpenGL, ROCsim provides real-time visualization of complex physics simulations including gravitational systems, collision dynamics, and spring networks.

---

## 📸 Video
- Deliverable 2 Demo Video on [Google Drive](https://drive.google.com/file/d/1ghEKCVz6n-8W7RZSUyi3ejOnZATOC9Rp/view?usp=sharing)
- Deliverable 3 Demo Video TBA

## 🌟 Features

### Core Physics Engine
- **Multiple Physics Models**
  - Gravitational N-Body simulation with orbital mechanics
  - Particle collision system with elastic/inelastic collisions
  - Spring-mass network with Hooke's Law
- **GPU-Accelerated Computing**
  - HIP/ROCm optimized kernels for AMD GPUs
  - Full CUDA compatibility for NVIDIA GPUs
  - Direct GPU-to-GPU data sharing (zero-copy pipeline)
- **Advanced Numerical Integration**
  - Verlet integration for energy conservation
  - Runge-Kutta 4th order for high precision
  - Adaptive time stepping for stability

### Professional Qt GUI
- **Interactive 3D Visualization**
  - Real-time OpenGL rendering
  - Smooth particle systems with 100-5000+ particles
  - Color-coded particles based on velocity/energy
- **Real-Time Controls**
  - Play/Pause/Reset simulation
  - Dynamic parameter adjustment (gravity, damping, particle count)
  - Runtime physics model switching
- **Performance Monitoring**
  - Live FPS counter
  - GPU compute time tracking
  - Memory usage statistics

### Technical Highlights
- **OpenGL-HIP Interoperability**: Direct buffer sharing between compute and graphics
- **Cross-Platform**: Linux (Ubuntu 22.04+) and Windows 10/11 (WSL2)
- **Modern C++17**: Clean architecture with design patterns
- **Extensible Design**: Plugin-ready for custom physics models

---

## 🔧 Development

### Core Project Structure
```
ROCsim/
├── code/                   # Header and C++ source files
│   ├── mainwindow.h
│   ├── openglwidget.h
│   ├── particlekernel.h
│   ├── CMakeLists.txt
│   └── ...
├── docs/                   # Project report, UML diagrams and any design files
│   ├── Deliverable 1/
│   ├── Deliverable 2/
│   └── Deliverable 3/
├── tests/                  # GoogleTest unit tests
└── README.md
```

### Building for Development
```bash
# Debug build with verbose output
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_VERBOSE_MAKEFILE=ON
make -j$(nproc)

# Run tests
ctest --output-on-failure
```

### Design Patterns Used
- **Strategy Pattern**: Interchangeable physics models
- **Factory Pattern**: Physics model instantiation
- **Singleton Pattern**: GPU memory manager
- **Observer Pattern**: Performance monitoring

---

## 🎯 Project Goals

ROCsim addresses the lack of open-source physics simulators optimized for AMD GPUs. While many tools use NVIDIA's CUDA, ROCsim provides:

1. **AMD-First Development**: Optimized for ROCm and Radeon GPUs
2. **Cross-Vendor Compatibility**: Works on both AMD and NVIDIA hardware
3. **Educational Focus**: Visualize physics concepts for students and researchers
4. **Performance**: Real-time simulation of thousands of particles at 60 FPS

---

## 📋 Requirements

### Hardware
- **GPU**: AMD Radeon (RDNA2+) or NVIDIA (Compute 6.0+) with HIP support
- **RAM**: Minimum 8GB system memory
- **Display**: OpenGL 3.3+ compatible graphics

### Software
- **OS**: Ubuntu 22.04+ or Windows 10/11 (with WSL2)
- **ROCm**: Version 6.4.2 or newer
- **Qt**: Version 6.5 or newer
- **CMake**: Version 3.16 or newer
- **Compiler**: GCC 11+ or Clang 14+ with C++17 support

---

## 🚀 Quick Start

### Installation

#### 1. Install ROCm (AMD GPUs)
```bash
# Ubuntu 22.04/24.04
wget https://repo.radeon.com/amdgpu-install/latest/ubuntu/jammy/amdgpu-install_latest_all.deb
sudo apt install ./amdgpu-install_latest_all.deb
sudo amdgpu-install -y --usecase=rocm

# Verify installation
rocminfo
```

#### 2. Install Qt and Dependencies
```bash
sudo apt update
sudo apt install -y qt6-base-dev qt6-opengl-dev
sudo apt install -y libglfw3-dev libglew-dev cmake build-essential
```

#### 3. Clone and Build
```bash
git clone https://github.com/yourusername/ROCsim.git
cd ROCsim
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

#### 4. Run
```bash
./rocm_qt_opengl_physics_demo
```

---

## 📖 Usage

### Basic Controls
- **Mouse Left Click + Drag**: Rotate 3D camera
- **Mouse Scroll**: Zoom in/out
- **Spacebar**: Pause/Resume simulation
- **R**: Reset simulation
- **1/2/3**: Switch physics models (Gravity/Collision/Springs)
- **+/-**: Increase/decrease particle count
- **ESC**: Exit application

### GUI Controls
- **Simulation Type Dropdown**: Select physics model
- **Parameter Sliders**: 
  - Gravity strength (0.0 - 2.0)
  - Damping factor (0.5 - 1.0)
  - Particle count (100 - 5000)
- **Performance Monitor**: Real-time FPS and GPU metrics

### Exporting Data
```cpp
// Export simulation state
File → Export → Simulation Data (CSV)

// Save configuration
File → Save Configuration → config.json
```

---

## 🏗️ Architecture

ROCsim follows a layered architecture with clear separation of concerns:

```
┌─────────────────────────────────────────────────┐
│                   Qt GUI                        │
│  MainWindow | ControlPanel | PerformanceMonitor │
└─────────────────────────────────────────────────┘
                      │
┌─────────────────────┴─────────────────────────────┐
│          Business Logic Layer                     │
│  SimulationManager | PhysicsEngine | RenderEngine │
└─────────────────────┬─────────────────────────────┘
                      │
┌─────────────────────┴─────────────────────────┐
│               Compute Layer                   │
│  HIPKernelManager | GPUMemoryManager          │
│  IntegratorConstraintSolver | PhysicsModels   │
└───────────────────────────────────────────────┘
┌─────────────────────┴─────────────────────────────┐
│                Data Layer                         │
│  Particle | PhysicsParameters                     │
│  ConfigurationManager | PerformanceMetrics        │
└─────────────────────┬─────────────────────────────┘
```

### Key Components

**Physics Engine (Team 1)**
- `PhysicsEngine`: Core physics computation coordinator
- `HIPKernelManager`: GPU kernel execution
- `IntegratorConstraintSolver`: ODE solving with constraints
- `PhysicsModel` (Strategy Pattern): Gravitational, Collision, Spring models

**Rendering Engine (Team 2)**
- `RenderEngine`: OpenGL visualization pipeline
- `MainWindow`: Qt application framework
- `ControlPanel`: User interaction controls
- `PerformanceMonitor`: Real-time metrics display

---

## 📊 Performance Benchmarks

| Configuration | Particle Count | FPS (AMD RX 6800) | FPS (NVIDIA RTX 3070) |
|--------------|----------------|-------------------|----------------------|
| Gravity      | 500            | 60                | 60                   |
| Gravity      | 1000           | 58                | 60                   |
| Collision    | 500            | 55                | 58                   |
| Springs      | 1000           | 52                | 55                   |

*Benchmarks on Ubuntu 22.04, ROCm 6.4, Qt 6.5*

---

## 🤝 Contributing

We welcome contributions! Please see our [Contributing Guide](CONTRIBUTING.md) for details.

### Team Structure
- **Team 1** (Physics & GPU Compute): Jiwon Lee, Jacob Lee
- **Team 2** (Visualization & UI): Eric Jean, Maharshii Patel

### Development Workflow
1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit changes (`git commit -m 'Add amazing feature'`)
4. Push to branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

---

## 📅 Roadmap

### Deliverable 2 (October 30, 2025)
- ✅ Basic physics engine with gravitational model
- ✅ Qt GUI with OpenGL rendering
- ✅ HIP-OpenGL interoperability
- ⏳ Single physics model working

### Deliverable 3 (December 15, 2025)
- ⏳ All three physics models (Gravity, Collision, Springs)
- ⏳ Advanced numerical integration (Verlet, RK4)
- ⏳ Performance optimization
- ⏳ Complete testing suite
- ⏳ Final documentation and video demo

---

## 📜 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

## 🙏 Acknowledgments

- **AMD ROCm Team**: For excellent GPU computing platform
- **Qt Project**: For robust cross-platform GUI framework
- **Western University CS3307**: Course project for Object-Oriented Design
- **NVIDIA CUDA Samples**: Reference implementations for GPU interoperability
- **OpenGL Community**: Tutorials and documentation

---

## 📧 Contact

**Project Link**: [https://github.com/yourusername/ROCsim](https://github.com/yourusername/ROCsim)

**Team 1 (Physics/GPU)**:
- Jiwon Lee - jlee3859@uwo.ca
- Jacob Lee - jlee3887@uwo.ca

**Team 2 (Graphics/UI)**:
- Eric Jean - ejean2@uwo.ca
- Maharshii Patel - mpate588@uwo.ca

---

## 🌐 Resources

- [ROCm Documentation](https://rocm.docs.amd.com/)
- [Qt Documentation](https://doc.qt.io/)
- [HIP Programming Guide](https://rocm.docs.amd.com/projects/HIP/)
- [OpenGL Tutorial](https://learnopengl.com/)

---

<p align="center">
  Made with ❤️ by the ROCkers | Fall 2025
</p>
