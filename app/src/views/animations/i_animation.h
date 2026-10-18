#pragma once

namespace eerie_leap::views::animations {

class IAnimation {
public:
    virtual ~IAnimation() = default;
    virtual void Initialize() = 0;
    virtual void Animate() = 0;
};

} // namespace eerie_leap::views::animations
