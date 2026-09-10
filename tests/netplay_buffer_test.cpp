#include "Core/NetPlay/GameCubeBuffer.h"

#include <chrono>
#include <cstdio>

using namespace std::chrono_literals;
using NetPlay::GameCubeBufferFeedback;
using NetPlay::GameCubeBufferPolicy;

namespace {
int failures = 0;
void Check(bool ok, const char* message) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
  }
}
}

int main() {
  const auto start = GameCubeBufferPolicy::Clock::time_point{} + 1s;
  GameCubeBufferPolicy policy;
  policy.Reset(start);
  unsigned target = policy.Update(2, 2, false, start);
  Check(target == 6, "Low-ping room starts with headroom");

  GameCubeBufferFeedback feedback;
  Check(!feedback.OnWait(5ms, target, start), "One short wait does not raise latency");
  Check(!feedback.OnWait(5ms, target, start + 2s), "Separate short waits do not accumulate");
  Check(!feedback.OnWait(5ms, target, start + 2100ms), "Two short waits are tolerated");
  const auto recurring = feedback.OnWait(6ms, target, start + 2200ms);
  Check(recurring && *recurring == 8, "Repeated short stalls request useful headroom");
  target = policy.Request(target, recurring.value_or(target), start + 2200ms);
  Check(target == 8, "Low ping does not cap stall feedback at four polls");

  Check(!feedback.OnWait(80ms, target, start + 2500ms), "Growth has a wall-clock cooldown");
  const auto big = feedback.OnWait(80ms, target, start + 3300ms);
  Check(big && *big == 12, "A big stall grows promptly to the bounded target");
  target = policy.Request(target, big.value_or(target), start + 3300ms);
  Check(!feedback.OnWait(500ms, target, start + 5s), "Long freezes cannot grow latency without limit");
  Check(policy.Request(target, 20, start + 5s) == 12, "Server enforces the cap for every peer");
  Check(policy.Request(target, 8, start + 5s) == 12, "Delayed peer feedback cannot reduce headroom");

  // A quiet section or a loading screen must not discard what was learned in
  // the previous minigame. Exercise the actual production policy with time
  // advanced rather than sleeping for a whole Mario Party session.
  for (int seconds = 4; seconds <= 1800; ++seconds)
    target = policy.Update(target, 2, true, start + std::chrono::seconds(seconds));
  Check(target == 12, "Thirty minutes of play retain headroom despite low ping");
  Check(policy.Update(target, 2, false, start + 1829s) == 12,
        "Returning to the lobby does not immediately shrink the buffer");
  target = policy.Update(target, 2, false, start + 1830s);
  Check(target == 11, "Lobby recovery decreases one poll after thirty seconds");
  Check(policy.Update(target, 2, false, start + 1831s) == 11,
        "Lobby recovery is gradual");
  Check(policy.Update(6, 10, true, start + 1840s) == 10,
        "A worsened link can still increase the buffer during play");
  Check(policy.Update(6, 100, true, start + 1841s) == 12,
        "Ping spikes also respect the response-time cap");

  policy = {};
  feedback = {};
  policy.Reset(start);
  Check(policy.Update(2, 2, false, start) == 6, "A new room does not inherit accumulated delay");
  const auto first_big = feedback.OnWait(40ms, 6, start);
  Check(first_big && *first_big == 9, "The first substantial stall does not wait for a cooldown");

  std::printf("GameCube buffer tests: %s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
  return failures ? 1 : 0;
}
