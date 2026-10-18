#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "dot_path_animation.h"
#include "views/animations/utilities/animation_math_helpers.h"

namespace eerie_leap::views::animations {

using namespace eerie_leap::views::animations::utilities;

DotPathAnimation::DotPathAnimation(std::shared_ptr<Cfb> cfb) :
    cfb_(std::move(cfb)),
    time_(0.0F),
    rng_(1234) {}

void DotPathAnimation::Initialize() {
    const uint16_t screen_width = cfb_->GetXRes();
    const uint16_t screen_height = cfb_->GetYRes();
    const float empty_margin = std::min(screen_width, screen_height) * 0.1F;

    // Generate initial position
    const float x = GenerateRandomPosition(empty_margin, screen_width - empty_margin);
    const float y = GenerateRandomPosition(empty_margin, screen_height - empty_margin);

    positions_.push_back({ x, y });
    sizes_.push_back(0.0F); // Start with size 0

    // Generate path
    GeneratePath(x, y, screen_width, screen_height, empty_margin);
}

void DotPathAnimation::Animate() {
    cfb_->Clear();

    const uint16_t screen_width = cfb_->GetXRes();
    const uint16_t screen_height = cfb_->GetYRes();

    // Draw dots along the path
    for(int i = 0; i < DOT_COUNT; ++i) {
        const float normalized_position = (i + time_) / DOT_COUNT;

        float x, y, size;
        GetPositionAndSize(normalized_position, x, y, size);

        const uint16_t screen_x = static_cast<uint16_t>(x + 0.5F);
        const uint16_t screen_y = static_cast<uint16_t>(y + 0.5F);

        if(x < screen_width && y < screen_height && size > 0.1F)
            DrawDot(screen_x, screen_y, size);
    }

    time_ += 0.02F;
    if(time_ >= 1.0F)
        time_ -= 1.0F;
}

void DotPathAnimation::GetPositionAndSize(float p, float& x, float& y, float& size) const {
    // Calculate interpolation indices
    const float float_index = p * NUMBER_OF_JUMPS * 0.9999F;
    const int i1 = static_cast<int>(float_index);
    const int i2 = i1 + 1;

    // Apply easing for smoother motion
    float lerp_param = float_index - i1;
    lerp_param = AnimationMathHelpers::EaseWithGamma(lerp_param, 4.0F);

    // Interpolate position
    const PVector& pos1 = positions_[i1];
    const PVector& pos2 = positions_[i2];
    x = pos1.x + (pos2.x - pos1.x) * lerp_param;
    y = pos1.y + (pos2.y - pos1.y) * lerp_param;

    // Interpolate size
    const float size1 = sizes_[i1];
    const float size2 = sizes_[i2];
    size = size1 + (size2 - size1) * lerp_param;
}

void DotPathAnimation::DrawDot(uint16_t x, uint16_t y, float size) {
    if(size <= 1.0F) {
        cfb_->DrawPoint({ x, y });
    } else {
        const int radius = static_cast<int>((size / 2.0F) + 0.5F);
        cfb_->DrawCircle({ x, y }, radius);
    }
}

void DotPathAnimation::GeneratePath(
    float x,
    float y,
    uint16_t screen_width,
    uint16_t screen_height,
    float empty_margin
) {
    std::uniform_real_distribution<float> jump_dist(10.0F, 40.0F);
    std::uniform_int_distribution<int> direction_dist(0, 7);
    std::uniform_real_distribution<float> size_dist(0.5F, 2.5F);

    for(int i = 0; i < NUMBER_OF_JUMPS; ++i) {
        const float jump_length = jump_dist(rng_);
        const int direction = direction_dist(rng_);

        ApplyJump(x, y, jump_length, direction);
        ClampToBounds(x, y, jump_length, screen_width, screen_height, empty_margin);

        positions_.push_back({ x, y });

        // Last dot has size 0, others are random
        const float size = (i == NUMBER_OF_JUMPS - 1) ? 0.0F : size_dist(rng_);
        sizes_.push_back(size);
    }
}

void DotPathAnimation::ApplyJump(float& x, float& y, float jump_length, int direction) {
    constexpr float DIAGONAL = 0.707106781F; // 1 / sqrt(2)

    switch(direction) {
        case 0:
            x += jump_length;
            break;
        case 1:
            x -= jump_length;
            break;
        case 2:
            y += jump_length;
            break;
        case 3:
            y -= jump_length;
            break;
        case 4:
            x += jump_length * DIAGONAL;
            y += jump_length * DIAGONAL;
            break;
        case 5:
            x -= jump_length * DIAGONAL;
            y += jump_length * DIAGONAL;
            break;
        case 6:
            x += jump_length * DIAGONAL;
            y -= jump_length * DIAGONAL;
            break;
        case 7:
            x -= jump_length * DIAGONAL;
            y -= jump_length * DIAGONAL;
            break;
    }
}

void DotPathAnimation::ClampToBounds(
    float& x,
    float& y,
    float jump_length,
    uint16_t screen_width,
    uint16_t screen_height,
    float empty_margin
) {
    if(x > screen_width - empty_margin)
        x -= 2 * jump_length;
    else if(x < empty_margin)
        x += 2 * jump_length;

    if(y > screen_height - empty_margin)
        y -= 2 * jump_length;
    else if(y < empty_margin)
        y += 2 * jump_length;
}

float DotPathAnimation::GenerateRandomPosition(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);

    return dist(rng_);
}

} // namespace eerie_leap::views::animations
