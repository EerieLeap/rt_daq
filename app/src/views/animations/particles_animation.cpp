#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "views/animations/utilities/animation_math_helpers.h"

#include "particles_animation.h"

namespace eerie_leap::views::animations {

using namespace eerie_leap::views::animations::utilities;

constexpr uint16_t NUM_DOTS = 80;

struct Dot {
    float seed_x;
    float seed_y;
    float phase_offset;
};

Dot dots[NUM_DOTS];

ParticlesAnimation::ParticlesAnimation(std::shared_ptr<Cfb> cfb) :
    cfb_(std::move(cfb)),
    frame_(0),
    rng_(1234) {}

void ParticlesAnimation::Initialize() {
    std::uniform_real_distribution<float> dist(0.0F, 10.0F);

    for(int i = 0; i < NUM_DOTS; i++) {
        dots[i].seed_x = dist(rng_);
        dots[i].seed_y = dist(rng_);
        dots[i].phase_offset = ((float)i / NUM_DOTS) * AnimationMathHelpers::PI * 2;
    }
}

void ParticlesAnimation::Animate() {
    cfb_->Clear();
    AnimateSineParticles(frame_);
    frame_++;
}

/* Particles following sine curves */
void ParticlesAnimation::AnimateSineParticles(uint32_t frame) {
    float time = frame / 120.0F;

    for(int i = 0; i < NUM_DOTS; i++) {
        /* Horizontal motion */
        float progress = fmodf(time + dots[i].phase_offset / (AnimationMathHelpers::PI * 2), 1.0F);
        const uint16_t x = (uint16_t)(progress * cfb_->GetXRes());

        /* Multiple sine waves at different frequencies */
        float freq1 = 0.1F + dots[i].seed_x * 0.05F;
        float freq2 = 0.15F + dots[i].seed_y * 0.05F;

        float wave1 = sinf(x * freq1 + time * AnimationMathHelpers::PI * 2);
        float wave2 = sinf(x * freq2 - time * AnimationMathHelpers::PI * 2 * 0.5F);

        const uint16_t y = cfb_->GetYRes() / 2 + (uint16_t)((wave1 + wave2) * 7.0F);

        if(x >= 0 && x < cfb_->GetXRes() && y >= 0 && y < cfb_->GetYRes())
            cfb_->DrawPoint({ x, y });
    }
}

} // namespace eerie_leap::views::animations
