#pragma once

#include <cmath>

namespace eerie_leap::views::animations::utilities {

class AnimationMathHelpers {
public:
    static constexpr float PI = 3.14159265359F;

    static float Ease(float p) {
        return 3.0F * p * p - 2.0F * p * p * p;
    }

    static float EaseWithGamma(float p, float g) {
        if(p < 0.5F)
            return 0.5F * powf(2.0F * p, g);
        else
            return 1.0F - 0.5F * powf(2.0F * (1.0F - p), g);
    }
};

} // namespace eerie_leap::views::animations::utilities
