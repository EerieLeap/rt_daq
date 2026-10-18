#include "utilities/animation_math_helpers.h"

#include "wave_lines_animation.h"

#include <utility>

namespace eerie_leap::views::animations {

using namespace eerie_leap::views::animations::utilities;
using namespace eerie_leap::subsys::cfb;

WaveLinesAnimation::WaveLinesAnimation(std::shared_ptr<Cfb> cfb) :
    cfb_(std::move(cfb)),
{}

void WaveLinesAnimation::Initialize() {
    frame_ = 0;
}

void WaveLinesAnimation::Animate() {
    cfb_->Clear();

    // Calculate normalized time (0 to 1 over 100 frames, looping)
    const float time = fmodf(frame_ / 100.0F, 1.0F);

    for(int line = 0; line < LINES_COUNT; line++) {
        const float phase_offset = line * 0.15F;
        DrawWaveLine(time + phase_offset, line);
    }

    frame_ += 3;
}

void WaveLinesAnimation::DrawWaveLine(float time, int line_index) {
    const uint16_t screen_width = cfb_->GetXRes();
    const uint16_t screen_height = cfb_->GetYRes();

    Coordinate prev_point = { 0, 0 };
    bool has_prev = false;

    // Calculate y position for this line
    const float y_center = (line_index + 1.0F) * screen_height / (LINES_COUNT + 1.0F);

    // Calculate maximum amplitude so lines don't overlap
    const float max_amplitude = screen_height * 1.2F / (2.0F * (LINES_COUNT + 1.0F));

    for(int i = 0; std::cmp_less(i, screen_width); i++) {
        const float normalized_position = static_cast<float>(i) / static_cast<float>(screen_width - 1);

        // Base position - horizontal line at calculated center
        const float x = normalized_position * screen_width;
        float y = y_center;

        // Calculate distance from center for radial effect
        const float center_x = screen_width / 2.0F;
        const float center_y = screen_height / 2.0F;
        const float dist = sqrtf((x - center_x) * (x - center_x) + (y - center_y) * (y - center_y));

        // Amplitude modulation based on time and distance
        const float cos_val = cosf(AnimationMathHelpers::PI * 2.0F * time - 0.02F * dist) * 1.1F;
        float amplitude = (cos_val + 1.0F) / 2.0F; // Map from [-1,1] to [0,1]

        // Apply distance-based exponential decay
        amplitude *= expf(-0.004F * dist);

        // Wave displacement with easing - scale to use appropriate amplitude
        const float wave_phase = AnimationMathHelpers::PI * 2.0F * WAVE_COUNT * normalized_position;
        // float sq_amp = amplitude * amplitude;

        // x += AnimationMathHelpers::EaseWithGamma(sq_amp, 3.0f) * (screen_width * 0.1f) * sinf(wave_phase);
        y += AnimationMathHelpers::EaseWithGamma(1.0F - (1.0F - amplitude) * (1.0F - amplitude), 3.0F) * max_amplitude
            * cosf(wave_phase);

        // Convert to screen coordinates
        const uint16_t screen_x = static_cast<uint16_t>(x + 0.5F);
        const uint16_t screen_y = static_cast<uint16_t>(y + 0.5F);

        // Bounds check
        if(screen_x < screen_width && screen_y < screen_height) {
            if(has_prev) {
                // Draw line segment from previous point
                cfb_->DrawLine(prev_point, { screen_x, screen_y });
            } else {
                // First point, just draw it
                cfb_->DrawPoint({ screen_x, screen_y });
            }

            prev_point = { screen_x, screen_y };
            has_prev = true;
        } else {
            has_prev = false;
        }
    }
}

} // namespace eerie_leap::views::animations
