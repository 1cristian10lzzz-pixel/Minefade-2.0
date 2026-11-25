#include "EntityVisualSystem.h"

#include "AnimationController.h"
#include "ModelLoader.h"
#include "ModelValidator.h"
#include "Skeleton.h"

#include "../game/Player.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace minefade {
namespace {

ModelVec3 Add(const ModelVec3& a,const ModelVec3& b) {
    return {a.x+b.x,a.y+b.y,a.z+b.z};
}
ModelVec3 Subtract(const ModelVec3& a,const ModelVec3& b) {
    return {a.x-b.x,a.y-b.y,a.z-b.z};
}
ModelVec3 Multiply(const ModelVec3& a,float s) {
    return {a.x*s,a.y*s,a.z*s};
}
ModelVec3 Cross(const ModelVec3& a,const ModelVec3& b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
ModelVec3 Normalize(const ModelVec3& value) {
    const float length=std::sqrt(value.x*value.x+value.y*value.y+value.z*value.z);
    if(length<1e-8f || !std::isfinite(length)) return {0.0f,1.0f,0.0f};
    return Multiply(value,1.0f/length);
}
ModelVec3 Rotate(const ModelQuaternion& q,const ModelVec3& value) {
    const ModelVec3 u{q.x,q.y,q.z};
    const float dot=u.x*value.x+u.y*value.y+u.z*value.z;
    const float uu=u.x*u.x+u.y*u.y+u.z*u.z;
    const ModelVec3 cross=Cross(u,value);
    return Add(Add(Multiply(u,2.0f*dot),
                   Multiply(value,q.w*q.w-uu)),
               Multiply(cross,2.0f*q.w));
}
ModelVec3 CubePoint(const ModelCube& cube,const ModelVec3& point) {
    const ModelQuaternion rotation=Skeleton::FromEulerDegrees(cube.rotationDegrees);
    const ModelVec3 relative=Subtract(point,cube.pivot);
    return Add(cube.pivot,Rotate(rotation,relative));
}
ModelVec3 WorldPoint(const ModelMatrix& boneMatrix,const ModelVec3& local,
                     const Entity& entity) {
    const ModelVec3 posed=Skeleton::TransformPoint(boneMatrix,local);
    const float cosine=std::cos(entity.yaw);
    const float sine=std::sin(entity.yaw);
    return {
        entity.position.x+posed.x*cosine+posed.z*sine,
        entity.position.y+posed.y,
        entity.position.z-posed.x*sine+posed.z*cosine
    };
}
std::array<ModelVec3,4> FaceCorners(const ModelCube& cube,std::size_t face) {
    const float x0=cube.origin.x, x1=cube.origin.x+cube.size.x;
    const float y0=cube.origin.y, y1=cube.origin.y+cube.size.y;
    const float z0=cube.origin.z, z1=cube.origin.z+cube.size.z;
    switch(face) {
        case 0: return {{{x1,y0,z0},{x1,y1,z0},{x1,y1,z1},{x1,y0,z1}}}; // +X
        case 1: return {{{x0,y0,z1},{x0,y1,z1},{x0,y1,z0},{x0,y0,z0}}}; // -X
        case 2: return {{{x0,y1,z1},{x1,y1,z1},{x1,y1,z0},{x0,y1,z0}}}; // +Y
        case 3: return {{{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}}}; // -Y
        case 4: return {{{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}}}; // +Z
        default:return {{{x1,y0,z0},{x0,y0,z0},{x0,y1,z0},{x1,y1,z0}}}; // -Z
    }
}

void AppendTriangle(std::vector<WorldVertex>& output,
                    const std::array<ModelVec3,4>& world,
                    const std::array<std::array<float,2>,4>& uv,
                    const std::array<float,3>& colour,
                    std::size_t a,std::size_t b,std::size_t c) {
    const ModelVec3 edgeA=Subtract(world[b],world[a]);
    const ModelVec3 edgeB=Subtract(world[c],world[a]);
    const ModelVec3 normal=Normalize(Cross(edgeA,edgeB));
    const std::array<std::size_t,3> order{{a,b,c}};
    for(std::size_t index:order) {
        WorldVertex vertex{};
        vertex.x=world[index].x;
        vertex.y=world[index].y;
        vertex.z=world[index].z;
        vertex.nx=normal.x;
        vertex.ny=normal.y;
        vertex.nz=normal.z;
        vertex.r=colour[0];
        vertex.g=colour[1];
        vertex.b=colour[2];
        vertex.u=uv[index][0];
        vertex.v=uv[index][1];
        vertex.atlasTile=0.0f;
        output.push_back(vertex);
    }
}

bool IsVisible(const Entity& entity,const ModelData& model,const CameraFrustum* frustum) {
    if(frustum==nullptr) return true;
    const float radius=std::max(
        std::abs(model.boundsMax.x-model.boundsMin.x),
        std::abs(model.boundsMax.z-model.boundsMin.z))*0.75f;
    const float height=std::max(0.1f,model.boundsMax.y-model.boundsMin.y);
    return frustum->IntersectsAabb(
        entity.position.x-radius,entity.position.y+model.boundsMin.y,
        entity.position.z-radius,
        entity.position.x+radius,entity.position.y+model.boundsMax.y,
        entity.position.z+radius) || height<=0.0f;
}

} // namespace

bool EntityVisualSystem::LoadSheepModel(const std::uint8_t* bytes,std::size_t size,
                                        std::string& error) {
    Clear();
    ModelData candidate;
    if(!ModelLoader::Load(bytes,size,candidate,error)) {
        sheepDiagnostic_=error;
        return false;
    }
    if(!ModelValidator::Validate(candidate,error)) {
        sheepDiagnostic_=error;
        return false;
    }
    std::string registrationError;
    if(!registry_.Register(MakeSheepEntityDefinition(),registrationError)) {
        error=registrationError;
        sheepDiagnostic_=error;
        return false;
    }
    sheepModel_=std::move(candidate);
    sheepValidated_=true;
    sheepDiagnostic_="MAES sheep model v1 loaded and validated";
    legacyAdapter_.SetMode(LegacyModelMode::Auto);
    error.clear();
    return true;
}

void EntityVisualSystem::Clear() {
    sheepModel_={};
    registry_.Clear();
    sheepValidated_=false;
    sheepDiagnostic_.clear();
}

void EntityVisualSystem::AppendMesh(const std::vector<Entity>& entities,
                                    std::vector<WorldVertex>& output,
                                    const CameraFrustum* frustum,
                                    bool textureAvailable) const {
    if(!sheepValidated_ || !legacyAdapter_.UseMaes(sheepValidated_)) return;
    for(const Entity& entity:entities) {
        if(entity.kind!=0 || !IsVisible(entity,sheepModel_,frustum)) continue;

        EntityAnimationState animation;
        animation.time=entity.animationClock;
        animation.condition=entity.visualState;
        animation.previousCondition=entity.previousVisualState;
        animation.transitionSeconds=entity.visualTransitionSeconds;

        std::vector<AnimatedBonePose> pose;
        if(!AnimationController::Evaluate(sheepModel_,animation,pose)) continue;
        std::vector<ModelVec3> translations;
        std::vector<ModelQuaternion> rotations;
        std::vector<ModelVec3> scales;
        translations.reserve(pose.size());
        rotations.reserve(pose.size());
        scales.reserve(pose.size());
        for(const AnimatedBonePose& bone:pose) {
            translations.push_back(bone.translation);
            rotations.push_back(bone.rotation);
            scales.push_back(bone.scale);
        }
        std::vector<ModelMatrix> boneMatrices;
        if(!Skeleton::BuildMatrices(sheepModel_,translations,rotations,scales,boneMatrices))
            continue;

        for(const ModelCube& cube:sheepModel_.cubes) {
            if(cube.boneIndex>=boneMatrices.size() ||
               cube.materialIndex>=sheepModel_.materials.size()) continue;
            const ModelMaterial& material=sheepModel_.materials[cube.materialIndex];
            const ModelMatrix& boneMatrix=boneMatrices[cube.boneIndex];
            for(std::size_t face=0;face<6u;++face) {
                const auto localCorners=FaceCorners(cube,face);
                std::array<ModelVec3,4> worldCorners{};
                for(std::size_t corner=0;corner<4u;++corner) {
                    const ModelVec3 cubeRotated=CubePoint(cube,localCorners[corner]);
                    worldCorners[corner]=WorldPoint(boneMatrix,cubeRotated,entity);
                }
                const auto& rect=cube.faceUv[face];
                const std::array<std::array<float,2>,4> uv{{
                    {{rect[0],rect[1]}},{{rect[2],rect[1]}},
                    {{rect[2],rect[3]}},{{rect[0],rect[3]}}
                }};
                const std::size_t offset=face*4u;
                // A valid GL texture uses the model's full UVs. If upload fails,
                // preserve a representative face colour so the entity is still visible.
                const std::array<float,3> color = textureAvailable
                    ? std::array<float,3>{{material.baseColor[0],material.baseColor[1],material.baseColor[2]}}
                    : std::array<float,3>{{
                        (static_cast<float>(cube.faceColors[offset])/255.0f)*material.baseColor[0],
                        (static_cast<float>(cube.faceColors[offset+1u])/255.0f)*material.baseColor[1],
                        (static_cast<float>(cube.faceColors[offset+2u])/255.0f)*material.baseColor[2]
                    }};
                AppendTriangle(output,worldCorners,uv,color,0,1,2);
                AppendTriangle(output,worldCorners,uv,color,0,2,3);
            }
        }
    }
}

} // namespace minefade
