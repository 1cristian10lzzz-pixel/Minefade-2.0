#include "Skeleton.h"

#include <algorithm>
#include <cmath>

namespace minefade {
namespace {

ModelMatrix Identity() {
    return {{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
}

ModelMatrix MultiplyMatrix(const ModelMatrix& a, const ModelMatrix& b) {
    ModelMatrix result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (int k = 0; k < 4; ++k)
                value += a[static_cast<std::size_t>(k * 4 + row)] *
                         b[static_cast<std::size_t>(column * 4 + k)];
            result[static_cast<std::size_t>(column * 4 + row)] = value;
        }
    }
    return result;
}

ModelMatrix Translation(float x, float y, float z) {
    ModelMatrix result = Identity();
    result[12] = x; result[13] = y; result[14] = z;
    return result;
}

ModelMatrix Scale(float x, float y, float z) {
    ModelMatrix result = Identity();
    result[0] = x; result[5] = y; result[10] = z;
    return result;
}

ModelMatrix Rotation(const ModelQuaternion& q) {
    const float xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z;
    const float xy=q.x*q.y, xz=q.x*q.z, yz=q.y*q.z;
    const float wx=q.w*q.x, wy=q.w*q.y, wz=q.w*q.z;
    ModelMatrix result = Identity();
    result[0]=1.0f-2.0f*(yy+zz);
    result[1]=2.0f*(xy+wz);
    result[2]=2.0f*(xz-wy);
    result[4]=2.0f*(xy-wz);
    result[5]=1.0f-2.0f*(xx+zz);
    result[6]=2.0f*(yz+wx);
    result[8]=2.0f*(xz+wy);
    result[9]=2.0f*(yz-wx);
    result[10]=1.0f-2.0f*(xx+yy);
    return result;
}

ModelQuaternion Normalize(ModelQuaternion q) {
    const float length=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if (!std::isfinite(length) || length < 1e-8f) return {};
    q.x/=length; q.y/=length; q.z/=length; q.w/=length;
    return q;
}

} // namespace

ModelQuaternion Skeleton::Multiply(const ModelQuaternion& a,const ModelQuaternion& b) {
    return Normalize({
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
    });
}

ModelQuaternion Skeleton::FromEulerDegrees(const ModelVec3& degrees) {
    constexpr float kHalfRadians=0.008726646259971648f;
    const float x=degrees.x*kHalfRadians;
    const float y=degrees.y*kHalfRadians;
    const float z=degrees.z*kHalfRadians;
    const float sx=std::sin(x),cx=std::cos(x);
    const float sy=std::sin(y),cy=std::cos(y);
    const float sz=std::sin(z),cz=std::cos(z);
    // Match mfmodelc.py: q = qz * qy * qx.
    return Normalize({
        sx*cy*cz-cx*sy*sz,
        cx*sy*cz+sx*cy*sz,
        cx*cy*sz-sx*sy*cz,
        cx*cy*cz+sx*sy*sz
    });
}

ModelQuaternion Skeleton::Slerp(const ModelQuaternion& from,const ModelQuaternion& to,float amount) {
    ModelQuaternion a=Normalize(from);
    ModelQuaternion b=Normalize(to);
    float cosine=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if (cosine<0.0f) {
        cosine=-cosine;
        b={-b.x,-b.y,-b.z,-b.w};
    }
    const float t=std::clamp(amount,0.0f,1.0f);
    if (cosine>0.9995f) {
        return Normalize({
            a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,
            a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t
        });
    }
    cosine=std::clamp(cosine,-1.0f,1.0f);
    const float angle=std::acos(cosine);
    const float sine=std::sin(angle);
    if (std::abs(sine)<1e-6f) return a;
    const float wa=std::sin((1.0f-t)*angle)/sine;
    const float wb=std::sin(t*angle)/sine;
    return Normalize({
        a.x*wa+b.x*wb,a.y*wa+b.y*wb,
        a.z*wa+b.z*wb,a.w*wa+b.w*wb
    });
}

bool Skeleton::BuildMatrices(const ModelData& model,
                             const std::vector<ModelVec3>& translations,
                             const std::vector<ModelQuaternion>& rotations,
                             const std::vector<ModelVec3>& scales,
                             std::vector<ModelMatrix>& output) {
    const std::size_t count=model.bones.size();
    if (count==0u || translations.size()!=count || rotations.size()!=count ||
        scales.size()!=count) return false;
    output.assign(count,Identity());
    for (std::size_t i=0;i<count;++i) {
        const ModelBone& bone=model.bones[i];
        ModelVec3 pivot=bone.pivot;
        if (bone.parentIndex!=0xFFFFFFFFu) {
            const ModelVec3& parentPivot=model.bones[bone.parentIndex].pivot;
            pivot={pivot.x-parentPivot.x,pivot.y-parentPivot.y,pivot.z-parentPivot.z};
        }
        const ModelQuaternion bind=FromEulerDegrees(bone.bindRotationDegrees);
        const ModelQuaternion rotation=Multiply(bind,rotations[i]);
        ModelMatrix local=Translation(pivot.x+translations[i].x,
                                      pivot.y+translations[i].y,
                                      pivot.z+translations[i].z);
        local=MultiplyMatrix(local,Rotation(rotation));
        local=MultiplyMatrix(local,Scale(scales[i].x,scales[i].y,scales[i].z));
        local=MultiplyMatrix(local,Translation(-pivot.x,-pivot.y,-pivot.z));
        if (bone.parentIndex!=0xFFFFFFFFu) {
            if (bone.parentIndex>=i) return false;
            output[i]=MultiplyMatrix(output[bone.parentIndex],local);
        } else {
            output[i]=local;
        }
    }
    return true;
}

ModelVec3 Skeleton::TransformPoint(const ModelMatrix& matrix,const ModelVec3& point) {
    return {
        matrix[0]*point.x+matrix[4]*point.y+matrix[8]*point.z+matrix[12],
        matrix[1]*point.x+matrix[5]*point.y+matrix[9]*point.z+matrix[13],
        matrix[2]*point.x+matrix[6]*point.y+matrix[10]*point.z+matrix[14]
    };
}

} // namespace minefade
