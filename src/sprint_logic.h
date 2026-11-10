// SprintSplit - separate the sprint key from the dodge key in ELDEN RING.
//
// This header is the whole decision logic and deliberately has no Windows
// dependency: the exact same algorithm is mirrored in tools/simulate.py so the
// timing behaviour can be checked without running the game.
//
// Input model: the game polls XInputGetState() every frame. It treats the dodge
// button (B) as a *tap for roll/backstep* and as a *hold for sprint*. This class
// rewrites that button so that:
//   * the physical B button is always reported as one short tap,
//   * a configurable "sprint" button is reported as a held B while moving.
//
// Output model per call: the button word the game will see, plus the flags the
// proxy needs to bump dwPacketNumber.
#pragma once

#include <cstdint>

namespace ss {

// XINPUT_GAMEPAD_* button bits (duplicated so this header stays portable).
enum ButtonMask : std::uint16_t {
    kUp            = 0x0001,
    kDown          = 0x0002,
    kLeft          = 0x0004,
    kRight         = 0x0008,
    kStart         = 0x0010,
    kBack          = 0x0020,
    kLeftThumb     = 0x0040,
    kRightThumb    = 0x0080,
    kLeftShoulder  = 0x0100,
    kRightShoulder = 0x0200,
    kGuide         = 0x0400,
    kA             = 0x1000,
    kB             = 0x2000,
    kX             = 0x4000,
    kY             = 0x8000
};

struct Options {
    bool     enabled              = true;
    std::uint16_t sprint_mask     = kLeftThumb;  // hold this to sprint
    float    deadzone             = 0.25f;       // left stick "is moving" threshold
    int      tap_ms               = 30;          // length of the synthetic B tap (2 frames @60)
    int      pre_gap_ms           = 30;          // B release before a tap while sprinting
    int      post_gap_ms          = 30;          // extra release before sprint resumes
    int      sprint_release_ms    = 150;         // keep sprinting this long after stopping
    bool     resume_sprint        = true;        // re-hold B after a roll ends
    bool     suppress_sprint_btn  = true;        // hide the sprint button from the game
    int      user_index           = -1;          // -1 = every pad, else 0..3
};

struct Input {
    std::uint16_t buttons = 0;
    std::int16_t  lx = 0;
    std::int16_t  ly = 0;
};

struct Output {
    std::uint16_t buttons = 0;
    bool          modified = false;
};

class Splitter {
public:
    void Reset(std::uint64_t now_ms) {
        phase_ = Phase::Idle;
        phase_end_ = 0;
        prev_buttons_ = 0;
        have_prev_ = false;
        sprint_injected_ = false;
        sprint_cancelled_ = false;
        sprint_consumed_ = false;
        resume_pending_ = false;
        last_move_ms_ = now_ms;
    }

    Output Update(const Input& in, const Options& opt, std::uint64_t now_ms) {
        Output out;
        out.buttons = in.buttons;

        if (!have_prev_) {
            prev_buttons_ = in.buttons;
            have_prev_ = true;
        }

        const bool sprint_down = (in.buttons & opt.sprint_mask) != 0;
        const bool dodge_down  = (in.buttons & kB) != 0;
        const bool moving      = IsMoving(in, opt.deadzone);
        if (moving) last_move_ms_ = now_ms;

        // A released sprint key always re-arms the sprint.
        if (!sprint_down) sprint_cancelled_ = false;

        // Start / keep the synthetic B hold that makes the game sprint.
        if (sprint_down && !sprint_cancelled_) {
            if (!sprint_injected_ && moving) sprint_injected_ = true;
        } else {
            sprint_injected_ = false;
        }
        if (sprint_injected_ && !moving &&
            now_ms - last_move_ms_ >= static_cast<std::uint64_t>(opt.sprint_release_ms)) {
            sprint_injected_ = false;
        }

        // Advance the tap timeline. A phase completes on the first poll at or
        // after its deadline, so every duration below is a minimum.
        if (phase_ != Phase::Idle && now_ms >= phase_end_) {
            switch (phase_) {
            case Phase::PreGap:
                phase_ = Phase::Tap;
                phase_end_ = now_ms + static_cast<std::uint64_t>(opt.tap_ms);
                break;
            case Phase::Tap:
                if (resume_pending_ && sprint_down) {
                    phase_ = Phase::PostGap;
                    phase_end_ = now_ms + static_cast<std::uint64_t>(opt.post_gap_ms);
                } else {
                    phase_ = Phase::Idle;
                    phase_end_ = 0;
                }
                break;
            case Phase::PostGap:
                phase_ = Phase::Idle;
                phase_end_ = 0;
                sprint_cancelled_ = false;
                sprint_injected_ = sprint_down && moving;
                break;
            default:
                break;
            }
        }

        // Physical dodge press: always exactly one synthetic tap, never a hold.
        const bool dodge_pressed = dodge_down && !(prev_buttons_ & kB);
        if (dodge_pressed) {
            const bool was_holding =
                (phase_ == Phase::Tap) ||
                (phase_ == Phase::PostGap) ||
                (phase_ == Phase::Idle && sprint_injected_);

            sprint_injected_ = false;
            resume_pending_ = opt.resume_sprint && sprint_down;
            sprint_cancelled_ = !resume_pending_;

            if (was_holding) {
                phase_ = Phase::PreGap;
                phase_end_ = now_ms + static_cast<std::uint64_t>(opt.pre_gap_ms);
            } else {
                phase_ = Phase::Tap;
                phase_end_ = now_ms + static_cast<std::uint64_t>(opt.tap_ms);
            }
        }

        bool dodge_out = false;
        switch (phase_) {
        case Phase::Tap:
            dodge_out = true;
            break;
        case Phase::Idle:
            dodge_out = sprint_injected_;
            break;
        default:
            dodge_out = false;
            break;
        }

        if (dodge_out != dodge_down) {
            if (dodge_out) {
                out.buttons = static_cast<std::uint16_t>(out.buttons | kB);
            } else {
                out.buttons = static_cast<std::uint16_t>(out.buttons & ~kB);
            }
        }

        // Hide the sprint button for as long as it is used as sprint. The latch
        // matters: without it, letting go of the stick (or of the key) would
        // re-expose the button and the game would read a fresh press, e.g. L3
        // toggling crouch right after a sprint.
        if (!sprint_down) {
            sprint_consumed_ = false;
        } else if (sprint_injected_ || moving || phase_ != Phase::Idle) {
            sprint_consumed_ = true;
        }
        if (opt.suppress_sprint_btn && sprint_consumed_) {
            out.buttons = static_cast<std::uint16_t>(out.buttons & ~opt.sprint_mask);
        }

        prev_buttons_ = in.buttons;
        out.modified = (out.buttons != in.buttons);
        return out;
    }

    static bool IsMoving(const Input& in, float deadzone) {
        const float nx = static_cast<float>(in.lx) / 32767.0f;
        const float ny = static_cast<float>(in.ly) / 32767.0f;
        return (nx * nx + ny * ny) > (deadzone * deadzone);
    }

private:
    enum class Phase { Idle, PreGap, Tap, PostGap };

    Phase           phase_ = Phase::Idle;
    std::uint64_t   phase_end_ = 0;
    std::uint16_t   prev_buttons_ = 0;
    bool            have_prev_ = false;
    bool            sprint_injected_ = false;
    bool            sprint_cancelled_ = false;
    bool            sprint_consumed_ = false;
    bool            resume_pending_ = false;
    std::uint64_t   last_move_ms_ = 0;
};

}  // namespace ss
