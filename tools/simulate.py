"""Mirror of src/sprint_logic.h, used to check the timing without the game.

Run:  python tools/simulate.py

The simulator feeds 60 Hz frames through the exact same state machine that the
DLL runs (kept in sync by hand) and prints, for every scenario, when the game
would see the dodge button go down/up. A B press shorter than 150 ms is what the
game treats as "tap" (roll / backstep); a longer one is "hold" (sprint).
"""

from __future__ import annotations

from dataclasses import dataclass

FPS = 60.0
FRAME_MS = 1000.0 / FPS

K_UP, K_DOWN, K_LEFT, K_RIGHT = 0x0001, 0x0002, 0x0004, 0x0008
K_START, K_BACK, K_L3, K_R3 = 0x0010, 0x0020, 0x0040, 0x0080
K_LB, K_RB, K_GUIDE = 0x0100, 0x0200, 0x0400
K_A, K_B, K_X, K_Y = 0x1000, 0x2000, 0x4000, 0x8000


@dataclass
class Options:
    enabled: bool = True
    sprint_mask: int = K_L3
    deadzone: float = 0.25
    tap_ms: int = 30
    pre_gap_ms: int = 30
    post_gap_ms: int = 30
    sprint_release_ms: int = 150
    resume_sprint: bool = True
    suppress_sprint_btn: bool = True
    user_index: int = -1


IDLE, PRE_GAP, TAP, POST_GAP = "idle", "pre_gap", "tap", "post_gap"


class Splitter:
    def __init__(self) -> None:
        self.phase = IDLE
        self.phase_end = 0.0
        self.prev_buttons = 0
        self.have_prev = False
        self.sprint_injected = False
        self.sprint_cancelled = False
        self.sprint_consumed = False
        self.resume_pending = False
        self.last_move_ms = 0.0

    def reset(self, now: float) -> None:
        self.__init__()  # type: ignore[misc]
        self.last_move_ms = now

    def update(self, buttons: int, lx: int, ly: int, opt: Options, now: float):
        if not self.have_prev:
            self.prev_buttons = buttons
            self.have_prev = True

        sprint_down = (buttons & opt.sprint_mask) != 0
        dodge_down = (buttons & K_B) != 0
        nx, ny = lx / 32767.0, ly / 32767.0
        moving = (nx * nx + ny * ny) > (opt.deadzone * opt.deadzone)
        if moving:
            self.last_move_ms = now

        if not sprint_down:
            self.sprint_cancelled = False

        if sprint_down and not self.sprint_cancelled:
            if not self.sprint_injected and moving:
                self.sprint_injected = True
        else:
            self.sprint_injected = False
        if (
            self.sprint_injected
            and not moving
            and now - self.last_move_ms >= opt.sprint_release_ms
        ):
            self.sprint_injected = False

        if self.phase != IDLE and now >= self.phase_end:
            if self.phase == PRE_GAP:
                self.phase = TAP
                self.phase_end = now + opt.tap_ms
            elif self.phase == TAP:
                if self.resume_pending and sprint_down:
                    self.phase = POST_GAP
                    self.phase_end = now + opt.post_gap_ms
                else:
                    self.phase = IDLE
                    self.phase_end = 0.0
            elif self.phase == POST_GAP:
                self.phase = IDLE
                self.phase_end = 0.0
                self.sprint_cancelled = False
                self.sprint_injected = sprint_down and moving

        dodge_pressed = dodge_down and not (self.prev_buttons & K_B)
        if dodge_pressed:
            was_holding = (
                self.phase == TAP
                or self.phase == POST_GAP
                or (self.phase == IDLE and self.sprint_injected)
            )
            self.sprint_injected = False
            self.resume_pending = opt.resume_sprint and sprint_down
            self.sprint_cancelled = not self.resume_pending
            if was_holding:
                self.phase = PRE_GAP
                self.phase_end = now + opt.pre_gap_ms
            else:
                self.phase = TAP
                self.phase_end = now + opt.tap_ms

        if self.phase == TAP:
            dodge_out = True
        elif self.phase == IDLE:
            dodge_out = self.sprint_injected
        else:
            dodge_out = False

        out = buttons
        if dodge_out != dodge_down:
            out = (out | K_B) if dodge_out else (out & ~K_B)

        if not sprint_down:
            self.sprint_consumed = False
        elif self.sprint_injected or moving or self.phase != IDLE:
            self.sprint_consumed = True
        if opt.suppress_sprint_btn and self.sprint_consumed:
            out &= ~opt.sprint_mask

        self.prev_buttons = buttons
        return out


def run_scenario(name: str, frames, opt: Options):
    """frames: list of (buttons, lx, ly) sampled at 60 Hz."""
    splitter = Splitter()
    splitter.reset(0.0)
    print(f"\n=== {name} ===")
    prev_b = None
    prev_sprint_seen = None
    b_down_at = None
    events = []
    for i, (buttons, lx, ly) in enumerate(frames):
        now = i * FRAME_MS
        out = splitter.update(buttons, lx, ly, opt, now)
        b = bool(out & K_B)
        sprint_hidden = (buttons & opt.sprint_mask) and not (out & opt.sprint_mask)
        sprint_seen = bool(out & opt.sprint_mask)
        sprint_physical = bool(buttons & opt.sprint_mask)
        if prev_sprint_seen is not None and sprint_seen != prev_sprint_seen:
            if not sprint_physical:
                label = "sprint key released"
            elif sprint_seen:
                label = "sprint key passed to game"
            else:
                label = "sprint key swallowed"
            events.append((now, label, False))
        prev_sprint_seen = sprint_seen
        if prev_b is None:
            prev_b = b
        if b != prev_b:
            if b:
                b_down_at = now
                events.append((now, "B down", sprint_hidden))
            else:
                events.append((now, f"B up (held {now - b_down_at:.0f} ms)", sprint_hidden))
            prev_b = b
    for when, what, hidden in events:
        print(f"  {when:7.0f} ms  {what}{'   [sprint key hidden]' if hidden else ''}")
    return events


def frames_builder(duration_ms: int):
    count = int(duration_ms / FRAME_MS)
    return [i * FRAME_MS for i in range(count)]


def scenario(name, duration_ms, script):
    """script(ms) -> (buttons, lx, ly)"""
    frames = [(script(t),) for t in frames_builder(duration_ms)]
    return run_scenario(name, [f[0] for f in frames], Options())


STICK = 32000


def main() -> None:
    opt = Options()

    def plain_tap(t):
        b = K_B if 200 <= t < 260 else 0
        return (b, 0, 0)

    scenario("1) standing, tap B", 700, plain_tap)

    def moving_tap(t):
        b = K_B if 200 <= t < 260 else 0
        return (b, 0, STICK if t >= 100 else 0)

    scenario("2) moving, tap B", 700, moving_tap)

    def hold_b(t):
        b = K_B if 200 <= t < 800 else 0
        return (b, 0, STICK if t >= 100 else 0)

    scenario("3) moving, HOLD B for 600 ms (must never sprint)", 1000, hold_b)

    def sprint(t):
        l3 = K_L3 if t >= 200 else 0
        return (l3, 0, STICK if t >= 400 else 0)

    scenario("4) hold L3 while standing, then move", 1200, sprint)

    def sprint_roll(t):
        l3 = K_L3 if t >= 200 else 0
        b = K_B if 900 <= t < 1000 else 0
        return (l3 | b, 0, STICK if t >= 400 else 0)

    scenario("5) sprinting, press B to roll", 1500, sprint_roll)

    def stop_while_sprinting(t):
        l3 = K_L3 if t >= 200 else 0
        return (l3, 0, STICK if 400 <= t < 1000 else 0)

    scenario("6) sprint, then stop moving", 1400, stop_while_sprinting)

    def crouch(t):
        return (K_L3 if 200 <= t < 400 else 0, 0, 0)

    scenario("7) standing, tap L3 (crouch must still work)", 700, crouch)

    def press_while_moving(t):
        l3 = K_L3 if t >= 600 else 0
        return (l3, 0, STICK if t >= 100 else 0)

    scenario("8) already moving, press L3 -> sprint", 1200, press_while_moving)

    def release_while_moving(t):
        l3 = K_L3 if 200 <= t < 900 else 0
        return (l3, 0, STICK if t >= 100 else 0)

    scenario("9) sprinting, release L3 (still moving)", 1200, release_while_moving)


if __name__ == "__main__":
    main()
