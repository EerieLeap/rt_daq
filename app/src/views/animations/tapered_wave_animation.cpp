#include <cmath>
#include <utility>

#include "utilities/animation_math_helpers.h"

#include "tapered_wave_animation.h"

namespace eerie_leap::views::animations {

using namespace eerie_leap::views::animations::utilities;

TaperedWaveAnimation::TaperedWaveAnimation(std::shared_ptr<Cfb> cfb) :
    cfb_(std::move(cfb)) {}

void TaperedWaveAnimation::Initialize() {
    time_ = 0.0F;
}

void TaperedWaveAnimation::Animate() {
    cfb_->Clear();

    const uint16_t screen_width = cfb_->GetXRes();
    const uint16_t screen_height = cfb_->GetYRes();

    Coordinate prev_point = { .x = 0, .y = 0 };
    bool has_prev = false;

    for(int i = 0; std::cmp_less_equal(i, screen_width); i++) {
        const float normalized_position = static_cast<float>(i) / static_cast<float>(screen_width);

        // Calculate position
        // Map from center with range proportional to screen width
        const float range = static_cast<float>(screen_width) * 0.9F; // 90% of screen width
        const float x = static_cast<float>(screen_width) / 2.0F + (normalized_position - 0.5F) * range;

        // Sine wave for y position, scaled to screen height
        const float amplitude = static_cast<float>(screen_height) * 0.3F; // 30% of screen height
        const float y = static_cast<float>(screen_height) / 2.0F
            + amplitude * sinf(AnimationMathHelpers::PI * normalized_position - time_ * 0.015F);

        // Calculate stroke weight (thickest in the middle)
        const float weight = 1.0F + THICKNESS * normalized_position * (1.0F - normalized_position);

        // Convert to screen coordinates
        const uint16_t screen_x = static_cast<uint16_t>(std::lround(x));
        const uint16_t screen_y = static_cast<uint16_t>(std::lround(y));

        // Bounds check
        if(screen_x < screen_width && screen_y < screen_height) {
            if(has_prev) {
                // Draw line segment with thickness
                DrawThickLine(prev_point, { screen_x, screen_y }, weight);
            }

            prev_point = { screen_x, screen_y };
            has_prev = true;
        } else {
            has_prev = false;
        }
    }

    time_ += 10.0F;
}

void TaperedWaveAnimation::DrawThickLine(Coordinate p1, Coordinate p2, float thickness) {
    // For thin lines, just draw normally
    if(thickness <= 1.5F) {
        cfb_->DrawLine(p1, p2);
        return;
    }

    // For thicker lines, draw multiple parallel lines
    const int half_thickness = static_cast<int>(thickness / 2.0F);

    // Calculate perpendicular direction
    const int dx = p2.x - p1.x;
    const int dy = p2.y - p1.y;
    const float length = sqrtf(static_cast<float>((dx * dx) + (dy * dy)));

    if(length < 0.1F) {
        cfb_->DrawPoint(p1);
        return;
    }

    // Normalized perpendicular vector
    const float perp_x = static_cast<float>(-dy) / length;
    const float perp_y = static_cast<float>(dx) / length;

    // Draw multiple lines to create thickness
    for(int offset = -half_thickness; offset <= half_thickness; offset++) {
        Coordinate start = {
            .x = static_cast<uint16_t>(std::lround(static_cast<float>(p1.x) + static_cast<float>(offset) * perp_x)),
            .y = static_cast<uint16_t>(std::lround(static_cast<float>(p1.y) + static_cast<float>(offset) * perp_y))
        };
        Coordinate end = {
            .x = static_cast<uint16_t>(std::lround(static_cast<float>(p2.x) + static_cast<float>(offset) * perp_x)),
            .y = static_cast<uint16_t>(std::lround(static_cast<float>(p2.y) + static_cast<float>(offset) * perp_y))
        };

        cfb_->DrawLine(start, end);
    }
}

} // namespace eerie_leap::views::animations
