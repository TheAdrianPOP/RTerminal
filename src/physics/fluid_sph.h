#pragma once

#include "engine_config.h"
#include "vector.h"
#include "math_utils.h"

struct FluidSPHConfig {
    float h             = kSPHDefaultH;
    float rest_density  = kSPHDefaultRestDensity;
    float stiffness     = kSPHDefaultStiffness;
    float viscosity     = kSPHDefaultViscosity;
    float particle_mass = kSPHDefaultParticleMass;
    float bound_stiff   = kSPHDefaultBoundStiff;
    Vec3  bound_min     = kSPHDefaultBoundMin;
    Vec3  bound_max     = kSPHDefaultBoundMax;
    float min_density   = 0.001f;
};

struct FluidParticle {
    Vec3  position;
    Vec3  velocity;
    Vec3  accel;
    float density  = 1.f;
    float pressure = 0.f;
    bool  active   = true;
};

struct SPHKernels {
    float h, h2, h6, h9;
    float poly6_coeff;
    float spiky_coeff;
    float visc_coeff;

    explicit SPHKernels(float sr) {
        h=sr; h2=h*h; h6=h2*h2*h2; h9=h6*h2*h;
        poly6_coeff =  315.f / (64.f * PI * h9);
        spiky_coeff = -45.f  / (PI * h6);
        visc_coeff  =  45.f  / (PI * h6);
    }

    FORCE_INLINE float poly6(float r2) const {
        if (r2 >= h2) return 0.f;
        float d = h2 - r2;
        return poly6_coeff * d * d * d;
    }

    FORCE_INLINE float spiky_grad(float r) const {
        if (r <= 0.f || r >= h) return 0.f;
        float d = h - r;
        return spiky_coeff * d * d;
    }

    FORCE_INLINE float visc_lap(float r) const {
        if (r <= 0.f || r >= h) return 0.f;
        return visc_coeff * (h - r);
    }
};

template<int MAX_PARTICLES, int HASH_SIZE = MAX_PARTICLES * 4>
struct SPHSpatialHash {
    static constexpr int INVALID = -1;
    int head[HASH_SIZE];
    int next[MAX_PARTICLES];
    unsigned int cell_key[MAX_PARTICLES];

    void clear() {
        for (int i=0;i<HASH_SIZE;++i) head[i]=INVALID;
        for (int i=0;i<MAX_PARTICLES;++i) next[i]=INVALID;
    }

    FORCE_INLINE unsigned int makeKey(int cx,int cy,int cz) const {
        return (unsigned)((cx+1024)*4096*4096+(cy+1024)*4096+(cz+1024));
    }

    FORCE_INLINE int hashCell(int cx,int cy,int cz) const {
        unsigned h=(unsigned)(cx*92837111^cy*689287499^cz*283923481);
        return (int)(h%(unsigned)HASH_SIZE);
    }

    void insert(int idx, const Vec3& pos, float inv_h) {
        int cx=(int)fast_floor(pos.x*inv_h);
        int cy=(int)fast_floor(pos.y*inv_h);
        int cz=(int)fast_floor(pos.z*inv_h);
        int k=hashCell(cx,cy,cz);
        cell_key[idx]=makeKey(cx,cy,cz);
        next[idx]=head[k]; head[k]=idx;
    }

    template<typename Fn>
    void query(const Vec3& pos, float inv_h, Fn&& fn) const {
        int cx=(int)fast_floor(pos.x*inv_h);
        int cy=(int)fast_floor(pos.y*inv_h);
        int cz=(int)fast_floor(pos.z*inv_h);
        for(int dz=-1;dz<=1;++dz)
        for(int dy=-1;dy<=1;++dy)
        for(int dx=-1;dx<=1;++dx){
            unsigned int target=makeKey(cx+dx,cy+dy,cz+dz);
            int k=hashCell(cx+dx,cy+dy,cz+dz);
            int i=head[k];
            while(i!=INVALID){
                if(cell_key[i]==target) fn(i);
                i=next[i];
            }
        }
    }
};

template<int MAX_PARTICLES>
struct FluidSPH {
    FluidParticle                 particles[MAX_PARTICLES];
    int                           count = 0;
    SPHSpatialHash<MAX_PARTICLES> hash;
    FluidSPHConfig                cfg;
    SPHKernels                    kern;

    FluidSPH()                          : kern(kSPHDefaultH) {}
    explicit FluidSPH(const FluidSPHConfig& c) : cfg(c), kern(c.h) {}

    bool add(Vec3 pos, Vec3 vel=Vec3(0,0,0)) {
        if (count>=MAX_PARTICLES) return false;
        auto& p=particles[count++];
        p.position=pos; p.velocity=vel; p.accel=Vec3(0,0,0);
        p.density=cfg.rest_density; p.pressure=0.f; p.active=true;
        return true;
    }

    void clear(){ count=0; }

    void step(Vec3 gravity, float dt) {
        if (count==0) return;

        const float h     = kern.h;
        const float h2    = kern.h2;
        const float inv_h = 1.f/h;
        const float m     = cfg.particle_mass;

        hash.clear();
        for(int i=0;i<count;++i) hash.insert(i,particles[i].position,inv_h);

        for(int i=0;i<count;++i){
            FluidParticle& pi=particles[i];
            float rho=0.f;
            hash.query(pi.position,inv_h,[&](int j){
                float r2=(pi.position-particles[j].position).magnitudeSq();
                rho+=m*kern.poly6(r2);
            });
            pi.density  = fast_max(rho, cfg.min_density);
            pi.pressure = fast_max(0.f, cfg.stiffness*(pi.density-cfg.rest_density));
        }

        for(int i=0;i<count;++i){
            FluidParticle& pi=particles[i];
            Vec3 a_press(0,0,0), a_visc(0,0,0);

            hash.query(pi.position,inv_h,[&](int j){
                if(j==i) return;
                FluidParticle& pj=particles[j];
                Vec3  rij=pi.position-pj.position;
                float r2 =rij.magnitudeSq();
                if(r2>=h2||r2<=EPSILON_SQ) return;
                float r  =fast_sqrt(r2);
                Vec3  rn =rij*(1.f/r);

                float pterm=pi.pressure/(pi.density*pi.density)
                           +pj.pressure/(pj.density*pj.density);
                a_press += rn*(m*pterm*(-kern.spiky_grad(r)));

                Vec3 vij=pj.velocity-pi.velocity;
                a_visc  += vij*(cfg.viscosity*m/pj.density*kern.visc_lap(r));
            });

            pi.accel = a_press + a_visc + gravity;
        }

        for(int i=0;i<count;++i){
            FluidParticle& p=particles[i];
            p.velocity += p.accel * dt;
            p.position += p.velocity * dt;

            auto bound=[&](float& x,float& vx,float lo,float hi){
                if(x<lo){ vx+=cfg.bound_stiff*(lo-x)*dt; x=lo; }
                if(x>hi){ vx+=cfg.bound_stiff*(hi-x)*dt; x=hi; }
            };
            bound(p.position.x,p.velocity.x,cfg.bound_min.x,cfg.bound_max.x);
            bound(p.position.y,p.velocity.y,cfg.bound_min.y,cfg.bound_max.y);
            bound(p.position.z,p.velocity.z,cfg.bound_min.z,cfg.bound_max.z);
        }
    }

    float totalKineticEnergy() const {
        float ke=0.f; const float m=cfg.particle_mass;
        for(int i=0;i<count;++i) ke+=0.5f*m*particles[i].velocity.magnitudeSq();
        return ke;
    }

    Vec3 centerOfMass() const {
        if(!count) return Vec3(0,0,0);
        Vec3 s(0,0,0);
        for(int i=0;i<count;++i) s+=particles[i].position;
        return s*(1.f/(float)count);
    }

    Vec3 totalMomentum() const {
        Vec3 p(0,0,0); const float m=cfg.particle_mass;
        for(int i=0;i<count;++i) p+=particles[i].velocity*m;
        return p;
    }

    float averageDensity() const {
        if(!count) return 0.f;
        float s=0.f;
        for(int i=0;i<count;++i) s+=particles[i].density;
        return s/(float)count;
    }
};