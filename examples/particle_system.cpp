// demo_3d.cpp — Live terminal demo: 3D particles + SPH fluid + 3D smoke grid.
//
// Layout:  [  SPARKS  ] [ FLUID MESH ] [ 3D SMOKE ] [  CONFIG UI  ]
//
// Build:
//   g++ -std=c++17 -O2 -msse4.1 -I. demo_3d.cpp \
//       console_utils.cpp display.cpp render_utils.cpp ui.cpp -o demo_3d

#include "perf_timer.h"
#include "display.h"
#include "console_utils.h"
#include "input.h"
#include "engine_config.h"
#include "math_utils.h"
#include "matrix.h"
#include "particle_system.h"
#include "fluid_sph.h"
#include "fluid_grid.h"
#include "vector.h"
#include "ui.h"
#include <cstdio>
#include <cstring>
#include <csignal>

static volatile int g_running = 1;
static void onSigint(int) { g_running = 0; }

// ── Camera ────────────────────────────────────────────────────────────────────
struct Camera {
    Vec3  pos, fwd, right, up;
    float fov_x;

    void lookAt(Vec3 eye, Vec3 target, float fov_deg) {
        pos = eye; fov_x = degToRad(fov_deg);
        fwd = (target - eye).normalize();
        Vec3 wu = fast_abs(fwd.y) > 0.99f ? Vec3(1,0,0) : Vec3(0,1,0);
        right = fwd.cross(wu).normalize();
        up    = right.cross(fwd).normalize();
    }
};

static Camera arcballCamera(Vec3 target, float radius,
                              float yaw, float pitch, float fov_deg) {
    pitch = fast_clamp(pitch, -1.5f, 1.5f);
    float cp = fast_cos(pitch), sp = fast_sin(pitch);
    float cy = fast_cos(yaw),   sy = fast_sin(yaw);
    Camera c;
    c.lookAt(target + Vec3(radius*cp*cy, radius*sp, radius*cp*sy), target, fov_deg);
    return c;
}

// ── Border drawing ────────────────────────────────────────────────────────────
static void drawBorder(ConsoleCell* buf, int bw, int bh,
                        int x0, int y0, int w, int h,
                        const char* title, RGBColor col)
{
    auto set = [&](int x, int y, char ch, RGBColor c) {
        if (x>=0&&x<bw&&y>=0&&y<bh)
            buf[y*bw+x] = ConsoleCell(ConsoleGlyph(ch), c, true, true);
    };
    for (int x=x0;x<x0+w;++x) { set(x,y0,'-',col); set(x,y0+h-1,'-',col); }
    for (int y=y0;y<y0+h;++y) { set(x0,y,'|',col); set(x0+w-1,y,'|',col); }
    set(x0,y0,'+',col); set(x0+w-1,y0,'+',col);
    set(x0,y0+h-1,'+',col); set(x0+w-1,y0+h-1,'+',col);
    if (title) {
        int len=0; while(title[len]) len++;
        int tx = x0+(w-len)/2;
        for (int i=0;i<len;++i) set(tx+i,y0,title[i],{255,255,180});
    }
}

// Helper: write into panel-clipped cell
static FORCE_INLINE bool panelCell(ConsoleCell* buf, int bw, int bh,
                                    int ox, int oy, int pw, int ph,
                                    int lx, int ly, ConsoleCell cell)
{
    int bx=ox+1+lx, by=oy+1+ly;
    if (lx<0||lx>=pw-2||ly<0||ly>=ph-2||bx<0||bx>=bw||by<0||by>=bh) return false;
    buf[by*bw+bx] = cell;
    return true;
}

// ── Scene A: particle sparks ──────────────────────────────────────────────────
struct SceneSparks {
    static constexpr int MAXP = 5012;
    ParticlePool<MAXP>  pool;
    ParticleEmitter     emitter;
    ParticleRng         rng{9999u};

    float gravity     = 4.f;
    float speed_scale = 1.f;
    float color_temp  = 1.f;
    float life_scale  = 1.f;

    void init() {
        emitter.origin    = Vec3(0,0,0);
        emitter.direction = Vec3(0,1,0);
        emitter.spread    = 3.6f;
        emitter.speed_min = 1.5f; emitter.speed_max = 4.f;
        emitter.life_min  = 0.8f; emitter.life_max  = 2.0f;
        emitter.color     = {255,160,40};
        emitter.rate      = 80.f;
        emitter.active    = true;
    }

    void update(float dt) {
        static float explosion_timer = 0.f;
        explosion_timer -= dt;
        if (explosion_timer <= 0.f) {
            explosion_timer = rng.nextRange(1.0f, 2.5f);
            Vec3 origin(rng.nextRange(-1.5f,1.5f),
                        rng.nextRange(-0.5f,1.5f),
                        rng.nextRange(-1.5f,1.5f));
            float ct = fast_clamp(color_temp, 0.f, 2.f);
            RGBColor burst_col;
            if (ct <= 1.f) {
                burst_col = {(int16_t)(int)(ct*255.f),
                             (int16_t)(int)(60+ct*100.f),
                             (int16_t)(int)(200-ct*160.f)};
            } else {
                float f = ct-1.f;
                burst_col = {255,
                             (int16_t)(int)(160+f*95.f),
                             (int16_t)(int)(40+f*215.f)};
            }
            int num = (int)emitter.rate;
            float sm = 4.0f * speed_scale;
            for (int i=0;i<num;++i) {
                float theta = rng.nextRange(0.f, TWO_PI);
                float phi   = fast_cos(rng.nextRange(-1.f,1.f));
                float st    = fast_sin(phi);
                Vec3 dir(st*fast_cos(theta), fast_cos(phi), st*fast_sin(theta));
                float speed = rng.nextRange(sm*0.4f, sm*1.6f);
                float life  = rng.nextRange(0.8f*life_scale, 2.0f*life_scale);
                RGBColor col = burst_col;
                col.r = (int16_t)fast_clamp(col.r+rng.nextRange(-20.f,20.f),0.f,255.f);
                col.g = (int16_t)fast_clamp(col.g+rng.nextRange(-20.f,20.f),0.f,255.f);
                col.b = (int16_t)fast_clamp(col.b+rng.nextRange(-20.f,20.f),0.f,255.f);
                pool.spawn(origin, dir*speed, life, col);
            }
        }
        for (int i=0;i<pool.capacity();++i)
            if (pool.particles[i].active)
                pool.particles[i].velocity *= fast_clamp(1.f-2.5f*dt, 0.f, 1.f);

        emitter.active = false;
        updateParticles(pool, emitter, rng, dt, Vec3(0.f,-gravity,0.f));
    }

    void render(ConsoleCell* buf, int bw, int bh,
                int ox, int oy, int pw, int ph, const Camera& cam) const
    {
        const float half_w = (float)(pw-2)*0.5f, half_h = (float)(ph-2)*0.5f;
        const float fov_y  = cam.fov_x*((float)(ph-2)/(float)(pw-2));
        const float tan_x  = fast_tan(cam.fov_x*0.5f);
        const float tan_y  = fast_tan(fov_y*0.5f);

        for (int i=0;i<pool.capacity();++i) {
            const Particle& p = pool.particles[i];
            if (!p.active) continue;
            Vec3  rel   = p.position - cam.pos;
            float depth = rel.dot(cam.fwd);
            if (depth <= 0.01f) continue;
            float rx = rel.dot(cam.right)/depth, ry = rel.dot(cam.up)/depth;
            if (fast_abs(rx)>tan_x||fast_abs(ry)>tan_y) continue;
            int sx=(int)(half_w+rx/tan_x*half_w);
            int sy=(int)(half_h-ry/tan_y*half_h);

            float t     = fast_clamp(p.life/fast_max(p.max_life,0.01f),0.f,1.f);
            float speed = p.velocity.magnitude();
            char g = t>0.8f ? (speed>5.f?'|':'#')
                   : t>0.5f ? (speed>3.f?'+':'*')
                   : t>0.2f ? ';' : ',';

            RGBColor col = p.color;
            if (color_temp < 1.5f && t < 0.6f) { col.g=(int16_t)(col.g*t); col.b=(int16_t)(col.b*t); }
            col.r=(int16_t)(col.r*t); col.g=(int16_t)(col.g*t); col.b=(int16_t)(col.b*t);
            panelCell(buf,bw,bh,ox,oy,pw,ph,sx,sy,ConsoleCell(ConsoleGlyph(g),col,true,false));
        }
    }
};

// ── Scene B: SPH fluid mesh ───────────────────────────────────────────────────
struct SceneSPH {
    FluidSPHConfig cfg;
    FluidSPH<512>  fluid;

    float gravity     = 6.f;
    float iso_thresh  = 0.55f;
    float mb_radius   = 4.5f;
    bool  do_reset    = false;
    bool  show_glow   = true;
    bool  show_contour= true;
    bool  flow_mode   = true;

    // Per-frame accumulation buffers (avoid static locals)
    PodVector<float> density_field, depth_field, vel_field;

    void init() {
        cfg = {0.12f, 0.8f, 12.f, 0.2f, 0.005f, 200.f,
               Vec3(-0.8f,-0.8f,-0.8f), Vec3(0.8f,0.8f,0.8f)};
        fluid = FluidSPH<512>(cfg);
        for (int z=0;z<8;++z) for (int y=0;y<8;++y) for (int x=0;x<8;++x)
            fluid.add(Vec3(-0.21f+x*0.06f, 0.2f+y*0.06f, -0.21f+z*0.06f));
    }

    void update(float dt) {
        if (do_reset) { init(); do_reset=false; }
        cfg.bound_min.y = flow_mode ? -2.5f : -0.8f;
        fluid.cfg = cfg;
        const float sub_dt = 0.004f;
        int steps = fast_max(1,(int)(dt/sub_dt));
        for (int s=0;s<steps;++s) {
            fluid.step(Vec3(0.f,-gravity,0.f), sub_dt);
            if (flow_mode) {
                for (int i=0;i<fluid.count;++i) {
                    if (fluid.particles[i].position.y < -0.9f) {
                        float rx=-0.15f+(rand()%300)/1000.f;
                        float rz=-0.15f+(rand()%300)/1000.f;
                        fluid.particles[i].position = Vec3(rx, cfg.bound_max.y-0.1f, rz);
                        fluid.particles[i].velocity = Vec3(0,-1.0f,0);
                    }
                }
            }
        }
    }

    void render(ConsoleCell* buf, int bw, int bh,
                int ox, int oy, int pw, int ph, const Camera& cam)
    {
        const int IW=pw-2, IH=ph-2;
        if (IW<=0||IH<=0) return;
        const float half_w=(float)IW*0.5f, half_h=(float)IH*0.5f;
        const float fov_y=cam.fov_x*((float)IH/(float)IW);
        const float tan_x=fast_tan(cam.fov_x*0.5f), tan_y=fast_tan(fov_y*0.5f);
        const int fsz=IW*IH;

        density_field.resize(fsz); depth_field.resize(fsz); vel_field.resize(fsz);
        memset(density_field.data(), 0, fsz*sizeof(float));
        for (int i=0;i<fsz;++i) depth_field[i]=1e30f;
        memset(vel_field.data(), 0, fsz*sizeof(float));

        const float MB_R  = fast_max(2.f, (float)IW*(mb_radius/100.f));
        const float MB_R2 = MB_R*MB_R;

        for (int i=0;i<fluid.count;++i) {
            const FluidParticle& p = fluid.particles[i];
            Vec3  rel   = p.position-cam.pos;
            float depth = rel.dot(cam.fwd);
            if (depth<=0.01f) continue;
            float rx=rel.dot(cam.right)/depth, ry=rel.dot(cam.up)/depth;
            if (fast_abs(rx)>tan_x*1.5f||fast_abs(ry)>tan_y*1.5f) continue;
            float sx=half_w+rx/tan_x*half_w, sy=half_h-ry/tan_y*half_h;
            float vel_mag=p.velocity.magnitude();
            int cx0=fast_max(0,(int)(sx-MB_R)), cx1=fast_min(IW-1,(int)(sx+MB_R+1));
            int cy0=fast_max(0,(int)(sy-MB_R)), cy1=fast_min(IH-1,(int)(sy+MB_R+1));
            for (int gy=cy0;gy<=cy1;++gy) for (int gx=cx0;gx<=cx1;++gx) {
                float dx=gx-sx, dy=gy-sy, d2=dx*dx+dy*dy;
                if (d2>=MB_R2) continue;
                float w=(1.f-d2/MB_R2); w*=w;
                int fi=gy*IW+gx;
                density_field[fi]+=w;
                if (depth<depth_field[fi]) { depth_field[fi]=depth; vel_field[fi]=vel_mag; }
            }
        }

        const float ISO=iso_thresh;
        Vec3 light_dir=Vec3(-0.5f,0.8f,0.5f).normalize();

        // Pass 1.5: glow interior
        if (show_glow)
        for (int gy=1;gy<IH-1;++gy) for (int gx=1;gx<IW-1;++gx) {
            float d=density_field[gy*IW+gx];
            if (d<ISO*0.35f) continue;
            float dx2=density_field[gy*IW+gx+1]-density_field[gy*IW+gx-1];
            float dy2=density_field[(gy+1)*IW+gx]-density_field[(gy-1)*IW+gx];
            Vec3 normal=Vec3(-dx2,-dy2,0.8f).normalize();
            float vt=fast_clamp(vel_field[gy*IW+gx]/4.f,0.f,1.f);
            float ndotl=fast_max(0.f,normal.dot(light_dir));
            float spec=fast_pow(ndotl,6.f), diffuse=0.4f+ndotl*0.6f;
            RGBColor c2 = {
                (int16_t)fast_clamp((int)(20+vt*20+spec*230),0,255),
                (int16_t)fast_clamp((int)(80+vt*60+diffuse*80+spec*230),0,255),
                (int16_t)fast_clamp((int)(160+vt*40+diffuse*90+spec*230),0,255)
            };
            panelCell(buf,bw,bh,ox,oy,pw,ph,gx,gy,
                      ConsoleCell(ConsoleGlyph("\xe2\x96\x88"),c2,false,false));
        }

        // Pass 2: marching-squares contour
        // bits: 3=TL 2=TR 1=BR 0=BL
        static const char ms_lut[16] = {
            ' ', '\\', '/', '-',
            '\\', '|', '-', '\\',
            '/', '-',  '|', '/',
            '-', '/',  '\\', ' '
        };
        if (show_contour)
        for (int gy=0;gy<IH-1;++gy) for (int gx=0;gx<IW-1;++gx) {
            float d00=density_field[gy*IW+gx], d10=density_field[gy*IW+gx+1];
            float d01=density_field[(gy+1)*IW+gx], d11=density_field[(gy+1)*IW+gx+1];
            int cfg2=((d00>=ISO)?8:0)|((d10>=ISO)?4:0)|((d11>=ISO)?2:0)|((d01>=ISO)?1:0);
            if (cfg2==0||cfg2==15) continue;
            char g=ms_lut[cfg2]; if (g==' ') continue;
            float avg_vel=(vel_field[gy*IW+gx]+vel_field[gy*IW+gx+1]+
                           vel_field[(gy+1)*IW+gx]+vel_field[(gy+1)*IW+gx+1])*0.25f;
            float avg_dep=(depth_field[gy*IW+gx]+depth_field[gy*IW+gx+1]+
                           depth_field[(gy+1)*IW+gx]+depth_field[(gy+1)*IW+gx+1])*0.25f;
            float vt=fast_clamp(avg_vel/4.f,0.f,1.f);
            float dt2=fast_clamp(1.f-avg_dep*0.4f,0.f,1.f);
            RGBColor ecol = {
                (int16_t)fast_clamp((int)(30+vt*200+dt2*40),0,255),
                (int16_t)fast_clamp((int)(160+vt*60+dt2*50),0,255),
                (int16_t)fast_clamp((int)(230+vt*25+dt2*25),0,255)
            };
            panelCell(buf,bw,bh,ox,oy,pw,ph,gx,gy,
                      ConsoleCell(ConsoleGlyph(g),ecol,true,true));
        }
    }
};

// ── Scene C: 3D smoke grid ────────────────────────────────────────────────────
struct SceneSmoke {
    static constexpr int GW=30, GH=30, GD=30;
    FluidGrid<GW,GH,GD> grid;
    float time=0.f;
    float swirl_speed  = 2.5f;
    float density_rate = 80.f;
    float rise_speed   = 4.5f;
    float dissipation  = 0.4f;
    bool  fire_mode    = false;

    // Accumulation buffers (member, not static)
    PodVector<float> accum_vol, accum_r, accum_g, accum_b;

    void init() { grid.clear(); }

    void update(float dt) {
        time += dt;
        int cx=GW/2, cy=2, cz=GD/2;
        float su=fast_sin(time*swirl_speed)*1.5f, sw=fast_cos(time*swirl_speed)*1.5f;
        for (int oz=-1;oz<=1;++oz) for (int ox2=-1;ox2<=1;++ox2) {
            float inj=(ox2==0&&oz==0)?0.8f:0.3f;
            grid.addDensity(cx+ox2,cy,cz+oz,inj*dt*density_rate);
            grid.addVelocity(cx+ox2,cy,cz+oz,su*ox2,rise_speed,sw*oz);
        }
        stepFluid3D(grid,0.0001f,0.00005f,dissipation,dt);
    }

    void render(ConsoleCell* buf, int bw, int bh,
                int ox, int oy, int pw, int ph, const Camera& cam)
    {
        const int IW=pw-2, IH=ph-2;
        if (IW<=0||IH<=0) return;
        const int fsz=IW*IH;
        accum_vol.resize(fsz); accum_r.resize(fsz);
        accum_g.resize(fsz);   accum_b.resize(fsz);
        memset(accum_vol.data(),0,fsz*sizeof(float));
        memset(accum_r.data(),  0,fsz*sizeof(float));
        memset(accum_g.data(),  0,fsz*sizeof(float));
        memset(accum_b.data(),  0,fsz*sizeof(float));

        const float half_w=(float)IW*0.5f, half_h=(float)IH*0.5f;
        const float fov_y=cam.fov_x*((float)IH/(float)IW);
        const float tan_x=fast_tan(cam.fov_x*0.5f), tan_y=fast_tan(fov_y*0.5f);
        const float cell_size=1.f/(float)GW;
        const Vec3  grid_origin(-0.5f,-0.5f,-0.5f);
        Vec3 light_dir=Vec3(-1.f,1.f,-0.5f).normalize();

        for (int kk=0;kk<GD;++kk) for (int j=0;j<GH;++j) for (int i=0;i<GW;++i) {
            float d=grid.density[FluidGrid<GW,GH,GD>::idx(i,j,kk)];
            if (d<0.02f) continue;
            Vec3 world(grid_origin.x+((float)i+0.5f)*cell_size,
                       grid_origin.y+((float)j+0.5f)*cell_size,
                       grid_origin.z+((float)kk+0.5f)*cell_size);
            Vec3  rel   = world-cam.pos;
            float depth = rel.dot(cam.fwd);
            if (depth<=0.01f) continue;
            float rx=rel.dot(cam.right)/depth, ry=rel.dot(cam.up)/depth;
            if (fast_abs(rx)>tan_x*1.5f||fast_abs(ry)>tan_y*1.5f) continue;
            int sx=(int)(half_w+rx/tan_x*half_w), sy=(int)(half_h-ry/tan_y*half_h);

            float ddx=grid.density[grid.idx(fast_clamp(i+1,0,GW-1),j,kk)]-
                      grid.density[grid.idx(fast_clamp(i-1,0,GW-1),j,kk)];
            float ddy=grid.density[grid.idx(i,fast_clamp(j+1,0,GH-1),kk)]-
                      grid.density[grid.idx(i,fast_clamp(j-1,0,GH-1),kk)];
            float ddz=grid.density[grid.idx(i,j,fast_clamp(kk+1,0,GD-1))]-
                      grid.density[grid.idx(i,j,fast_clamp(kk-1,0,GD-1))];
            Vec3 normal(-ddx,-ddy,-ddz);
            float nlen=normal.magnitude();
            float lighting=0.35f;
            if (nlen>0.0001f) {
                normal.x/=nlen; normal.y/=nlen; normal.z/=nlen;
                lighting+=0.65f*fast_max(0.f,normal.dot(light_dir));
            }
            float h_ratio=(float)j/(float)GH;
            bool  is_fire=fire_mode&&(h_ratio<0.2f&&d>0.25f);
            float rc,gc,bc;
            if (is_fire) {
                float f=(0.2f-h_ratio)*5.f;
                rc=255.f; gc=80.f+175.f*f; bc=20.f+40.f*f;
            } else {
                float lum=fast_clamp((90.f+d*150.f)*lighting,0.f,255.f);
                rc=lum; gc=lum+5.f; bc=lum+15.f;
            }
            float splat_r=fast_max(1.f,2.5f/depth);
            int ir=(int)(splat_r+0.5f); float r2=splat_r*splat_r;
            for (int dy_=-ir;dy_<=ir;++dy_) for (int dx_=-ir;dx_<=ir;++dx_) {
                float dist2=(float)(dx_*dx_+dy_*dy_);
                if (dist2>r2) continue;
                float w=1.f-dist2/r2; w*=w;
                int px=sx+dx_, py=sy+dy_;
                if (px>=0&&px<IW&&py>=0&&py<IH) {
                    int pi=py*IW+px; float val=d*w;
                    accum_vol[pi]+=val; accum_r[pi]+=rc*val;
                    accum_g[pi]+=gc*val; accum_b[pi]+=bc*val;
                }
            }
        }

        for (int py=0;py<IH;++py) for (int px=0;px<IW;++px) {
            float vol=accum_vol[py*IW+px];
            if (vol<0.05f) continue;
            float inv=1.f/fast_max(vol,0.0001f);
            float alpha=fast_clamp(vol*2.f,0.f,1.f);
            RGBColor col = {
                (int16_t)fast_clamp((int)(accum_r[py*IW+px]*inv*alpha),0,255),
                (int16_t)fast_clamp((int)(accum_g[py*IW+px]*inv*alpha),0,255),
                (int16_t)fast_clamp((int)(accum_b[py*IW+px]*inv*alpha),0,255)
            };
            panelCell(buf,bw,bh,ox,oy,pw,ph,px,py,
                      ConsoleCell(ConsoleGlyph("\xe2\x96\x88"),col,false,false));
        }
    }
};

// ── Config UI overlay ─────────────────────────────────────────────────────────
class DemoConfigUI : public UI {
public:
    FluidSPHConfig*  cfg_sph        = nullptr;
    ParticleEmitter* cfg_sparks     = nullptr;
    float* smoke_swirl=nullptr, *smoke_density=nullptr;
    float* auto_rot_f=nullptr;  // not used — kept for compat
    bool*  auto_rot=nullptr;
    float* zoom=nullptr;
    float* sparks_gravity=nullptr, *sparks_speed=nullptr;
    float* sparks_color_temp=nullptr, *sparks_life=nullptr;
    float* sph_gravity=nullptr, *sph_iso=nullptr, *sph_mb_radius=nullptr;
    bool*  sph_show_glow=nullptr, *sph_show_contour=nullptr;
    bool*  sph_flow_mode=nullptr, *sph_reset=nullptr;
    float* smoke_rise=nullptr, *smoke_dissipation=nullptr;
    bool*  smoke_fire_mode=nullptr;

    bool handleMouseWidget(int x, int y, int w, int h, bool& clicked, bool& dragging) {
        if (!input_ptr) return false;
        Vec2 m=input_ptr->getMousePosition();
        bool over=(m.u>=x&&m.u<x+w&&m.v>=y&&m.v<y+h);
        clicked =over&&input_ptr->isMouseButtonPressed(MOUSE_LEFT);
        dragging=over&&input_ptr->isMouseButtonDown(MOUSE_LEFT);
        return over;
    }

    void InteractiveSlider(const char* label, float* val, float mn, float mx, float step) {
        int bar_w=ctx.window_width-15;
        bool clicked, dragging;
        bool hover=handleMouseWidget(ctx.cursor_x,ctx.cursor_y,ctx.window_width,1,clicked,dragging);
        if (dragging) {
            Vec2 m=input_ptr->getMousePosition();
            float t=fast_clamp((m.u-(ctx.cursor_x+12))/(float)bar_w,0.f,1.f);
            *val=fast_round((mn+t*(mx-mn))/step)*step;
        }
        MenuSlider(label,val,mn,mx,step);
    }

    void InteractiveToggle(const char* label, bool* val) {
        bool clicked, dragging;
        handleMouseWidget(ctx.cursor_x,ctx.cursor_y,ctx.window_width,1,clicked,dragging);
        if (clicked) *val=!(*val);
        MenuToggle(label,val);
    }

    void InteractiveButton(const char* label, bool* flag) {
        bool clicked, dragging;
        bool hover=handleMouseWidget(ctx.cursor_x,ctx.cursor_y,ctx.window_width,1,clicked,dragging);
        if (clicked) *flag=true;
        TextColored(hover?Colors::SELECTED:Colors::VALUE,"  [%s]",label);
    }

    void render(PodVector<PodVector<ConsoleCell>>& buffer, int w, int h) override {
        ctx.buffer=&buffer; ctx.screen_w=w; ctx.screen_h=h;
        Begin(" DEMO CONFIG ",w-34,0,34,h,Colors::BORDER);
        if (input_ptr) {
            Vec2 mp=input_ptr->getMousePosition();
            TextColored(Colors::CURSOR," %3.0f,%3.0f  LDrag:Orbit",mp.u,mp.v);
            TextColored(Colors::INACTIVE," RDrag:Pan  Wheel:Zoom");
            TextColored(Colors::INACTIVE," DblClick:Reset  Ctrl+C:Quit");
        }
        Separator();
        TextColored(Colors::TITLE," [SYSTEM]");
        if (auto_rot) InteractiveToggle(" AutoRotate",auto_rot);
        if (zoom)     InteractiveSlider(" Zoom",zoom,0.1f,5.0f,0.05f);
        Separator();
        if (cfg_sparks) {
            TextColored(Colors::TITLE," [SPARKS]");
            InteractiveSlider(" Rate",  &cfg_sparks->rate,  10.f,600.f,10.f);
            InteractiveSlider(" Spread",&cfg_sparks->spread,0.5f,12.f,0.5f);
            if (sparks_speed)      InteractiveSlider(" Speed",  sparks_speed,     0.2f,4.f,0.1f);
            if (sparks_gravity)    InteractiveSlider(" Gravity",sparks_gravity,   0.f,15.f,0.5f);
            if (sparks_color_temp) InteractiveSlider(" Color",  sparks_color_temp,0.f,2.f,0.05f);
            if (sparks_life)       InteractiveSlider(" Life",   sparks_life,      0.2f,4.f,0.1f);
            Separator();
        }
        if (cfg_sph) {
            TextColored(Colors::TITLE," [FLUID MESH]");
            InteractiveSlider(" Viscosity",&cfg_sph->viscosity,    0.f,1.f,0.02f);
            InteractiveSlider(" Stiffness",&cfg_sph->stiffness,    1.f,80.f,1.f);
            InteractiveSlider(" Mass",     &cfg_sph->particle_mass,0.001f,0.03f,0.001f);
            if (sph_gravity)     InteractiveSlider(" Gravity",  sph_gravity,   0.f,20.f,0.5f);
            if (sph_iso)         InteractiveSlider(" ISO",      sph_iso,       0.1f,1.5f,0.05f);
            if (sph_mb_radius)   InteractiveSlider(" MBRadius", sph_mb_radius, 1.f,12.f,0.5f);
            if (sph_show_glow)   InteractiveToggle(" Glow",    sph_show_glow);
            if (sph_show_contour)InteractiveToggle(" Contour", sph_show_contour);
            if (sph_flow_mode)   InteractiveToggle(" FlowMode",sph_flow_mode);
            if (sph_reset)       InteractiveButton("Reset Fluid",sph_reset);
            Separator();
        }
        if (smoke_swirl&&smoke_density) {
            TextColored(Colors::TITLE," [SMOKE]");
            InteractiveSlider(" Swirl",  smoke_swirl,  0.f,10.f,0.5f);
            InteractiveSlider(" Density",smoke_density,10.f,300.f,10.f);
            if (smoke_rise)        InteractiveSlider(" Rise",  smoke_rise,       0.5f,12.f,0.5f);
            if (smoke_dissipation) InteractiveSlider(" Dissip",smoke_dissipation,0.05f,1.5f,0.05f);
            if (smoke_fire_mode)   InteractiveToggle(" FireMode",smoke_fire_mode);
        }
        End();
    }
};

// =============================================================================
int main() {
    std::signal(SIGINT, onSigint);
    setupConsoleMode();

    int W=0, H=0;
    getConsoleSize(W, H);

    Display      display;
    InputManager input;
    display.Resize(W, H);

    GraphicsConfig gfx;
    gfx.pixel_mode=true; gfx.color_mode=ColorMode::TrueColor; gfx.show_ui=false;

    SceneSparks sparks; sparks.init();
    SceneSPH    sph;    sph.init();
    SceneSmoke  smoke;  smoke.init();

    DemoConfigUI config_ui;
    config_ui.bindInput(&input);
    config_ui.cfg_sph          = &sph.cfg;
    config_ui.cfg_sparks       = &sparks.emitter;
    config_ui.smoke_swirl      = &smoke.swirl_speed;
    config_ui.smoke_density    = &smoke.density_rate;
    config_ui.sparks_gravity   = &sparks.gravity;
    config_ui.sparks_speed     = &sparks.speed_scale;
    config_ui.sparks_color_temp= &sparks.color_temp;
    config_ui.sparks_life      = &sparks.life_scale;
    config_ui.sph_gravity      = &sph.gravity;
    config_ui.sph_iso          = &sph.iso_thresh;
    config_ui.sph_mb_radius    = &sph.mb_radius;
    config_ui.sph_show_glow    = &sph.show_glow;
    config_ui.sph_show_contour = &sph.show_contour;
    config_ui.sph_flow_mode    = &sph.flow_mode;
    config_ui.sph_reset        = &sph.do_reset;
    config_ui.smoke_rise       = &smoke.rise_speed;
    config_ui.smoke_dissipation= &smoke.dissipation;
    config_ui.smoke_fire_mode  = &smoke.fire_mode;

    bool  auto_rotate = true;
    float orbit_t=0.f, yaw_offset=0.f, pitch_offset=0.f;
    float zoom_mult=1.f, zoom_target=1.f;
    Vec3  pan_offset(0.f,0.f,0.f);
    config_ui.auto_rot = &auto_rotate;
    config_ui.zoom     = &zoom_mult;

    // Inertia for left-drag orbit
    float yaw_vel=0.f, pitch_vel=0.f;
    float last_left_press_t=-9.f, global_t=0.f;

    // 2D ui_buffer — allocated once, reallocated only on resize
    PodVector<PodVector<ConsoleCell>> ui_buffer;
    int last_W=-1, last_H=-1;

    PerfTimer frame_clock;
    frame_clock.start();

    while (g_running) {
        float dt = fast_clamp((float)(frame_clock.getElapsedMs()*0.001), 0.001f, 0.1f);
        frame_clock.start();
        global_t += dt;

        input.update();

        const int UI_W=34;
        const int sim_W=W-UI_W, PANEL_W=sim_W/3, PANEL_H=H-2;

        Vec2 mp=input.getMousePosition(), md=input.getMouseDelta();
        bool over_sim = (mp.u >= 0 && mp.u < (float)sim_W);

        // Left drag: orbit with inertia
        if (over_sim && input.isMouseButtonDown(MOUSE_LEFT)) {
            auto_rotate = false;
            float idt = fast_max(dt, 0.001f);
            yaw_vel   =  md.u * 0.022f / idt;
            pitch_vel = -md.v * 0.022f / idt;
            yaw_offset   += md.u * 0.022f;
            pitch_offset -= md.v * 0.022f;
        } else {
            float decay = fast_clamp(1.f - 6.f*dt, 0.f, 1.f);
            yaw_vel   *= decay; pitch_vel *= decay;
            yaw_offset   += yaw_vel   * dt;
            pitch_offset += pitch_vel * dt;
        }
        pitch_offset = fast_clamp(pitch_offset, -1.45f, 1.45f);

        // Double-click left: reset view
        if (over_sim && input.isMouseButtonPressed(MOUSE_LEFT)) {
            if (global_t - last_left_press_t < 0.35f) {
                yaw_offset=0.f; pitch_offset=0.f;
                yaw_vel=0.f;    pitch_vel=0.f;
                pan_offset=Vec3(0,0,0);
                zoom_target=1.f;
                auto_rotate=true;
            }
            last_left_press_t = global_t;
        }

        // Right drag: pan XZ
        if (over_sim && input.isMouseButtonDown(MOUSE_RIGHT)) {
            auto_rotate   = false;
            pan_offset.x += md.u * 0.012f;
            pan_offset.z -= md.v * 0.012f;
        }

        // Scroll wheel: smooth zoom
        float wheel = input.getMouseWheelDelta();
        if (fast_abs(wheel) > 0.01f) zoom_target -= wheel * 0.12f;
        zoom_target = fast_clamp(zoom_target, 0.1f, 5.f);
        zoom_mult  += (zoom_target - zoom_mult) * fast_clamp(10.f*dt, 0.f, 1.f);

        if (auto_rotate) orbit_t += dt*0.4f;

        int nw, nh; getConsoleSize(nw,nh);
        if (nw!=W||nh!=H) { W=nw; H=nh; display.Resize(W,H); }

        sparks.update(dt);
        sph.update(dt);
        smoke.update(dt);

        ConsoleCell* buf=display.GetBuffer();
        ConsoleCell  blank(ConsoleGlyph(' '),{10,10,18});
        for (int i=0;i<W*H;++i) buf[i]=blank;

        Camera cam_sparks=arcballCamera(Vec3(0,0.5f,0)+pan_offset,4.27f*zoom_mult,orbit_t+yaw_offset,0.36f+pitch_offset,55.f);
        Camera cam_sph   =arcballCamera(Vec3(0,0.f,0)+pan_offset, 2.4f*zoom_mult, orbit_t*0.6f+yaw_offset,0.45f+pitch_offset,52.f);
        Camera cam_smoke =arcballCamera(Vec3(0,0,0)+pan_offset,   2.08f*zoom_mult,orbit_t+HALF_PI+yaw_offset,0.29f+pitch_offset,55.f);

        sparks.render(buf,W,H,0,          0,PANEL_W,PANEL_H,cam_sparks);
        sph   .render(buf,W,H,PANEL_W,    0,PANEL_W,PANEL_H,cam_sph);
        smoke .render(buf,W,H,PANEL_W*2,  0,PANEL_W,PANEL_H,cam_smoke);
        drawBorder(buf,W,H,0,         0,PANEL_W,PANEL_H,"SPARKS",    {200,120,60});
        drawBorder(buf,W,H,PANEL_W,   0,PANEL_W,PANEL_H,"FLUID MESH",{60,200,255});
        drawBorder(buf,W,H,PANEL_W*2, 0,PANEL_W,PANEL_H,"3D SMOKE",  {160,160,200});

        // Sync ui_buffer dimensions only when they change
        if (W!=last_W||H!=last_H) {
            ui_buffer.resize(H);
            for (int i=0;i<H;++i) ui_buffer[i].resize(W);
            last_W=W; last_H=H;
        }
        for (int i=0;i<H;++i)
            memcpy(ui_buffer[i].data(), buf+i*W, W*sizeof(ConsoleCell));

        config_ui.update(dt,Vec3(0),Vec3(0,0,1),0.f,Vec3(0),true,true);
        config_ui.setMenuOpen(true);
        config_ui.render(ui_buffer,W,H);

        for (int i=0;i<H;++i)
            memcpy(buf+i*W, ui_buffer[i].data(), W*sizeof(ConsoleCell));

        display.Flush(W,H,gfx);
        platform_sleep(0.01);
    }

    restoreConsoleMode();
    return 0;
}