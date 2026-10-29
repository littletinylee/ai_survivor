#include "core/player_controller.h"

#include <algorithm>
#include <cmath>

namespace ai_survivors {

PlayerController::PlayerController(double half_width, double half_height,
                                   double radius, double move_speed)
    : half_width_(half_width),
      half_height_(half_height),
      radius_(radius),
      move_speed_(move_speed) {}

bool PlayerController::set_move_speed(double move_speed) {
    if (!std::isfinite(move_speed) || move_speed <= 0.0) {
        return false;
    }
    move_speed_ = move_speed;
    return true;
}

void PlayerController::reset(Vec2 position) { position_ = position; }

bool PlayerController::step(GameState state, double delta_seconds,
                            double input_x, double input_y) {
    if (state != GameState::kPlaying) {
        return false;
    }
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0 ||
        !std::isfinite(input_x) || !std::isfinite(input_y)) {
        return false;
    }
    const double length = std::sqrt(input_x * input_x + input_y * input_y);
    if (length > 0.0) {
        const double scale = move_speed_ * delta_seconds / length;
        position_.x += input_x * scale;
        position_.y += input_y * scale;
    }
    const double max_x = std::max(0.0, half_width_ - radius_);
    const double max_y = std::max(0.0, half_height_ - radius_);
    position_.x = std::clamp(position_.x, -max_x, max_x);
    position_.y = std::clamp(position_.y, -max_y, max_y);
    return true;
}

} // namespace ai_survivors
