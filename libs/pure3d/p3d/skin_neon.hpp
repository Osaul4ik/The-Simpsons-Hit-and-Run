//=============================================================================
// NEON vertex blending for the software skinning path (tPrimGroupSkinnedPC).
// Same maths as the scalar code: the normal only follows the first bone, a first
// weight of exactly 1 skips the other bones, the fourth weight is what is left.
//=============================================================================
#ifndef P3D_SKIN_NEON_HPP
#define P3D_SKIN_NEON_HPP

#include <arm_neon.h>

// reads exactly three floats: the last position of an array has nothing behind it
static inline float32x4_t p3dSkinLoad3(const float* p)
{
    return vsetq_lane_f32(p[2], vcombine_f32(vld1_f32(p), vdup_n_f32(0.0f)), 2);
}

static inline void p3dSkinStore3(float* out, float32x4_t v)
{
    vst1_f32(out, vget_low_f32(v));
    out[2] = vgetq_lane_f32(v, 2);
}

// x*row0 + y*row1 + z*row2 + row3 of a row vector matrix
static inline float32x4_t p3dSkinTransform(const float* m, float32x2_t xy, float32x2_t z)
{
    float32x4_t r = vmlaq_lane_f32(vld1q_f32(m + 12), vld1q_f32(m), xy, 0);
    r = vmlaq_lane_f32(r, vld1q_f32(m + 4), xy, 1);
    return vmlaq_lane_f32(r, vld1q_f32(m + 8), z, 0);
}

// Vertex: weights[3], indices[4], normal, position. Matrix: float m[4][4] first.
// out: 3 floats per vertex (position), or 6 with normals (position, normal).
template<class Vertex, class Matrix>
static inline void p3dSkinVerticesNeon(const Vertex* verts, int count, const Matrix* const* palette, float* out, bool withNormals)
{
    for(int i = 0; i < count; i++)
    {
        const Vertex& v = verts[i];
        const float* m0 = palette[v.indices[0]]->m[0];
        const float32x4_t p = p3dSkinLoad3(&v.position.x);
        const float32x2_t xy = vget_low_f32(p), z = vget_high_f32(p);

        const float w0 = v.weights[0];
        float32x4_t pos = vmulq_n_f32(p3dSkinTransform(m0, xy, z), w0);
        if(w0 != 1.0f)
        {
            const float w1 = v.weights[1], w2 = v.weights[2];
            pos = vmlaq_n_f32(pos, p3dSkinTransform(palette[v.indices[1]]->m[0], xy, z), w1);
            pos = vmlaq_n_f32(pos, p3dSkinTransform(palette[v.indices[2]]->m[0], xy, z), w2);
            pos = vmlaq_n_f32(pos, p3dSkinTransform(palette[v.indices[3]]->m[0], xy, z), 1.0f - (w2 + w1 + w0));
        }
        p3dSkinStore3(out, pos);
        out += 3;

        if(withNormals)
        {
            const float32x4_t n = p3dSkinLoad3(&v.normal.x);
            float32x4_t r = vmulq_lane_f32(vld1q_f32(m0), vget_low_f32(n), 0);
            r = vmlaq_lane_f32(r, vld1q_f32(m0 + 4), vget_low_f32(n), 1);
            r = vmlaq_lane_f32(r, vld1q_f32(m0 + 8), vget_high_f32(n), 0);
            p3dSkinStore3(out, r);
            out += 3;
        }
    }
}

#endif
