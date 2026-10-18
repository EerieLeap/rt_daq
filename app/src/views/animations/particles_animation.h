#pragma once

#include <memory>
#include <random>

#include "subsys/cfb/cfb.h"

#include "i_animation.h"

namespace eerie_leap::views::animations {

using eerie_leap::subsys::cfb::Cfb;

class ParticlesAnimation : public IAnimation {
private:
    std::shared_ptr<Cfb> cfb_;
    uint32_t frame_;
    std::mt19937 rng_;

    void AnimateSineParticles(uint32_t frame);

public:
    explicit ParticlesAnimation(std::shared_ptr<Cfb> cfb);

    void Initialize() override;
    void Animate() override;
};

} // namespace eerie_leap::views::animations
