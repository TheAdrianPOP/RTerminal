#pragma once

#include "engine_config.h"
#include "math_utils.h"
#include "matrix.h"
#include "console.h"

template<int W, int H, int D>
struct FluidGrid {
    static constexpr int CELLS = W * H * D;

    float density [CELLS];
    float density0[CELLS];
    float u [CELLS];
    float v [CELLS];
    float w [CELLS];
    float u0[CELLS];
    float v0[CELLS];
    float w0[CELLS];

    FluidGrid() { clear(); }

    static constexpr int idx(int x, int y, int z) {
        return z * W * H + y * W + x;
    }
    static int cx(int x) { return x<0?0:(x>=W?W-1:x); }
    static int cy(int y) { return y<0?0:(y>=H?H-1:y); }
    static int cz(int z) { return z<0?0:(z>=D?D-1:z); }

    void addDensity(int x, int y, int z, float amount) {
        if (x<0||x>=W||y<0||y>=H||z<0||z>=D) return;
        density[idx(x,y,z)] = fast_clamp(density[idx(x,y,z)] + amount, 0.f, 1.f);
    }

    void addVelocity(int x, int y, int z, float du, float dv, float dw_) {
        if (x<0||x>=W||y<0||y>=H||z<0||z>=D) return;
        int i=idx(x,y,z);
        u[i]+=du; v[i]+=dv; w[i]+=dw_;
    }

    void clear() {
        for (int i=0;i<CELLS;++i)
            density[i]=density0[i]=u[i]=v[i]=w[i]=u0[i]=v0[i]=w0[i]=0.f;
    }

    float sampleTrilinear(const float* field, float x, float y, float z) const {
        int x0=cx((int)x), y0=cy((int)y), z0=cz((int)z);
        int x1=cx(x0+1),   y1=cy(y0+1),   z1=cz(z0+1);
        float sx=x-(float)x0, sy=y-(float)y0, sz=z-(float)z0;
        float c00=fast_lerp(field[idx(x0,y0,z0)],field[idx(x1,y0,z0)],sx);
        float c10=fast_lerp(field[idx(x0,y1,z0)],field[idx(x1,y1,z0)],sx);
        float c01=fast_lerp(field[idx(x0,y0,z1)],field[idx(x1,y0,z1)],sx);
        float c11=fast_lerp(field[idx(x0,y1,z1)],field[idx(x1,y1,z1)],sx);
        return fast_lerp(fast_lerp(c00,c10,sy), fast_lerp(c01,c11,sy), sz);
    }
};

template<int W, int H, int D>
static void diffuse3D(float* x, const float* x0, float diff, float dt, int iter) {
    const float a   = dt * diff * (float)(W*H*D);
    const float inv = 1.f / (1.f + kFluidGrid3DNeighbors * a);
    for (int k=0;k<iter;++k)
        for (int kk=1;kk<D-1;++kk)
        for (int j=1;j<H-1;++j)
        for (int i=1;i<W-1;++i) {
            int c=kk*(W*H)+j*W+i;
            x[c]=(x0[c]+a*(
                x[c-1]+x[c+1]+
                x[c-W]+x[c+W]+
                x[c-W*H]+x[c+W*H]))*inv;
        }
}

template<int W, int H, int D>
static void advect3D(float* d, const float* d0,
                     const float* vu, const float* vv, const float* vw,
                     float dt)
{
    const float dt0u=dt*(float)W, dt0v=dt*(float)H, dt0w=dt*(float)D;
    for (int kk=1;kk<D-1;++kk)
    for (int j=1;j<H-1;++j)
    for (int i=1;i<W-1;++i) {
        int c=kk*(W*H)+j*W+i;
        float x=fast_clamp((float)i-dt0u*vu[c], kFluidAdvectCellLo, (float)W-kFluidAdvectCellHi);
        float y=fast_clamp((float)j-dt0v*vv[c], kFluidAdvectCellLo, (float)H-kFluidAdvectCellHi);
        float z=fast_clamp((float)kk-dt0w*vw[c],kFluidAdvectCellLo, (float)D-kFluidAdvectCellHi);
        int x0=(int)x, y0=(int)y, z0=(int)z;
        int x1=x0+1<W?x0+1:W-1;
        int y1=y0+1<H?y0+1:H-1;
        int z1=z0+1<D?z0+1:D-1;
        float sx=x-(float)x0, sy=y-(float)y0, sz=z-(float)z0;
        float c00=fast_lerp(d0[z0*(W*H)+y0*W+x0],d0[z0*(W*H)+y0*W+x1],sx);
        float c10=fast_lerp(d0[z0*(W*H)+y1*W+x0],d0[z0*(W*H)+y1*W+x1],sx);
        float c01=fast_lerp(d0[z1*(W*H)+y0*W+x0],d0[z1*(W*H)+y0*W+x1],sx);
        float c11=fast_lerp(d0[z1*(W*H)+y1*W+x0],d0[z1*(W*H)+y1*W+x1],sx);
        d[c]=fast_lerp(fast_lerp(c00,c10,sy),fast_lerp(c01,c11,sy),sz);
    }
}

template<int W, int H, int D>
inline void stepFluid3D(FluidGrid<W,H,D>& g,
                         float diff, float visc, float decay, float dt,
                         int diffuse_iter)
{
    const int C=FluidGrid<W,H,D>::CELLS;

    for(int i=0;i<C;++i){g.u0[i]=g.u[i];g.v0[i]=g.v[i];g.w0[i]=g.w[i];}
    diffuse3D<W,H,D>(g.u,g.u0,visc,dt,diffuse_iter);
    diffuse3D<W,H,D>(g.v,g.v0,visc,dt,diffuse_iter);
    diffuse3D<W,H,D>(g.w,g.w0,visc,dt,diffuse_iter);
    for(int i=0;i<C;++i){g.u0[i]=g.u[i];g.v0[i]=g.v[i];g.w0[i]=g.w[i];}
    advect3D<W,H,D>(g.u,g.u0,g.u0,g.v0,g.w0,dt);
    advect3D<W,H,D>(g.v,g.v0,g.u0,g.v0,g.w0,dt);
    advect3D<W,H,D>(g.w,g.w0,g.u0,g.v0,g.w0,dt);

    for(int i=0;i<C;++i) g.density0[i]=g.density[i];
    diffuse3D<W,H,D>(g.density,g.density0,diff,dt,diffuse_iter);
    for(int i=0;i<C;++i) g.density0[i]=g.density[i];
    advect3D<W,H,D>(g.density,g.density0,g.u,g.v,g.w,dt);
    const float dk=1.f-fast_clamp(decay*dt,0.f,1.f);
    for(int i=0;i<C;++i) g.density[i]=fast_clamp(g.density[i]*dk,0.f,1.f);
}

template<int W, int H, int D>
inline void renderFluid3D(const FluidGrid<W,H,D>& g,
                           ConsoleCell* buffer,
                           int buf_w, int buf_h,
                           Vec3 grid_origin,
                           float cell_size,
                           Vec3 cam_pos,
                           Vec3 cam_fwd,
                           Vec3 cam_right,
                           Vec3 cam_up,
                           float fov_x,
                           RGBColor color,
                           float threshold = kFluidGridRenderThreshold)
{
    if (!buffer) return;

    static const char* ramp[] = {
        " ", "\xe2\x96\x91", "\xe2\x96\x92", "\xe2\x96\x93", "\xe2\x96\x88"
    };

    const float half_w = (float)buf_w * 0.5f;
    const float half_h = (float)buf_h * 0.5f;
    const float fov_y  = fov_x * ((float)buf_h / (float)buf_w);
    const float tan_x  = fast_tan(fov_x * 0.5f);
    const float tan_y  = fast_tan(fov_y * 0.5f);

    PodVector<float> depth_buf;
    depth_buf.resize(buf_w * buf_h, kFluidDepthSentinel);
    for (int kk=0; kk<D; ++kk)
    for (int j=0;  j<H;  ++j)
    for (int i=0;  i<W;  ++i) {
        float d = g.density[FluidGrid<W,H,D>::idx(i,j,kk)];
        if (d < threshold) continue;

        Vec3 world(
            grid_origin.x + ((float)i + 0.5f) * cell_size,
            grid_origin.y + ((float)j + 0.5f) * cell_size,
            grid_origin.z + ((float)kk + 0.5f) * cell_size
        );

        Vec3  rel   = world - cam_pos;
        float depth = rel.dot(cam_fwd);
        if (depth <= kParticleNearClip) continue;

        float rx = rel.dot(cam_right) / depth;
        float ry = rel.dot(cam_up)    / depth;
        if (fast_abs(rx) > tan_x || fast_abs(ry) > tan_y) continue;

        int sx = (int)(half_w + rx / tan_x * half_w);
        int sy = (int)(half_h - ry / tan_y * half_h);
        if (sx<0||sx>=buf_w||sy<0||sy>=buf_h) continue;

        int pixel = sy * buf_w + sx;
        if (depth >= depth_buf[pixel]) continue;
        depth_buf[pixel] = depth;

        int ri = fast_clamp((int)(d * kFluidRampIndexScale), 0, 4);
        RGBColor col = {
            (int16_t)(color.r * d),
            (int16_t)(color.g * d),
            (int16_t)(color.b * d)
        };
        buffer[pixel] = ConsoleCell(ConsoleGlyph(ramp[ri]), col, false, false);
    }
}