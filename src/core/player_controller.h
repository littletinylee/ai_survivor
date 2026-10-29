#pragma once

#include "core/game_state.h"

namespace ai_survivors {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

// Pure 2D movement. Movement happens only while the state is Playing.
class PlayerController {
public:
    // Arena is centered on the origin: x in [-half_width, half_width], etc.
    // Radius keeps the whole body inside the arena.
    PlayerController(double half_width, double half_height, double radius,
                     double move_speed);

    // Rejects non-finite or non-positive speed (keeps the old value).
    bool set_move_speed(double move_speed);
    double move_speed() const { return move_speed_; }

    Vec2 position() const { return position_; }
    void reset(Vec2 position = Vec2{});

    // Input is normalized so diagonals are not faster. Non-finite input or
    // delta is rejected. Returns true when the position was processed.
    bool step(GameState state, double delta_seconds, double input_x,
              double input_y);

private:
    double half_width_;
    double half_height_;
    double radius_;
    double move_speed_;
    Vec2 position_;
};

} // namespace ai_survivors
