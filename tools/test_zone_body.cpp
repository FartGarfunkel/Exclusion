// Headless checks for the Anomaly body, goodwill and trader prices. No world, no graphics.
//   ./build/test_zone_body
#include "core/init.h"
#include "sim/player.h"
#include "sim/zone.h"
#include "gen/zone_plan.h"
#include <cmath>
#include <cstdio>
#include <string>

using namespace dq;
using namespace dq::zone;

static int fails = 0;
static void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) fails++;
}

int main() {
  CoreOptions co; co.server = true; co.userDirOverride = "/tmp/dq-test-zone-body";
  if (!initCore(co)) { std::printf("initCore failed\n"); return 99; }

  Player p;
  check(std::fabs(carryCap(p) - 50.f) < 0.01f, "base carry is 50 kg");
  check(std::fabs(walkCap(p) - 100.f) < 0.01f, "walk cap is carry + 50");
  check(std::fabs(moveSlow(p) - 1.f) < 0.01f, "an empty pack walks");
  check(!noSprint(p), "an empty pack can sprint");

  p.inv[0] = ItemStack("lead_sheet", 20);
  p.inv[1] = ItemStack("lead_sheet", 20);
  p.inv[2] = ItemStack("lead_sheet", 20);
  p.inv[3] = ItemStack("lead_sheet", 20);
  p.inv[4] = ItemStack("lead_sheet", 15);   // 95 kg, inside the last 10 of a 100 kg walk
  check(carryWeight(p) > 90.f && carryWeight(p) < 100.f, "the pack is in the heavy band");
  check(moveSlow(p) < 0.9f && moveSlow(p) > 0.4f, "within 10 kg of the walk cap he slows");
  p.inv[4] = ItemStack("lead_sheet", 20);
  p.inv[5] = ItemStack("lead_sheet", 1);    // 101 kg
  check(carryWeight(p) > walkCap(p), "past the walk cap");
  check(moveSlow(p) < 0.1f, "past the walk cap he can barely move");
  check(noSprint(p), "past the walk cap he cannot sprint");

  Player loner;
  check(attitude(loner, F_BANDIT) < 0, "bandits attack a loner");
  check(attitude(loner, F_ARMY) < 0, "the army attacks a loner");
  check(attitude(loner, F_MERC) < 0, "mercenaries attack a loner");
  check(attitude(loner, F_CHOIR) < 0, "the monolith attacks a loner");
  check(attitude(loner, F_DRIFTER) == 0, "loners ignore a loner until goodwill moves");
  check(attitude(loner, F_BASTION) == 0, "duty ignores a loner");
  check(attitude(loner, F_INSTITUTE) == 0, "ecologists ignore a loner");
  check(standing(loner, F_BANDIT) <= -2000, "bandit community goodwill is -2000");
  loner.stats["zone"]["faction"] = (int)F_BANDIT;
  check(attitude(loner, F_BANDIT) == 0, "bandits do not auto-friend their own actor");
  check(attitude(loner, F_DRIFTER) < 0, "loners attack a bandit");
  loner.stats["zone"]["faction"] = (int)F_CHOIR;
  check(attitude(loner, F_CHOIR) > 0, "the monolith already counts its own as a friend");

  const double bandageBuy = buyPrice("bandage", "hamlet");
  check(std::fabs(bandageBuy - 90.0) < 0.01, "Sidorovich sells a bandage at trasher x econ sell");
  ItemStack bandage("bandage", 1);
  check(std::fabs(sellPrice(bandage, "hamlet") - 25.0) < 0.01, "Sidorovich buys a bandage at trasher x econ buy");
  ItemStack worn("pm", 1); worn.d = 600;
  check(sellPrice(worn, "hamlet") == 0.0, "he refuses a gun under half condition");
  ItemStack fresh("pm", 1);
  check(std::fabs(sellPrice(fresh, "hamlet") - 75.0) < 0.01, "a fresh PM sells on the condition curve at full");
  ItemStack art("moth_wing", 1);
  const double artSell = sellPrice(art, "kalina");
  check(artSell > sellPrice(art, "hamlet"), "the ecologists pay the artefact section rate");
  check(artSell < art.n * 4000, "they do not pay the catalogue price");

  Player hurt;
  openWound(hurt, "torso", 1.f);
  check(std::fabs(zget(hurt, "bleed") - 1.f) < 0.01f, "a torso hit publishes bleed");
  openWound(hurt, "head", 0.5f);
  check(std::fabs(zget(hurt, "bleed") - 1.5f) < 0.01f, "a second place adds to the bleed the HUD reads");

  std::printf("%d failed\n", fails);
  return fails ? 1 : 0;
}
