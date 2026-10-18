#pragma once

#include <memory>
#include <random>
#include <vector>

#include "subsys/cfb/cfb.h"

#include "i_animation.h"

namespace eerie_leap::views::animations {

using eerie_leap::subsys::cfb::Cfb;

struct PVector {
    float x;
    float y;
};

class DotPathAnimation : public IAnimation {
private:
    std::shared_ptr<Cfb> cfb_;
    float time_;

    static constexpr int NUMBER_OF_JUMPS = 90;
    static constexpr float DOT_COUNT = 50.0f;

    std::vector<PVector> positions_;
    std::vector<float> sizes_;
    uint16_t screen_width_;
    uint16_t screen_height_;
    std::mt19937 rng_;

    void DrawDot(uint16_t x, uint16_t y, float size);
    void GetPositionAndSize(float p, float& x, float& y, float& size) const;
    void GeneratePath(float x, float y, uint16_t screen_width, uint16_t screen_height, float empty_margin);
    void ApplyJump(float& x, float& y, float jump_length, int direction);
    void ClampToBounds(
        float& x,
        float& y,
        float jump_length,
        uint16_t screen_width,
        uint16_t screen_height,
        float empty_margin
    );
    float GenerateRandomPosition(float min, float max);

public:
    explicit DotPathAnimation(std::shared_ptr<Cfb> cfb);

    void Initialize() override;
    void Animate() override;
};

} // namespace eerie_leap::views::animations
