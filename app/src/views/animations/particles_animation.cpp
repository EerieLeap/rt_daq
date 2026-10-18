#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <utility>

#include "views/animations/utilities/animation_math_helpers.h"

#include "particles_animation.h"

namespace eerie_leap::views::animations {

using namespace eerie_leap::views::animations::utilities;

constexpr uint16_t NUM_DOTS = 80;

namespace {

struct Dot {
    float seed_x;
    float seed_y;
    float phase_offset;
};

} // namespace

static Dot dots[NUM_DOTS];

ParticlesAnimation::ParticlesAnimation(std::shared_ptr<Cfb> cfb) :
    cfb_(std::move(cfb)),
    // NOLINTNEXTLINE(bugprone-random-generator-seed): cosmetic animation; the fixed seed gives the same particles on every start
    rng_(1234) {}

void ParticlesAnimation::Initialize() {
    std::uniform_real_distribution<float> dist(0.0F, 10.0F);

    for(int i = 0; std::cmp_less(i, NUM_DOTS); i++) {
        dots[i].seed_x = dist(rng_);
        dots[i].seed_y = dist(rng_);
        dots[i].phase_offset = (static_cast<float>(i) / static_cast<float>(NUM_DOTS)) * AnimationMathHelpers::PI * 2;
    }
}

void ParticlesAnimation::Animate() {
    cfb_->Clear();
    AnimateSineParticles(frame_);
    frame_++;
}

/* Particles following sine curves */
void ParticlesAnimation::AnimateSineParticles(uint32_t frame) {
    const float time = static_cast<float>(frame) / 120.0F;

    for(const auto& dot : dots) {
        /* Horizontal motion */
        const float progress = fmodf(time + dot.phase_offset / (AnimationMathHelpers::PI * 2), 1.0F);
        const uint16_t x = static_cast<uint16_t>(progress * static_cast<float>(cfb_->GetXRes()));

        /* Multiple sine waves at different frequencies */
        const float freq1 = 0.1F + dot.seed_x * 0.05F;
        const float freq2 = 0.15F + dot.seed_y * 0.05F;

        const float wave1 = sinf(static_cast<float>(x) * freq1 + time * AnimationMathHelpers::PI * 2);
        const float wave2 = sinf(static_cast<float>(x) * freq2 - time * AnimationMathHelpers::PI * 2 * 0.5F);

        const uint16_t y = cfb_->GetYRes() / 2 + static_cast<uint16_t>((wave1 + wave2) * 7.0F);

        if(x >= 0 && x < cfb_->GetXRes() && y >= 0 && y < cfb_->GetYRes())
            cfb_->DrawPoint({ x, y });
    }
}

} // namespace eerie_leap::views::animations
