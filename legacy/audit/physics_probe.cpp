// Audit harness: drives the REAL ParticlesGLWidget physics (compiled from the original
// particlesglwidget.cpp) headless. `private` is opened only so we can call the
// internal step functions directly; no physics code is modified.
#define private public
#define protected public
#include "particlesglwidget.h"
#undef private
#undef protected
#include <QApplication>
#include <cstdio>
#include <cmath>
#include <random>

static Particle mk(float x,float y,float vx,float vy,float m){
    Particle p{}; p.x=x;p.y=y;p.vx=vx;p.vy=vy;p.mass=m;
    p.radius = ParticlesGLWidget::BASE_RADIUS*std::cbrt(m/ParticlesGLWidget::BASE_MASS); return p;}
static void runCollide(ParticlesGLWidget& w){ w.clearGrid(); w.populateGrid(); w.handleParticleCollisions(); }
static double px(const std::vector<Particle>& ps){double s=0;for(auto&p:ps)s+=p.mass*p.vx;return s;}
static double ke(const std::vector<Particle>& ps){double s=0;for(auto&p:ps)s+=0.5*p.mass*(p.vx*p.vx+p.vy*p.vy);return s;}

int main(int argc,char**argv){
    QApplication app(argc,argv);
    ParticlesGLWidget w(nullptr, 5000);
    std::printf("cellSize=%.4f grid=%dx%d BASE_RADIUS=%.3f\n", w.cellSize,w.gridWidth,w.gridHeight,ParticlesGLWidget::BASE_RADIUS);

    // E1: particle-count argument
    w.initializeParticles();
    std::printf("[E1] constructed with initialParticleCount=5000 -> particles.size()=%zu\n", w.particles.size());

    // E2: two overlapping, approaching particles, each alone in adjacent cells
    w.damping=1.0f;
    float c=w.cellSize; float x0=-1.0f+10*c+c-0.006f, x1=-1.0f+11*c+0.006f; // straddle a cell edge
    w.particles={mk(x0,0.01f,+0.01f,0,1), mk(x1,0.01f,-0.01f,0,1)};
    std::printf("[E2] cells %d,%d; dist=%.4f < 2r=%.4f\n", w.getGridIndex(x0,0.01f), w.getGridIndex(x1,0.01f), x1-x0, 2*w.particles[0].radius);
    runCollide(w);
    std::printf("[E2] after narrow phase: vA=%.4f vB=%.4f (unchanged => collision missed)\n", w.particles[0].vx, w.particles[1].vx);
    // same pair, add a 3rd particle to A's cell
    w.particles={mk(x0,0.01f,+0.01f,0,1), mk(x1,0.01f,-0.01f,0,1), mk(x0-0.02f,-0.02f,0,0,1)};
    runCollide(w);
    std::printf("[E2b] with a bystander in A's cell: vA=%.4f vB=%.4f\n", w.particles[0].vx, w.particles[1].vx);

    // E3: statistical miss rate vs brute force, 100 random particles, default settings
    {
        std::mt19937 g(42); std::uniform_real_distribution<float> P(-0.8f,0.8f),V(-0.01f,0.01f);
        int missed=0,total=0;
        for(int trial=0;trial<2000;++trial){
            w.particles.clear();
            for(int i=0;i<100;++i) w.particles.push_back(mk(P(g),P(g),V(g),V(g),1));
            // brute-force: overlapping & approaching pairs
            auto before=w.particles; std::vector<std::pair<int,int>> pairs;
            for(int i=0;i<100;++i)for(int j=i+1;j<100;++j){auto&a=before[i];auto&b=before[j];
                float dx=b.x-a.x,dy=b.y-a.y,d=std::sqrt(dx*dx+dy*dy); if(d<a.radius+b.radius && d>1e-4f){
                    float dvn=(b.vx-a.vx)*dx/d+(b.vy-a.vy)*dy/d; if(dvn<0) pairs.push_back({i,j});}}
            runCollide(w);
            for(auto [i,j]:pairs){ ++total; if(w.particles[i].vx==before[i].vx && w.particles[i].vy==before[i].vy) ++missed; }
        }
        std::printf("[E3] approaching overlapping pairs: %d, not resolved by grid: %d (%.1f%%)\n", total, missed, 100.0*missed/std::max(total,1));
    }

    // E4: momentum with unequal masses (head-on, damping=1 i.e. intended elastic)
    w.damping=1.0f;
    w.particles={mk(0.0f,0.0f,+0.01f,0,1), mk(0.012f,0.0f,-0.01f,0,8), mk(0.0f,0.03f,0,0,1)};
    w.particles.pop_back(); // keep 2 (both now in same cell)
    { auto b=w.particles; runCollide(w);
      std::printf("[E4] m=1,8 head-on, damping=1: p before=%.5f after=%.5f | KE before=%.3e after=%.3e\n", px(b), px(w.particles), ke(b), ke(w.particles)); }

    // E5: effective restitution for walls vs particle pairs as a function of the damping slider
    for(float d: {0.0f,0.25f,0.5f,0.75f,1.0f}){
        w.damping=d;
        w.particles={mk(0.0f,0.0f,+0.01f,0,1), mk(0.015f,0.0f,-0.01f,0,1)}; runCollide(w);
        float ePair=(w.particles[1].vx-w.particles[0].vx)/0.02f;
        w.particles={mk(0.999f,0.0f,+0.01f,0,1)}; w.enableDownwardsGravity=false; w.updateParticlesCPU();
        float eWall=-w.particles[0].vx/0.01f;
        std::printf("[E5] slider=%3d damping=%.2f  wall e=%.2f  pair e=%.2f\n",(int)std::lround((1-d)*100),d,eWall,ePair);
    }

    // E6: gravity depends on mass in CPU path
    w.enableDownwardsGravity=true; w.damping=1;
    w.particles={mk(-0.5f,0.5f,0,0,1), mk(0.5f,0.5f,0,0,10)};
    for(int i=0;i<60;++i) w.updateParticlesCPU();
    std::printf("[E6] after 60 ticks from y=0.5: m=1 y=%.4f  m=10 y=%.4f (free fall should be mass-independent)\n", w.particles[0].y, w.particles[1].y);

    // E7: large mass -> radius exceeds grid cell
    w.damping=1; w.enableDownwardsGravity=false;
    { Particle a=mk(0.0f,0.0f,+0.01f,0,100); float r=a.radius; Particle b=mk(0.0f+1.8f*r,0.0f,-0.01f,0,100);
      // place so they are 2 cells apart
      w.particles={a,b};
      std::printf("[E7] mass=100 -> r=%.4f (2r=%.4f vs cell=%.4f); cells %d vs %d\n", r,2*r,w.cellSize,w.getGridIndex(a.x,a.y),w.getGridIndex(b.x,b.y));
      w.particles.push_back(mk(a.x+0.001f,a.y+0.001f,0,0,1)); // ensure A's cell has >=2
      runCollide(w); std::printf("[E7] overlapping heavy pair after narrow phase: vA=%.4f vB=%.4f\n", w.particles[0].vx,w.particles[1].vx); }

    // E8: single ball, gravity on, perfectly elastic walls: energy over 20k ticks
    w.damping=1; w.enableDownwardsGravity=true;
    w.particles={mk(0.0f,0.5f,0.0f,0,1)};
    auto E=[&]{auto&p=w.particles[0]; return 0.5*(p.vx*p.vx+p.vy*p.vy) + 0.0001*p.mass*(p.y+1.0);}; // g_eff=1e-4*m per tick^2
    double E0=E(); double mn=E0,mx=E0;
    for(int i=0;i<20000;++i){ w.updateParticlesCPU(); double e=E(); mn=std::min(mn,e); mx=std::max(mx,e);} 
    std::printf("[E8] bouncing ball, damping=1: E0=%.3e final=%.3e (%.1f%%) min=%.3e max=%.3e\n",E0,E(),100*(E()-E0)/E0,mn,mx);
    return 0;
}
