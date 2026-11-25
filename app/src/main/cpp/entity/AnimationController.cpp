#include "AnimationController.h"

#include "Skeleton.h"

#include <algorithm>
#include <cmath>

namespace minefade {
namespace {

ModelVec3 Lerp(const ModelVec3& a,const ModelVec3& b,float t) {
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
}

std::uint32_t FindState(const ModelData& model,std::uint8_t condition) {
    for (const ModelAnimationState& state : model.states) {
        if (static_cast<std::uint32_t>(state.condition)==condition) return state.clipIndex;
    }
    return model.states.empty()?0u:model.states.front().clipIndex;
}

float ClipTime(const ModelAnimationClip& clip,float time) {
    if (clip.duration<=0.0f) return 0.0f;
    if (clip.loop) {
        float wrapped=std::fmod(time,clip.duration);
        if (wrapped<0.0f) wrapped+=clip.duration;
        return wrapped;
    }
    return std::clamp(time,0.0f,clip.duration);
}

ModelKeyframe SampleTrack(const ModelTrack& track,float time) {
    if (track.keyframes.empty()) return {};
    if (time<=track.keyframes.front().time) return track.keyframes.front();
    if (time>=track.keyframes.back().time) return track.keyframes.back();
    for (std::size_t i=1;i<track.keyframes.size();++i) {
        const ModelKeyframe& b=track.keyframes[i];
        const ModelKeyframe& a=track.keyframes[i-1u];
        if (time>b.time) continue;
        const float t=(time-a.time)/std::max(b.time-a.time,1e-6f);
        ModelKeyframe result;
        result.time=time;
        result.translation=Lerp(a.translation,b.translation,t);
        result.rotation=Skeleton::Slerp(a.rotation,b.rotation,t);
        result.scale=Lerp(a.scale,b.scale,t);
        return result;
    }
    return track.keyframes.back();
}

void EvaluateClip(const ModelData& model,std::uint32_t clipIndex,float time,
                  std::vector<AnimatedBonePose>& output) {
    if (clipIndex>=model.clips.size()) return;
    const ModelAnimationClip& clip=model.clips[clipIndex];
    const float localTime=ClipTime(clip,time);
    const std::size_t end=std::min<std::size_t>(
        static_cast<std::size_t>(clip.firstTrack)+clip.trackCount,model.tracks.size());
    for (std::size_t i=clip.firstTrack;i<end;++i) {
        const ModelTrack& track=model.tracks[i];
        if (track.boneIndex>=output.size()) continue;
        const ModelKeyframe key=SampleTrack(track,localTime);
        AnimatedBonePose& pose=output[track.boneIndex];
        pose.translation=key.translation;
        pose.rotation=key.rotation;
        pose.scale=key.scale;
    }
}

} // namespace

bool AnimationController::Evaluate(const ModelData& model,const EntityAnimationState& state,
                                   std::vector<AnimatedBonePose>& output) {
    if (model.bones.empty() || model.clips.empty() || model.states.empty()) return false;
    const std::uint32_t currentClip=FindState(model,state.condition);
    const std::uint32_t previousClip=FindState(model,state.previousCondition);
    if (currentClip>=model.clips.size() || previousClip>=model.clips.size()) return false;

    const auto resetPose=[&](std::vector<AnimatedBonePose>& pose) {
        pose.assign(model.bones.size(),AnimatedBonePose{});
    };
    std::vector<AnimatedBonePose> current;
    resetPose(current);
    EvaluateClip(model,currentClip,state.time,current);

    float blendDuration=0.0f;
    for (const ModelAnimationState& definition : model.states) {
        if (definition.clipIndex==currentClip) {
            blendDuration=definition.blendDuration;
            break;
        }
    }
    float blend=1.0f;
    if (previousClip!=currentClip && blendDuration>1e-4f)
        blend=std::clamp(state.transitionSeconds/blendDuration,0.0f,1.0f);

    if (blend<1.0f) {
        std::vector<AnimatedBonePose> previous;
        resetPose(previous);
        EvaluateClip(model,previousClip,state.time,previous);
        for (std::size_t i=0;i<current.size();++i) {
            current[i].translation=Lerp(previous[i].translation,current[i].translation,blend);
            current[i].rotation=Skeleton::Slerp(previous[i].rotation,current[i].rotation,blend);
            current[i].scale=Lerp(previous[i].scale,current[i].scale,blend);
        }
    }
    output=std::move(current);
    return true;
}

} // namespace minefade
