// assets/zone_item_models.cpp — the Zone's items as 3D models (see the header). Each model is drawn on a Canvas in voxels
// (x right, y up, z toward the viewer) and scaled into the item-model frame by finishCanvas.
#include "assets/zone_item_models.h"
#include "assets/zone_model_kit.h"
#include "render/zone_gun_models.h"
#include "sim/zone.h"

namespace dq {
namespace {
using namespace zk;

// a little noise on a colour (stone, meat, fur, cloth)
Col noisy(int c, double amt, int seed = 3) { return Col(ZCF { return shadeC(c, 1.0 - amt * 0.5 + amt * hash3(x, y, z, seed)); }); }
bool fin(VoxelModel& out, Canvas& c, float size, bool stand = false) { return finishCanvas(out, c.g, size, stand); }

// ------------------------------------------------------------------ ammunition
struct AmmoLook { int box, band; bool metal; int w, h, d; };
AmmoLook ammoLook(const std::string& cal) {
  if (cal == "9x18") return {0xB8925A, 0xD8B23A, false, 22, 8, 14};
  if (cal == "9x19") return {0xB8925A, 0x3A6AB0, false, 22, 8, 14};
  if (cal == "45acp") return {0xD8D2C0, 0x8A2A2A, false, 22, 8, 14};
  if (cal == "9x39") return {0x3A3A44, 0x7A4AA0, false, 26, 9, 15};
  if (cal == "545") return {0x8A7A52, 0x3E7A3A, false, 28, 10, 16};
  if (cal == "556") return {0x6E7456, 0xE0E0D8, false, 28, 10, 16};
  if (cal == "762x39") return {0xA88A52, 0xD87A2A, false, 28, 10, 16};
  if (cal == "762x51") return {0x56603E, 0xD8C040, true, 30, 14, 16};
  if (cal == "762x54") return {0x4C5436, 0x9A3A2A, true, 30, 14, 16};
  if (cal == "50bmg") return {0x4A5236, 0xD8C040, true, 34, 18, 18};
  return {0xA08A5A, 0xC0C0C0, false, 24, 9, 15};
}
bool ammo(const std::string& id, const zone::ZItem* z, VoxelModel& out) {
  if (id == "ammo_12g_buck" || id == "ammo_12g_slug" || id == "ammo_12g_dart") {   // loose shells
    const int hull = id == "ammo_12g_buck" ? 0xB02A24 : id == "ammo_12g_slug" ? 0x2E7A3A : 0x2A5AB0;
    Canvas c(26, 22, 20);
    const float pos[3][2] = {{5, 5}, {5, 14}, {11.5f, 9.5f}};
    for (auto& p : pos) {
      c.cylX(p[0], p[1], 3.5f, 3.5f, 4, 21, hull);                       // plastic hull
      c.cylX(p[0], p[1], 3.7f, 3.7f, 1, 5, Col(ZCF { return (x & 1) ? 0xC8A24A : 0xB8923A; }));   // brass head
      c.cylX(p[0], p[1], 3.7f, 3.7f, 0, 0, 0x8A6A2A);                    // rim
      c.cylX(p[0], p[1], 2.2f, 2.2f, 21, 22, shadeC(hull, 0.7));         // crimp
      c.cylX(p[0], p[1], 3.6f, 3.6f, 14, 15, shadeC(hull, 0.85));
    }
    return fin(out, c, 0.62f);
  }
  if (id == "ammo_pg7") {   // the PG-7 rocket grenade: fins, stem, bulb warhead, nose
    Canvas c(52, 20, 20); const int OD = 0x4A5236;
    c.cylX(10, 10, 2.6f, 2.6f, 0, 14, 0x6A6E74);
    for (int k = 0; k < 4; k++) { double a = k * 1.5708 + 0.785; c.rod(vec3(1, 10, 10), vec3(8, 10 + 7.5f * std::sin(a), 10 + 7.5f * std::cos(a)), 0.9f, 0.9f, 0x55575C); }
    c.cylX(10, 10, 3.2f, 3.2f, 12, 22, 0x3A3C40);
    c.ell(30, 10, 10, 10.5f, 8, 8, Col(ZCF { return (x == 27 || x == 28) ? shadeC(OD, 0.6) : OD; }));
    c.coneX(10, 10, 6.5f, 1.8f, 36, 45, shadeC(OD, 0.85));
    c.cylX(10, 10, 1.6f, 1.6f, 45, 48, 0x6A6E74);
    c.cylX(10, 10, 3.4f, 3.4f, 20, 21, 0xD87A2A);
    return fin(out, c, 0.98f);
  }
  if (id == "vog25" || id == "m203_he") {   // 40 mm grenade rounds: a case and the grenade
    const bool v = id == "vog25"; Canvas c(40, 14, 14);
    c.cylX(7, 7, 4.2f, 4.2f, 0, 13, v ? Col(0xB8964A) : Col(0xA8ACB0));
    c.cylX(7, 7, 4.5f, 4.5f, 0, 1, v ? 0x8A6A2A : 0x80848A);
    c.cylX(7, 7, 4.0f, 4.0f, 14, 27, v ? 0x6A7048 : 0xC8A82A);
    c.cylX(7, 7, 4.2f, 4.2f, 19, 20, v ? 0x2E3220 : 0x2A2A2A);
    c.coneX(7, 7, 4.0f, 1.2f, 28, 36, v ? 0x6A7048 : 0xC8A82A);
    c.cylX(7, 7, 1.0f, 1.0f, 36, 38, 0x55575C);
    return fin(out, c, 0.75f);
  }
  const AmmoLook L = ammoLook(z ? z->raw.value("ammo", json::object()).value("cal", "") : "");
  const bool ap = id.size() > 3 && (id.compare(id.size() - 3, 3, "_ap") == 0 || id.find("_pbp") != std::string::npos);
  const int w = L.w, h = L.h, d = L.d; Canvas c(w + 6, h + 12, d + 6);
  const int B = L.box, band = L.band; const bool metal = L.metal;
  c.box(2, 2, 2, w + 1, h + 1, d + 1, Col(ZCF { int k = (y == h - 1) ? 2 : 0; (void)k;
    if (y >= h - 1 && y <= h) return shadeC(B, 0.78);              // lid seam
    if (y >= 3 && y <= 5 && !metal) return band;                    // calibre band round the box
    return noisy(B, 0.12, 5).at(x, y, z); }));
  c.box(2, h + 1, 2, w + 1, h + 1, d + 1, shadeC(B, 1.12));       // lid top
  if (metal) {   // an ammo can: rim, latch, handle, stencilled band
    c.box(1, h, 1, w + 2, h + 1, d + 2, shadeC(B, 0.85));
    c.box(w / 2 - 1, h - 6, d + 2, w / 2 + 4, h - 1, d + 3, 0x9A9EA4);
    c.rod(vec3(w / 2 - 3, h + 2, d / 2 + 2), vec3(w / 2 - 3, h + 6, d / 2 + 2), 1.1f, 1.1f, 0x2A2C30);
    c.rod(vec3(w / 2 + 7, h + 2, d / 2 + 2), vec3(w / 2 + 7, h + 6, d / 2 + 2), 1.1f, 1.1f, 0x2A2C30);
    c.rod(vec3(w / 2 - 3, h + 6, d / 2 + 2), vec3(w / 2 + 7, h + 6, d / 2 + 2), 1.1f, 1.1f, 0x2A2C30);
    c.box(4, h / 2 - 2, d + 2, w - 3, h / 2 + 2, d + 2, band);
    for (int x = 6; x < w - 5; x += 3) c.box(x, h / 2 - 1, d + 3, x + 1, h / 2 + 1, d + 3, 0x1E1E20);
  } else {       // a cardboard box: white label with the bullet on it
    c.box(w / 4, 4, d + 2, w - w / 4, h - 3, d + 2, 0xE8E4D0);
    for (int y = 6; y <= h - 5; y += 2) c.box(w / 4 + 6, y, d + 3, w - w / 4 - 1, y, d + 3, 0x3A3A3A);
    c.box(w / 4 + 2, 6, d + 3, w / 4 + 3, h - 6, d + 3, 0xC8963C);
    c.box(w / 4 + 2, h - 5, d + 3, w / 4 + 3, h - 5, d + 3, 0xA0602A);
  }
  if (ap) for (int k = 0; k < 4; k++) c.box(2 + w / 2 - 6 + k, 2, d + 2 + (metal ? 2 : 0), 2 + w / 2 - 3 + k, h + 1, d + 2 + (metal ? 2 : 0), 0xC02020);   // the red stripe
  return fin(out, c, 0.72f);
}

// ------------------------------------------------------------------ grenades
bool grenade(const std::string& id, VoxelModel& out) {
  const bool f1 = id == "grenade_f1"; Canvas c(34, 40, 28);
  const int OD = f1 ? 0x4E5A34 : 0x5E6B3C;
  c.ell(14, 14, 14, f1 ? 9.f : 9.5f, f1 ? 12.5f : 11.f, f1 ? 9.f : 9.5f, Col(ZCF {
    if (f1) return ((x % 4 == 0) || (y % 4 == 0) || (z % 4 == 0)) ? shadeC(OD, 0.62) : shadeC(OD, 1.0 + 0.1 * hash3(x, y, z, 2));
    return y == 14 ? shadeC(OD, 0.7) : noisy(OD, 0.14, 8).at(x, y, z); }));
  c.cylY(14, 14, 3.2f, 3.2f, f1 ? 26 : 24, f1 ? 28 : 26, 0x55575C);
  c.cylY(14, 14, 3.8f, 3.8f, f1 ? 28 : 26, f1 ? 31 : 29, 0x7A7E84);
  c.rod(vec3(19, f1 ? 29 : 27, 14), vec3(23, f1 ? 21 : 18, 14), 0.9f, 0.9f, 0x8A8E94);   // the safety lever
  c.rod(vec3(23, f1 ? 21 : 18, 14), vec3(23.5f, 8, 14), 1.1f, 1.1f, 0x8A8E94);
  c.ringZ(8.5f, f1 ? 31.f : 29.f, 3.f, 1.6f, 13, 14, 0xC8C8C0);                           // the pin's ring
  return fin(out, c, 0.62f, true);
}

// ------------------------------------------------------------------ medicine
void cross(Canvas& c, int cx, int cy, int arm, int th, int z, int col) {
  c.box(cx - arm, cy - th / 2, z, cx + arm, cy + th - th / 2 - 1, z, col); c.box(cx - th / 2, cy - arm, z, cx + th - th / 2 - 1, cy + arm, z, col);
}
void bottle(Canvas& c, float cx, float cz, float r, int h, int glass, int cap, int neckH, float neckR, const Col& body) {   // a round bottle on y = 0
  c.cylY(cx, cz, r, r, 0, h, body);
  c.coneY(cx, cz, r, neckR, h + 1, h + 3, glass);
  c.cylY(cx, cz, neckR, neckR, h + 4, h + 3 + neckH, glass);
  c.cylY(cx, cz, neckR + 0.9f, neckR + 0.9f, h + 4 + neckH, h + 6 + neckH, cap);
}
bool med(const std::string& id, VoxelModel& out) {
  if (id == "medkit" || id == "medkit_army" || id == "medkit_sci") {
    const int B = id == "medkit" ? 0xB02828 : id == "medkit_army" ? 0x56603E : 0xD8A820, X = id == "medkit_sci" ? 0xFFFFFF : 0xF0F0F0;
    Canvas c(40, 32, 20);
    c.box(2, 0, 2, 35, 21, 13, Col(ZCF { return (y == 13) ? shadeC(B, 0.62) : (y == 0 || x == 2 || x == 35) ? shadeC(B, 0.82) : noisy(B, 0.08, 4).at(x, y, z); }));
    c.box(1, 14, 1, 36, 14, 14, shadeC(B, 0.7));     // the lid's lip
    cross(c, 19, 7, 6, 4, 14, X);
    c.rod(vec3(12, 22, 8), vec3(12, 26, 8), 1.3f, 1.3f, 0x2A2C30); c.rod(vec3(26, 22, 8), vec3(26, 26, 8), 1.3f, 1.3f, 0x2A2C30); c.rod(vec3(12, 26, 8), vec3(26, 26, 8), 1.3f, 1.3f, 0x2A2C30);   // the handle
    c.box(16, 12, 14, 21, 15, 15, 0xB8BCC4);         // the latch
    return fin(out, c, 0.78f);
  }
  if (id == "bandage") {
    Canvas c(34, 30, 14);
    c.ringZ(14, 15, 12, 4.5f, 0, 8, Col(ZCF { double r = std::sqrt((x - 14.0) * (x - 14.0) + (y - 15.0) * (y - 15.0)); return ((int)r % 2) ? 0xE6E2D2 : 0xD0CCB8; }));
    c.ringZ(14, 15, 12.4f, 11.6f, 4, 5, 0x3A6AB0);                                          // the tape round it
    c.box(14, 1, 0, 28, 3, 8, Col(ZCF { return (x % 4 == 0) ? 0xD0CCB8 : 0xE6E2D2; }));    // the loose end
    c.box(26, 1, 0, 29, 3, 8, 0xC8C4B0);
    c.box(12, 24, 0, 15, 28, 8, 0xE0DCCC);                                                   // the clip
    return fin(out, c, 0.62f);
  }
  if (id == "antirad") {
    Canvas c(24, 32, 24);
    bottle(c, 12, 12, 7, 17, 0xB86A18, 0xE8E8E0, 3, 3.4f, Col(ZCF {
      const bool front = z > 12 && y >= 4 && y <= 13;
      if (front) { double d2 = (x - 12.0) * (x - 12.0) + (y - 9.0) * (y - 9.0); if (d2 < 2.5) return 0x1A1A1A; if (d2 < 13) return 0xE0B820; return 0xE6DCC0; }
      return (x + z) % 7 == 0 ? 0xA05A12 : 0xB86A18; }));
    return fin(out, c, 0.55f, true);
  }
  if (id == "psyblock" || id == "atlas") {
    const bool at = id == "atlas"; const int foil = at ? 0xB8D8B0 : 0x9A8CC0, pill = at ? 0x3E8A4A : 0x6A4AB0;
    Canvas c(32, 36, 10);
    c.box(1, 1, 0, 26, 32, 1, Col(ZCF { return (y > 26) ? shadeC(foil, 0.55) : ((x + y) % 9 == 0 ? shadeC(foil, 1.15) : foil); }));
    const int cols = at ? 2 : 2, rows = at ? 4 : 3;
    for (int i = 0; i < cols; i++) for (int j = 0; j < rows; j++) {
      float cx = 8 + i * 11, cy = (at ? 5.5f : 6.f) + j * (at ? 6.f : 7.f);
      c.ell(cx, cy, 3, at ? 4.3f : 4.4f, at ? 2.4f : 3.0f, 2.6f, pill); c.ell(cx - 0.6f, cy + 0.4f, 3, at ? 2.f : 2.5f, at ? 1.2f : 1.8f, 2.8f, shadeC(pill, 1.5));
    }
    return fin(out, c, 0.55f);
  }
  if (id == "vodka") {
    Canvas c(20, 46, 20);
    bottle(c, 10, 10, 5.3f, 25, 0xBFD8D0, 0xB0B4B8, 6, 2.4f, Col(ZCF {
      if (z > 10 && y >= 7 && y <= 20) { if (y == 9 || y == 10) return 0xC02020; if (y >= 14 && y <= 15) return 0x2A4A8A; return (y == 17 && x % 2) ? 0x333333 : 0xF0F0E8; }
      return (y > 22 || y < 3) ? 0xAFC8C2 : 0xC4DCD6; }));
    return fin(out, c, 0.92f, true);
  }
  if (id == "energy") {
    Canvas c(18, 28, 18);
    c.cylY(9, 9, 5, 5, 1, 20, Col(ZCF { return (y >= 6 && y <= 8) ? 0x30C0E0 : 0x1A2538; }));
    c.cylY(9, 9, 5, 5, 0, 0, 0xB8BCC4); c.coneY(9, 9, 5, 4, 21, 22, 0xB8BCC4); c.cylY(9, 9, 4, 4, 23, 23, 0xC8CCD2);
    c.box(7, 24, 8, 11, 24, 10, 0x9A9EA4);
    c.rod(vec3(11, 18, 14.4f), vec3(7, 12, 14.4f), 0.9f, 0.9f, 0xF0D020); c.rod(vec3(7, 12, 14.4f), vec3(10, 12, 14.4f), 0.9f, 0.9f, 0xF0D020); c.rod(vec3(10, 12, 14.4f), vec3(7, 5, 14.4f), 0.9f, 0.9f, 0xF0D020);
    return fin(out, c, 0.62f, true);
  }
  return false;
}

// ------------------------------------------------------------------ food and drink
void tin(Canvas& c, float cx, float cz, float rx, float rz, int h, int label, int lid) {
  c.cylY(cx, cz, rx, rz, 0, h, Col(ZCF { return (y >= 2 && y <= h - 2) ? noisy(label, 0.08, 3).at(x, y, z) : 0xB8BCC2; }));
  c.cylY(cx, cz, rx + 0.5f, rz + 0.5f, h + 1, h + 2, lid);
  c.cylY(cx, cz, rx - 1.5f, rz - 1.5f, h + 2, h + 2, shadeC(lid, 0.85));
}
bool food(const std::string& id, VoxelModel& out) {
  if (id == "bread") {
    Canvas c(42, 26, 24);
    c.ell(21, 10, 12, 19, 9.5f, 10.5f, Col(ZCF { if (y >= 12 && ((x * 2 + z * 3) % 11) < 2) return 0xE0B070; return y >= 11 ? shadeC(0xB07A34, 0.92 + 0.16 * hash3(x, y, z, 6)) : shadeC(0x9A6428, 0.9 + 0.2 * hash3(x, y, z, 9)); }));
    return fin(out, c, 0.78f);
  }
  if (id == "sausage") {
    Canvas c(44, 18, 16); const vec3 P[4] = {{4, 6, 8}, {15, 9, 8}, {28, 9, 8}, {39, 6, 8}};
    for (int i = 0; i < 3; i++) c.rod(P[i], P[i + 1], 5.f, 5.f, Col(ZCF { return (y > 11) ? 0xB86450 : (z < 6 ? 0x7A3428 : 0x9E4A38); }));
    c.cylX(6, 8, 5.4f, 5.4f, 3, 4, 0xD8C8A0); c.cylX(6, 8, 5.4f, 5.4f, 40, 41, 0xD8C8A0);
    c.rod(vec3(3, 6, 8), vec3(0, 3, 8), 0.7f, 0.7f, 0xD8C8A0); c.rod(vec3(41, 6, 8), vec3(44, 3, 8), 0.7f, 0.7f, 0xD8C8A0);
    return fin(out, c, 0.78f);
  }
  if (id == "canned_stew") {
    Canvas c(26, 22, 26); tin(c, 13, 13, 9, 9, 13, 0xA0382A, 0xC8CCD0);
    c.ell(13, 7, 22, 5, 3, 1.3f, 0xE8D8A0); c.ell(13, 7, 22.8f, 2.5f, 1.5f, 1, 0xB86A2A);
    c.box(8, 10, 22, 18, 10, 22, 0xE8D8A0);
    c.cylY(13, 13, 3, 3, 16, 16, 0xB8BCC2); c.cylY(13, 13, 1.7f, 1.7f, 16, 16, -1);   // the ring pull
    return fin(out, c, 0.62f, true);
  }
  if (id == "canned_fish") {
    Canvas c(34, 14, 24); tin(c, 17, 12, 14, 9, 7, 0x2A6A8A, 0xC8CCD0);
    c.ell(17, 4, 20.5f, 9, 2.2f, 1.3f, 0xD8B030); c.ell(24, 4, 21, 2.4f, 2.4f, 1, 0xD8B030);
    c.box(10, 8, 21, 24, 8, 21, 0xE8E8E0);
    c.box(26, 10, 6, 31, 10, 7, 0xB8BCC2);   // the key
    return fin(out, c, 0.8f);
  }
  if (id == "crackers") {
    Canvas c(30, 40, 10);
    c.box(3, 4, 0, 26, 34, 5, Col(ZCF { return (y >= 15 && y <= 25) ? 0xC03030 : (x + y) % 11 == 0 ? 0xF0D060 : 0xE8C040; }));
    for (int y : {0, 1, 2, 3, 35, 36, 37, 38}) c.box(3, y, 1, 26, y, 3 + (y % 2 ? 0 : 0), Col(ZCF { return (x % 2) ? 0xD0A830 : 0xE8C040; }));
    c.box(8, 17, 6, 21, 23, 6, 0xD8B070);
    for (int i = 0; i < 4; i++) c.box(10 + (i % 2) * 6, 19 + (i / 2) * 3, 7, 11 + (i % 2) * 6, 19 + (i / 2) * 3, 7, 0x8A6A3A);
    return fin(out, c, 0.72f);
  }
  if (id == "water") {
    Canvas c(18, 46, 18);
    bottle(c, 9, 9, 5, 28, 0xA8D4E8, 0x2A7AC8, 4, 2.4f, Col(ZCF {
      if (y >= 10 && y <= 20) return (y >= 14 && y <= 15) ? 0xF0F4F8 : 0x2A6AB0;
      return (y < 5 && (x + z) % 3 == 0) ? 0x8EBCD2 : (y > 20 ? 0xB4DAEC : 0xA8D4E8); }));
    return fin(out, c, 0.9f, true);
  }
  if (id == "mutant_meat" || id == "mutant_meat_cooked") {
    const bool cooked = id == "mutant_meat_cooked"; Canvas c(38, 22, 30);
    const int m1 = cooked ? 0x7A4A2A : 0xA82A32, m2 = cooked ? 0x5A3420 : 0x8E2230;
    c.ell(14, 9, 13, 12, 7.5f, 10, Col(ZCF { double n = hash3(x, y, z, 5); if (!cooked && n < 0.1) return 0xE8C8C0; if (cooked && ((x + z) % 6 == 0) && y > 8) return 0x2A1810; return shadeC(m1, 0.85 + 0.3 * n); }));
    c.ell(23, 8, 16, 10, 6.5f, 9, Col(ZCF { double n = hash3(x, y, z, 6); if (!cooked && n < 0.12) return 0xE8C8C0; if (cooked && ((x - z) % 6 == 0) && y > 7) return 0x2A1810; return shadeC(m2, 0.85 + 0.3 * n); }));
    c.cylX(12, 14, 2.2f, 2.2f, 26, 34, 0xE8E0C8); c.ell(35, 12, 14, 2.6f, 3.2f, 3.2f, 0xE8E0C8);
    return fin(out, c, 0.7f);
  }
  return false;
}

// ------------------------------------------------------------------ detectors, gear
bool gear(const std::string& id, VoxelModel& out) {
  // the detectors: rugged hand-helds held up in the left hand. Each body fills its whole canvas (so the crop keeps the
  // canvas' centre) and its screen is where zoneDeviceScreen says: the live display is drawn there (ui/zone_screens.cpp)
  if (id == "detector_echo") {   // a beeper: an olive box, a dark readout window with a row of lamps, a speaker, a knob
    Canvas c(24, 40, 8); const int B = 0x5A6048;
    c.box(0, 0, 0, 23, 39, 5, Col(ZCF { if ((x < 1 || x > 22) && (y < 1 || y > 38)) return -1; if (x <= 1 || x >= 22) return shadeC(B, 0.8); return noisy(B, 0.12, 2).at(x, y, z); }));
    c.box(2, 21, 6, 21, 37, 6, 0x1C1E1A); c.box(3, 23, 6, 20, 35, 6, 0x0C100C);
    for (int j = 0; j < 4; j++) for (int i = 0; i < 6; i++) c.box(5 + i * 2, 5 + j * 2, 6, 5 + i * 2, 5 + j * 2, 6, 0x1A1C16);
    c.cylZ(18, 15, 2.6f, 2.6f, 6, 7, 0x2A2C30); c.box(17, 15, 7, 19, 15, 7, 0xC0C0B0);
    c.box(3, 16, 6, 6, 18, 6, 0x9A3A2A); c.box(0, 26, 1, 0, 33, 4, 0x2A2C2A);
    return fin(out, c, 0.9f);
  }
  if (id == "detector_bear") {   // the orange dial detector: a round dial window, a small LCD, two lamps
    Canvas c(28, 44, 9); const int B = 0xC0602A;
    c.box(0, 0, 0, 27, 43, 6, Col(ZCF { if ((x < 2 || x > 25) && (y < 2 || y > 41)) return -1; if (y < 3) return 0x2A2826; if (x <= 1 || x >= 26) return shadeC(B, 0.78); return noisy(B, 0.14, 3).at(x, y, z); }));
    c.cylZ(13.5f, 27.5f, 11.5f, 11.5f, 7, 7, 0xB8B4A8); c.cylZ(13.5f, 27.5f, 10.4f, 10.4f, 7, 7, 0x0E1A12);
    c.box(6, 5, 7, 21, 12, 7, 0x2A2C28); c.box(7, 6, 7, 20, 11, 7, 0x8AA08A);
    c.box(3, 14, 7, 5, 16, 7, 0xD8A020); c.box(22, 14, 7, 24, 16, 7, 0x40FF60 | GLOW);
    c.cylZ(24, 4, 1.6f, 1.6f, 7, 8, 0x2A2C30);
    return fin(out, c, 0.9f);
  }
  if (id == "detector_veles") {   // the radar: a dark body, a big round-cornered green screen, a lamp row, keys
    Canvas c(28, 48, 9); const int B = 0x2E3033;
    c.box(0, 0, 0, 27, 47, 6, Col(ZCF { if ((x < 2 || x > 25) && (y < 2 || y > 45)) return -1; if (x <= 1 || x >= 26) return shadeC(B, 0.75); return noisy(B, 0.08, 4).at(x, y, z); }));
    c.box(2, 20, 7, 25, 43, 7, 0x1A1C1E); c.box(4, 22, 7, 23, 41, 7, 0x0A1E12);
    for (int i = 0; i < 8; i++) c.box(5 + i * 2 + (i > 3 ? 1 : 0), 15, 7, 5 + i * 2 + (i > 3 ? 1 : 0), 17, 7, 0x5A1810);
    for (int i = 0; i < 3; i++) c.box(6 + i * 6, 5, 7, 9 + i * 6, 9, 7, i == 1 ? 0xE08A20 : 0x55575C);
    c.cylZ(4, 11, 1.4f, 1.4f, 7, 8, 0xC02020);
    return fin(out, c, 0.95f);
  }
  if (id == "headlamp") {
    Canvas c(44, 32, 26);
    c.fill(vec3(0, 0, 10), vec3(44, 32, 13), [](vec3 p) { double a = (p.x - 22) / 21.0, b = (p.y - 14) / 13.0, r = a * a + b * b; return r <= 1.0 && r >= 0.62; }, 0x1E1E20);
    c.fill(vec3(0, 0, 8), vec3(44, 32, 11), [](vec3 p) { double a = (p.x - 22) / 21.0, b = (p.y - 20) / 10.0; return b > 0 && a * a + b * b <= 1.0 && a * a + b * b >= 0.62 && p.y > 20; }, 0x2E2E30);
    c.box(15, 16, 12, 29, 27, 21, Col(ZCF { return (z == 21) ? 0x55575C : noisy(0x3A3C40, 0.1, 5).at(x, y, z); }));
    c.cylZ(22, 21.5f, 5, 5, 21, 22, 0x1A1A1C); c.cylZ(22, 21.5f, 4, 4, 22, 23, 0xFFF4C0 | GLOW);
    c.box(18, 28, 14, 26, 28, 18, 0x2A2C30);
    return fin(out, c, 0.88f);
  }
  if (id == "pda") {   // a rugged landscape handheld: a wide screen in a bezel, a d-pad on the left, two keys on the right (render/zone_pda.cpp knows the screen's place)
    Canvas c(64, 36, 8);
    c.box(0, 0, 0, 63, 35, 5, Col(ZCF { bool corner = (x <= 1 || x >= 62) && (y <= 1 || y >= 34); if (corner) return -1; if (y <= 2 && x % 4 == 0) return 0x1C1E20; return noisy(0x2A2D30, 0.08, 6).at(x, y, z); }));
    c.box(8, 4, 6, 55, 31, 6, 0x15171A);
    c.box(10, 6, 6, 53, 29, 6, Col(ZCF { if (y == 29 || y == 6) return 0x0C2019 | GLOW; return ((x + y) % 7 == 0 ? 0x16463A : 0x123A2E) | GLOW; }));
    c.box(3, 15, 6, 5, 21, 6, 0x45484C); c.box(1, 17, 6, 7, 19, 6, 0x45484C); c.box(3, 17, 7, 5, 19, 7, 0x2A2C30);
    c.box(57, 21, 6, 60, 24, 6, 0xE08A20); c.box(57, 13, 6, 60, 16, 6, 0x6A6E74); c.box(58, 28, 6, 59, 30, 6, 0x40D080 | GLOW);
    c.box(20, 33, 6, 43, 33, 6, 0x1A1C1E);
    return fin(out, c, 0.85f);
  }
  if (id == "bolt") {   // a hex nut
    Canvas c(26, 26, 10);
    c.hexZ(13, 13, 11, 0, 5, Col(ZCF { double d = std::sqrt((x - 13.0) * (x - 13.0) + (y - 13.0) * (y - 13.0)); return (z >= 5 || z <= 0) ? 0xA8ACB2 : (d > 9.6 ? 0x6A6E74 : 0x8A8E94); }));
    c.cylZ(13, 13, 5, 5, 0, 5, -1);
    c.ringZ(13, 13, 5.4f, 4.6f, 0, 5, 0x55575C);
    return fin(out, c, 0.62f);
  }
  if (id == "knife") {
    Canvas c(50, 16, 6);
    c.fill(vec3(18, 0, 1), vec3(46, 16, 5), [](vec3 p) { double t = (p.x - 18) / 28.0; double top = 10, bot = 10 - 4.2 * (1 - std::pow(t, 1.6)) - 0.6; if (t > 0.72) top = 10 - (t - 0.72) * 9; return p.y <= top && p.y >= bot && p.z > 1.2 && p.z < 4.8; },
      Col(ZCF { return (y < 7 && x < 40) ? 0xE0E4E8 : (y == 8 && x < 36) ? 0x80848A : 0xB8BCC4; }));
    c.box(15, 3, 0, 17, 12, 5, 0x3A3C40);
    c.box(3, 5, 1, 14, 9, 4, Col(ZCF { return (x % 3 == 0) ? 0x141414 : 0x26272A; }));
    c.box(1, 4, 0, 2, 10, 5, 0x55575C); c.box(5, 8, 5, 5, 8, 5, 0x55575C);
    return fin(out, c, 0.92f);
  }
  return false;
}

// ------------------------------------------------------------------ tools, repair kits, money, parts
bool tools(const std::string& id, VoxelModel& out) {
  if (id == "toolkit_basic" || id == "toolkit_adv" || id == "toolkit_expert") {
    const int B = id == "toolkit_basic" ? 0xB02A24 : id == "toolkit_adv" ? 0x4A5A6A : 0x26282A; const bool ex = id == "toolkit_expert";
    Canvas c(46, 34, 20);
    c.box(2, 0, 2, 41, 18, 15, Col(ZCF { if (y == 11) return shadeC(B, 0.55); if (ex && y >= 16 && y <= 17) return 0xE0B020; if (x < 5 || x > 38 || y < 2) return shadeC(B, 0.78); return noisy(B, 0.08, 4).at(x, y, z); }));
    c.rod(vec3(13, 19, 8), vec3(13, 25, 8), 1.4f, 1.4f, 0xA8ACB2); c.rod(vec3(30, 19, 8), vec3(30, 25, 8), 1.4f, 1.4f, 0xA8ACB2); c.rod(vec3(13, 25, 8), vec3(30, 25, 8), 1.4f, 1.4f, 0xA8ACB2);
    c.box(8, 7, 16, 11, 14, 16, 0xB8BCC4); c.box(32, 7, 16, 35, 14, 16, 0xB8BCC4);
    c.box(16, 4, 16, 27, 9, 16, 0xD8D4C0);
    c.rod(vec3(18, 6, 17), vec3(25, 8, 17), 0.9f, 0.9f, 0x3A3A3A);   // a wrench on the plate
    return fin(out, c, 0.88f);
  }
  if (id == "repair_weapon") {
    Canvas c(36, 24, 18); const int B = 0x56603E;
    c.box(2, 0, 2, 31, 15, 11, Col(ZCF { return (y == 9) ? shadeC(B, 0.6) : (x < 4 || x > 29) ? shadeC(B, 0.8) : noisy(B, 0.08, 3).at(x, y, z); }));
    c.rod(vec3(12, 21, 4), vec3(12, 24, 4), 1.2f, 1.2f, 0x2A2C30); c.rod(vec3(22, 21, 4), vec3(22, 24, 4), 1.2f, 1.2f, 0x2A2C30); c.rod(vec3(12, 24, 4), vec3(22, 24, 4), 1.2f, 1.2f, 0x2A2C30);
    c.box(15, 16, 3, 19, 16, 8, 0x2A2C30);
    c.box(7, 8, 12, 24, 10, 12, 0xE0E0C8); c.box(7, 4, 12, 10, 8, 12, 0xE0E0C8); c.box(15, 5, 12, 17, 7, 12, 0xE0E0C8);   // a pistol pictogram
    c.box(12, 3, 12, 13, 4, 12, 0xD8A020);
    return fin(out, c, 0.85f);
  }
  if (id == "repair_armor") {
    Canvas c(36, 28, 14); const int B = 0x8A7A52;
    c.box(2, 0, 0, 31, 18, 8, Col(ZCF { return (x % 3 == 0 && (y == 1 || y == 17)) ? shadeC(B, 0.6) : noisy(B, 0.12, 4).at(x, y, z); }));
    c.box(2, 14, 8, 31, 22, 9, Col(ZCF { return (y == 14 || x == 2 || x == 31) ? shadeC(B, 0.62) : noisy(shadeC(B, 0.9), 0.1, 6).at(x, y, z); }));
    c.box(14, 6, 9, 18, 23, 10, 0x3A2A1C); c.box(13, 6, 10, 19, 8, 10, 0xB8BCC4);
    c.cylZ(16, 13, 2.5f, 2.5f, 10, 11, 0x55575C);
    c.box(5, 4, 9, 11, 9, 9, 0x4A6A3A); c.box(7, 5, 10, 9, 8, 10, 0xE0E0C8); c.box(6, 6, 10, 10, 7, 10, 0xE0E0C8);   // a green patch with a cross
    c.rod(vec3(23, 5, 9), vec3(29, 11, 9), 0.6f, 0.6f, 0xD8D8D0);                                                    // a needle
    return fin(out, c, 0.82f);
  }
  if (id == "rubles") {
    Canvas c(40, 18, 20);
    for (int i = 0; i < 4; i++) {
      const int off = (i % 2) * 1, base = i % 2 ? 0xB8C4A0 : 0xC4CCAA;
      c.box(3 + off, i * 3, 3 + (i == 2), 34 + off, i * 3 + 2, 14 + (i == 2), Col(ZCF {
        if (y % 3 == 2) { // the top face of a note
          if (x < 5 + off || x > 32 + off || z < 5 || z > 12) return shadeC(base, 0.78);
          double d = std::sqrt((x - 19.0) * (x - 19.0) / 90 + (z - 9.0) * (z - 9.0) / 9);
          return d < 0.5 ? 0xA8B890 : ((x * 3 + z) % 5 == 0 ? shadeC(base, 0.88) : base); }
        return shadeC(base, 0.8 + 0.1 * (y % 2)); }));
    }
    c.box(17, 0, 2, 21, 11, 15, 0xC03030); c.box(18, 11, 2, 20, 11, 15, 0xD05050);   // the paper band
    return fin(out, c, 0.8f);
  }
  return false;
}
bool part(const std::string& id, VoxelModel& out) {
  if (id == "scrap_metal") {
    Canvas c(38, 26, 26);
    Col rust = Col(ZCF { double n = hash3(x, y, z, 4); return n < 0.22 ? 0x9A5A32 : n < 0.34 ? 0x6E6E6A : 0x7A7E86; });
    c.box(2, 0, 3, 22, 2, 18, rust);
    c.rod(vec3(6, 3, 6), vec3(28, 11, 10), 2.4f, 2.4f, Col(ZCF { return hash3(x, y, z, 5) < 0.3 ? 0x8A5A3A : 0x5E6268; }));
    c.box(14, 3, 8, 26, 7, 20, noisy(0x5E6268, 0.3, 7));
    c.rod(vec3(4, 3, 20), vec3(16, 15, 22), 1.3f, 1.3f, 0x8A5A3A);
    c.box(22, 8, 3, 24, 18, 5, 0x7A7E86); c.box(24, 16, 3, 32, 18, 5, 0x7A7E86);
    c.cylZ(9, 7, 2.2f, 2.2f, 19, 21, 0x55575C); c.box(30, 0, 14, 34, 3, 17, 0x8A5A3A);
    return fin(out, c, 0.75f);
  }
  if (id == "electronics") {
    Canvas c(36, 12, 28);
    c.box(1, 0, 1, 30, 1, 23, Col(ZCF { if ((x % 5 == 0 || z % 7 == 0) && hash3(x, 0, z, 3) < 0.55) return 0xC8A040; return 0x2E7A3E; }));
    c.box(5, 2, 4, 11, 3, 10, 0x1E1E22); c.box(15, 2, 12, 20, 2, 16, 0x1E1E22); c.box(23, 2, 3, 26, 2, 8, 0x1E1E22);
    for (int i = 4; i <= 11; i += 2) { c.box(i, 2, 3, i, 2, 3, 0xD8D0A0); c.box(i, 2, 11, i, 2, 11, 0xD8D0A0); }
    c.cylY(24, 16, 1.8f, 1.8f, 2, 6, 0x2A4A8A); c.cylY(27, 18, 1.8f, 1.8f, 2, 6, 0x2A4A8A); c.cylY(24.5f, 16, 0.6f, 0.6f, 6, 6, 0xC0C0C0);
    c.box(8, 2, 14, 12, 3, 15, 0xD8802A); c.box(1, 2, 19, 7, 4, 22, 0xB8BCC4); c.box(3, 2, 19, 4, 3, 20, 0x1E1E22);
    return fin(out, c, 0.78f);
  }
  if (id == "cloth") {
    Canvas c(36, 18, 28); const int cc[3] = {0xC8B890, 0xA8AA84, 0xC8B890};
    for (int i = 0; i < 3; i++) { const int col = cc[i]; c.box(2 + i, i * 4, 2, 31 - i, i * 4 + 3, 22 + i, Col(ZCF { return (z < 5 || (y % 4 == 3 && x % 5 > 1)) ? shadeC(col, 0.82) : noisy(col, 0.1, 2).at(x, y, z); })); }
    c.box(10, 12, 6, 12, 13, 18, 0x6A4A32);
    return fin(out, c, 0.75f);
  }
  if (id == "springs") {
    Canvas c(48, 18, 18); vec3 prev(2, 9, 9 + 6);
    for (int i = 1; i <= 90; i++) { double a = i * 0.30, x = 2 + i * 0.5; vec3 p(x, 9 + 6 * std::sin(a), 9 + 6 * std::cos(a)); c.rod(prev, p, 1.15f, 1.15f, (i / 10) % 2 ? Col(0xA8ACB2) : Col(0x8A8E94)); prev = p; }
    c.rod(vec3(1, 9, 15), vec3(0, 9, 12), 1.1f, 1.1f, 0xA8ACB2);
    return fin(out, c, 0.85f);
  }
  if (id == "wires") {
    Canvas c(34, 34, 12);
    for (int k = 0; k < 4; k++) { const int col = k == 0 ? 0xB02828 : k == 1 ? 0x2A4AA0 : k == 2 ? 0xC87A3A : 0xB02828; vec3 prev; for (int i = 0; i <= 28; i++) { double a = i * 0.2244; double R = 10.5 + (k % 2) * 1.2; vec3 p(17 + R * std::cos(a), 17 + R * std::sin(a), 3 + k * 1.8f); if (i) c.rod(prev, p, 1.2f, 1.2f, col); prev = p; } }
    c.rod(vec3(26, 12, 5), vec3(33, 3, 7), 1.2f, 1.2f, 0xB02828); c.rod(vec3(33, 3, 7), vec3(34, 1, 8), 0.6f, 0.6f, 0xC87A3A);
    c.rod(vec3(7, 14, 8), vec3(1, 4, 6), 1.2f, 1.2f, 0x2A4AA0); c.rod(vec3(1, 4, 6), vec3(0, 2, 5), 0.6f, 0.6f, 0xC87A3A);
    c.box(15, 26, 2, 19, 28, 10, 0x1E1E20);   // the tie
    return fin(out, c, 0.78f);
  }
  if (id == "lead_sheet") {
    Canvas c(40, 22, 26);
    c.box(2, 0, 3, 28, 1, 22, Col(ZCF { double n = hash3(x, y, z, 8); return n < 0.1 ? 0x80848C : n < 0.2 ? 0x5A5E66 : 0x6A6E78; }));
    c.cylZ(32, 5, 4.2f, 4.2f, 3, 22, Col(ZCF { double r = std::sqrt((x - 32.0) * (x - 32.0) + (y - 5.0) * (y - 5.0)); return ((int)r % 2) ? 0x5A5E66 : 0x757982; }));
    c.cylZ(32, 5, 1.6f, 1.6f, 3, 22, 0x2A2C30);
    c.box(10, 2, 8, 14, 2, 9, 0x8A8E96);
    return fin(out, c, 0.82f);
  }
  if (id == "flash_drive") {
    Canvas c(14, 36, 8);
    c.box(2, 0, 0, 10, 20, 4, Col(ZCF { return (y > 4 && y < 14 && z == 4) ? 0x4A5ACA : 0x2A2E5A; }));
    c.box(3, 21, 1, 9, 31, 3, 0xC0C4C8); c.box(4, 23, 2, 5, 24, 3, 0x1E1E22); c.box(7, 23, 2, 8, 24, 3, 0x1E1E22);
    c.ringZ(6, 17, 2.2f, 1.1f, 0, 4, -1); c.box(5, 2, 4, 6, 3, 4, 0x40FF60 | GLOW);
    return fin(out, c, 0.72f);
  }
  if (id == "documents") {
    Canvas c(36, 34, 10); const int T = 0xC8A868;
    c.box(1, 0, 0, 28, 30, 1, T); c.box(3, 31, 0, 11, 33, 1, T);
    c.box(2, 1, 2, 26, 28, 3, Col(ZCF { return (y % 3 == 0 && x > 4 && x < 23 && y < 25) ? 0x4A4A4A : 0xEEEAD8; }));
    c.box(1, 0, 4, 28, 21, 5, shadeC(T, 1.05)); c.box(3, 22, 4, 11, 24, 5, shadeC(T, 1.05));
    c.box(5, 11, 6, 22, 13, 6, 0xB02828); c.box(5, 9, 6, 6, 15, 6, 0xB02828); c.box(21, 9, 6, 22, 15, 6, 0xB02828);
    c.rod(vec3(24, 24, 6), vec3(24, 30, 6), 0.7f, 0.7f, 0xB8BCC4);
    return fin(out, c, 0.82f);
  }
  if (id == "gun_oil") {
    Canvas c(30, 38, 16);
    c.cylY(8, 8, 6, 6, 0, 14, Col(ZCF { if (z > 8 && y >= 4 && y <= 11) return (y == 7 && x % 2) ? 0xF0F0E8 : 0x1E1E20; return y > 11 ? 0xC02020 : 0xD8B020; }));
    c.coneY(8, 8, 6, 2.4f, 15, 18, 0xC02020); c.cylY(8, 8, 2.4f, 2.4f, 19, 20, 0x2A2C30);
    c.rod(vec3(8, 20, 8), vec3(20, 29, 8), 1.4f, 1.0f, 0xB8BCC0); c.rod(vec3(20, 29, 8), vec3(26, 30, 8), 1.0f, 0.5f, 0xB8BCC0);
    return fin(out, c, 0.78f, false);
  }
  return false;
}

// ------------------------------------------------------------------ mutant trophies
bool trophy(const std::string& id, VoxelModel& out) {
  auto chain = [&](Canvas& c, std::vector<vec3> P, float r0, float r1, const Col& col) {
    for (size_t i = 0; i + 1 < P.size(); i++) { float t0 = (float)i / (P.size() - 1), t1 = (float)(i + 1) / (P.size() - 1); c.rod(P[i], P[i + 1], r0 + (r1 - r0) * t0, r0 + (r1 - r0) * t1, col); }
  };
  if (id == "dog_tail") {
    Canvas c(42, 26, 14);
    chain(c, {{3, 6, 7}, {12, 4, 7}, {22, 8, 7}, {32, 14, 7}, {38, 19, 7}}, 4.2f, 1.2f, Col(ZCF { double n = hash3(x, y, z, 4); if (x < 5) return 0xA83A3A; if (x > 31) return shadeC(0xB8A88A, 0.8 + 0.3 * n); return shadeC(n < 0.12 ? 0x2E2620 : 0x6A5A48, 0.85 + 0.3 * n); }));
    return fin(out, c, 0.85f);
  }
  if (id == "lurcher_tail") {
    Canvas c(48, 28, 16); std::vector<vec3> P = {{3, 7, 8}, {13, 5, 8}, {24, 9, 8}, {34, 15, 8}, {41, 20, 8}};
    chain(c, P, 5.f, 1.6f, Col(ZCF { double n = hash3(x, y, z, 7); return x < 5 ? 0xA83A3A : shadeC(y > 12 ? 0x6A5E7C : 0x4A4058, 0.85 + 0.3 * n); }));
    for (size_t i = 1; i + 1 < P.size(); i++) c.rod(P[i] + vec3(0, 3, 0), P[i] + vec3(1, 9 - (float)i, 0), 1.3f, 0.3f, 0xCCC4D8);
    c.ell(44, 22, 8, 3.2f, 3.2f, 3.2f, 0x8A7AA0);
    return fin(out, c, 0.9f);
  }
  if (id == "boar_hoof") {
    Canvas c(26, 32, 20);
    c.cylY(13, 10, 6.5f, 6, 15, 25, Col(ZCF { double n = hash3(x, y, z, 6); return y >= 25 ? 0xA03030 : shadeC(0x5A4430, 0.8 + 0.4 * n); }));
    c.rod(vec3(8.5f, 14, 10), vec3(7.5f, 2, 12), 4.8f, 3.4f, 0x2A2420); c.rod(vec3(17.5f, 14, 10), vec3(18.5f, 2, 12), 4.8f, 3.4f, 0x2A2420);
    c.ell(13, 15, 10, 8, 2.5f, 6, 0x3A2E26); c.ell(13, 11, 4, 2.2f, 2.2f, 1.6f, 0x2A2420);
    return fin(out, c, 0.7f);
  }
  if (id == "hog_eye") {
    Canvas c(28, 30, 28);
    c.rod(vec3(14, 15, 5), vec3(17, 11, 0), 2.4f, 1.8f, 0xB8504A);
    c.ell(14, 15, 14, 9.5f, 9.5f, 9.5f, Col(ZCF { return hash3(x, y, z, 9) < 0.07 ? 0xB03030 : shadeC(0xE8DCA8, 0.9 + 0.15 * hash3(x, y, z, 2)); }));
    c.cylZ(14, 15, 5, 5, 22, 23, 0xD88820); c.cylZ(14, 15, 2.2f, 2.2f, 22, 24, 0x141414); c.box(12, 17, 24, 12, 17, 24, 0xFFFFFF);
    return fin(out, c, 0.6f);
  }
  if (id == "shroud_tentacle") {
    Canvas c(32, 48, 20); std::vector<vec3> P = {{7, 3, 10}, {11, 13, 10}, {19, 21, 10}, {23, 30, 10}, {19, 38, 10}, {14, 43, 10}};
    chain(c, P, 5.5f, 1.2f, Col(ZCF { double n = hash3(x, y, z, 3); return y < 5 ? 0xA83A3A : shadeC(0x2E4A3A, 0.8 + 0.4 * n); }));
    for (size_t i = 1; i + 1 < P.size(); i++) for (int s = 0; s < 2; s++) { vec3 m = (P[i] * (1 - s * 0.5f) + P[i + 1] * (s * 0.5f)); c.ell(m.x - 4.2f + i * 0.2f, m.y, m.z + 1.5f, 1.6f, 1.6f, 1.2f, 0xA8C8A0); }
    return fin(out, c, 0.9f);
  }
  if (id == "crawler_foot") {
    Canvas c(42, 36, 14); const int K = 0x5A3E2A;
    c.ell(5, 5, 7, 5, 4.5f, 4.5f, shadeC(K, 0.8));
    c.rod(vec3(6, 6, 7), vec3(18, 25, 7), 3.6f, 2.8f, Col(ZCF { return hash3(x, y, z, 3) < 0.15 ? shadeC(K, 0.7) : K; })); c.ell(18, 25, 7, 3.2f, 3.2f, 3.2f, shadeC(K, 0.7));
    c.rod(vec3(18, 25, 7), vec3(31, 13, 7), 2.7f, 2.0f, K); c.ell(31, 13, 7, 2.5f, 2.5f, 2.5f, shadeC(K, 0.7));
    c.rod(vec3(31, 13, 7), vec3(39, 2, 7), 1.8f, 0.5f, 0xD8C8A0);
    for (int i = 0; i < 4; i++) { float t = 0.2f + i * 0.2f; vec3 b = vec3(6, 6, 7) * (1 - t) + vec3(18, 25, 7) * t; c.rod(b, b + vec3(-2, 3.5f, 0), 0.9f, 0.2f, 0x8A6A4A); }
    return fin(out, c, 0.9f);
  }
  if (id == "puppeteer_hand") {
    Canvas c(34, 44, 12); const Col skin = Col(ZCF { double n = hash3(x, y, z, 4); if (y < 2) return 0x8A3A4A; if (((x * 5 + y * 3) % 13 == 0) && n < 0.7) return 0x6A5A8A; return shadeC(0xB8A8B8, 0.88 + 0.2 * n); });
    c.box(11, 0, 3, 21, 8, 8, skin); c.box(8, 8, 3, 24, 20, 8, skin);
    const float fx[4] = {10, 14.5f, 19, 23}, fy[4] = {20, 21, 21, 20}, L[4] = {15, 18, 17, 13};
    for (int i = 0; i < 4; i++) { vec3 a(fx[i], fy[i], 5.5f), b(fx[i] + (i - 1.5f) * 0.9f, fy[i] + L[i], 6.5f - i * 0.3f); c.rod(a, b, 2.3f, 1.6f, skin); c.rod(b - vec3(0, 1.2f, 0), b, 1.7f, 1.3f, 0x3A2E3A); }
    c.rod(vec3(8, 10, 5.5f), vec3(1.5f, 18, 6), 2.8f, 1.8f, skin); c.rod(vec3(1.5f, 17, 6), vec3(1, 20, 6), 1.8f, 1.4f, 0x3A2E3A);
    return fin(out, c, 0.9f);
  }
  return false;
}

// ------------------------------------------------------------------ artifacts (families: gravity, thermal, electric, chemical, psy)
bool artifact(const std::string& id, VoxelModel& out) {
  if (id == "moth_wing") {
    Canvas c(38, 32, 8);
    c.fill(vec3(0, 0, 3), vec3(38, 32, 5), [](vec3 p) { auto e = [&](double cx, double cy, double rx, double ry, double a) { double dx = p.x - cx, dy = p.y - cy, ca = std::cos(a), sa = std::sin(a); double u = dx * ca + dy * sa, v = -dx * sa + dy * ca; return u * u / (rx * rx) + v * v / (ry * ry) <= 1; };
      return e(20, 17, 15, 8, 0.55) || e(16, 9, 11, 5.5, -0.2); }, Col(ZCF { return (x * 3 + y * 5) % 9 == 0 ? 0x8AB0E0 | GLOW : (hash3(x, y, z, 2) < 0.3 ? 0xE8F2FF : 0xC8DCF0); }));
    for (vec3 t : {vec3(34, 26, 3.5f), vec3(35, 19, 3.5f), vec3(30, 7, 3.5f), vec3(27, 4, 3.5f), vec3(23, 29, 3.5f)}) c.rod(vec3(5, 9, 3.5f), t, 0.7f, 0.5f, 0x5A7AA8);
    c.rod(vec3(3, 5, 4), vec3(8, 11, 4), 2.2f, 1.6f, 0x4A5A70);
    return fin(out, c, 0.9f);
  }
  if (id == "gravel_heart") {
    Canvas c(32, 32, 22);
    Col stone = Col(ZCF { if ((x + y * 2 + z * 3) % 13 == 0) return 0xA8D0FF | GLOW; return shadeC(0x7A7A82, 0.8 + 0.4 * hash3(x, y, z, 5)); });
    c.ell(10, 20, 10, 8.5f, 8.5f, 8, stone); c.ell(22, 20, 10, 8.5f, 8.5f, 8, stone);
    c.rod(vec3(7, 16, 10), vec3(16, 3, 10), 8.5f, 1.8f, stone); c.rod(vec3(25, 16, 10), vec3(16, 3, 10), 8.5f, 1.8f, stone);
    c.ell(16, 16, 15, 3, 3, 2, 0xE8F4FF | GLOW);
    for (int i = 0; i < 5; i++) c.ell(4 + i * 6, 12 + (i % 2) * 14, 4 + (i % 3) * 8, 2, 2, 2, shadeC(0x8A8A92, 0.9));
    return fin(out, c, 0.78f);
  }
  if (id == "hollow_star") {
    Canvas c(36, 36, 8);
    for (int k = 0; k < 5; k++) { double a = 1.5708 + k * 1.2566; c.rod(vec3(18, 18, 4), vec3(18 + 16 * std::cos(a), 18 + 16 * std::sin(a), 4), 3.4f, 0.7f, Col(ZCF { return (x + y) % 5 == 0 ? 0xE8F4FF : 0xBFD8F0; })); }
    c.cylZ(18, 18, 7, 7, 1, 6, 0xBFD8F0); c.cylZ(18, 18, 3.6f, 3.6f, 0, 7, -1); c.ringZ(18, 18, 4.6f, 3.6f, 2, 5, 0xA8D0FF | GLOW);
    return fin(out, c, 0.9f);
  }
  if (id == "ember_knot") {
    Canvas c(36, 36, 20); vec3 prev;
    for (int i = 0; i <= 80; i++) { double t = i * 6.28318 / 80; vec3 p(18 + 4.3 * (std::sin(t) + 2 * std::sin(2 * t)), 18 + 4.3 * (std::cos(t) - 2 * std::cos(2 * t)), 10 - 3.4 * std::sin(3 * t)); if (i) c.rod(prev, p, 2.5f, 2.5f, Col(ZCF { double n = hash3(x, y, z, 3); return n < 0.22 ? (0xFF7A20 | GLOW) : n < 0.3 ? (0xFFC040 | GLOW) : shadeC(0x2A1A14, 0.8 + 0.5 * n); })); prev = p; }
    return fin(out, c, 0.8f);
  }
  if (id == "night_candle") {
    Canvas c(20, 36, 20);
    c.cylY(10, 10, 6, 6, 0, 2, 0x2A1E1A);
    c.cylY(10, 10, 4.6f, 4.6f, 3, 20, Col(ZCF { return (((x * 7 + z * 5) % 9) == 0 && y > 12) ? (0xC85A1A | GLOW) : shadeC(0x3A2A24, 0.8 + 0.4 * hash3(x, y, z, 2)); }));
    c.cylY(10, 10, 5.4f, 5.4f, 19, 20, 0xFF7A20 | GLOW);
    for (int i = 0; i < 3; i++) c.rod(vec3(10 + 4.5f * std::cos(i * 2.1), 19, 10 + 4.5f * std::sin(i * 2.1)), vec3(10 + 4.8f * std::cos(i * 2.1), 12 + i * 2, 10 + 4.8f * std::sin(i * 2.1)), 0.9f, 0.7f, 0xC85A1A | GLOW);
    c.coneY(10, 10, 3, 0.2f, 21, 33, 0xFFB030 | GLOW); c.coneY(10, 10, 1.5f, 0.1f, 21, 28, 0xFFF0A0 | GLOW);
    return fin(out, c, 0.82f, true);
  }
  if (id == "static_pearl") {
    Canvas c(32, 32, 32);
    c.ell(16, 16, 16, 8.5f, 8.5f, 8.5f, Col(ZCF { double h = hash3(x >> 1, y >> 1, z >> 1, 2); return h < 0.3 ? 0xE8F8FF : h < 0.6 ? 0xD0F0F8 : 0xF0E8FF; }));
    for (int k = 0; k < 6; k++) { double a = k * 1.047 + 0.3, b = (k % 2 ? 0.6 : -0.5); vec3 d(std::cos(a) * std::cos(b), std::sin(b), std::sin(a) * std::cos(b)); vec3 p = vec3(16) + d * 9.f; for (int s = 0; s < 3; s++) { vec3 n = p + d * 2.4f + vec3((s % 2 ? 1.f : -1.f) * 1.2f, 0.9f, 0.5f * (s - 1)); c.rod(p, n, 0.7f, 0.5f, 0x80F0FF | GLOW); p = n; } }
    return fin(out, c, 0.72f);
  }
  if (id == "spindle") {
    Canvas c(20, 48, 20);
    c.coneY(10, 10, 0.5f, 6.5f, 0, 22, Col(ZCF { return 0x3A4A5A; })); c.coneY(10, 10, 6.5f, 0.5f, 23, 45, 0x3A4A5A);
    for (int y = 4; y <= 41; y += 5) { double t = y < 23 ? y / 22.0 : (45 - y) / 22.0; c.cylY(10, 10, 0.6f + 5.9f * t + 0.8f, 0.6f + 5.9f * t + 0.8f, y, y, 0x5A7A9A); }
    c.rod(vec3(10, 2, 10), vec3(10, 44, 10), 1.3f, 1.3f, 0x90F4FF | GLOW);
    return fin(out, c, 0.9f, true);
  }
  if (id == "rust_shell") {
    Canvas c(42, 42, 18); vec3 prev; const double tm = 2.2 * 6.28318;
    for (int i = 0; i <= 110; i++) { double t = i * tm / 110, a = 3 + t / 6.28318 * 6.6; vec3 p(21 + a * std::cos(t), 21 + a * std::sin(t), 9); float r = 1.2f + 2.0f * (float)(t / tm);
      if (i) c.rod(prev, p, r, r + 0.05f, Col(ZCF { return (i / 7) % 2 ? 0x8A4A2A : (hash3(x, y, z, 3) < 0.4 ? 0x7ADA3A : 0x9A5A32); }));
      prev = p; }
    c.ell(21, 21, 9, 2.2f, 2.2f, 2.6f, 0x8AE03A | GLOW);
    return fin(out, c, 0.82f);
  }
  if (id == "blood_coral") {
    Canvas c(34, 38, 22);
    c.cylY(16, 11, 7, 6, 0, 2, 0x5A2018);
    auto br = [&](vec3 a, vec3 b, float r0, float r1) { c.rod(a, b, r0, r1, Col(ZCF { return shadeC(0xA82A2A, 0.8 + 0.4 * hash3(x, y, z, 5)); })); c.ell(b.x, b.y, b.z, r1 + 0.6f, r1 + 0.6f, r1 + 0.6f, 0x7ADA3A | GLOW); };
    c.rod(vec3(16, 2, 11), vec3(16, 14, 11), 4.f, 3.f, 0x8A2420);
    br(vec3(16, 14, 11), vec3(8, 25, 9), 3.f, 1.4f); br(vec3(16, 14, 11), vec3(25, 27, 13), 3.f, 1.4f); br(vec3(16, 14, 11), vec3(16, 33, 11), 2.8f, 1.5f);
    br(vec3(10, 21, 10), vec3(3, 24, 14), 1.8f, 1.1f); br(vec3(22, 22, 12), vec3(29, 17, 8), 1.9f, 1.1f); br(vec3(16, 25, 11), vec3(11, 32, 14), 1.8f, 1.1f);
    return fin(out, c, 0.85f);
  }
  if (id == "iron_moss") {
    Canvas c(32, 26, 28);
    Col blob = Col(ZCF { double n = hash3(x, y, z, 6); return (y > 9 + (int)(n * 6)) ? shadeC(0x5A8A2A, 0.8 + 0.5 * n) : shadeC(0x7A4A32, 0.8 + 0.4 * n); });
    c.ell(14, 10, 13, 11, 8, 10, blob); c.ell(22, 9, 15, 7, 6, 8, blob); c.ell(9, 11, 18, 6, 6, 6, blob);
    c.cylZ(12, 9, 2.4f, 2.4f, 0, 28, -1); c.cylZ(22, 11, 2.f, 2.f, 0, 28, -1); c.cylX(8, 14, 2.f, 2.f, 0, 32, -1);
    return fin(out, c, 0.78f);
  }
  if (id == "glass_eye") {
    Canvas c(28, 28, 28);
    c.ell(14, 14, 14, 10.5f, 10.5f, 10.5f, Col(ZCF { return hash3(x, y, z, 4) < 0.1 ? 0xC8A0F0 : 0x8A50D0; }));
    c.cylZ(14, 14, 5.5f, 5.5f, 21, 23, 0xE0C0FF | GLOW); c.cylZ(14, 14, 2.4f, 2.4f, 21, 25, 0x1A0A2A);
    c.box(10, 19, 24, 11, 20, 24, 0xFFFFFF); c.box(8, 18, 22, 8, 19, 22, 0xFFFFFF);
    return fin(out, c, 0.62f);
  }
  if (id == "mothers_tear") {
    Canvas c(24, 36, 24);
    c.ell(12, 10, 12, 8.5f, 8.5f, 8.5f, 0x9A60E0); c.coneY(12, 12, 6.5f, 0.3f, 12, 32, 0x9A60E0);
    c.ell(12, 10, 12, 4.2f, 4.2f, 4.2f, 0xC8A0F8 | GLOW); c.coneY(12, 12, 2.f, 0.2f, 12, 24, 0xD8B8FF | GLOW);
    c.box(7, 14, 20, 8, 17, 20, 0xFFFFFF);
    return fin(out, c, 0.75f, true);
  }
  return false;
}

// ------------------------------------------------------------------ suits and helmets
struct Suit { int main, trim, accent; bool hood = false, plate = false, pauldron = false, robe = false, frame = false, camo = false, visor = false, rig = false; };
bool suit(const std::string& id, VoxelModel& out) {
  Suit S;
  if (id == "jacket_leather") S = {0x5A3A22, 0x3A2414, 0x9A9EA0};
  else if (id == "suit_bandit") { S = {0x2A2622, 0x16130F, 0x9A3A22}; S.hood = true; }
  else if (id == "suit_drifter") { S = {0x5A6038, 0x3A4024, 0xC8A040}; S.hood = true; }
  else if (id == "suit_army") { S = {0x4A5A38, 0x2E3A24, 0x8A7A58}; S.camo = true; S.rig = true; S.pauldron = true; }
  else if (id == "suit_merc") { S = {0x2E3034, 0x1A1B1E, 0xE08A20}; S.plate = true; S.pauldron = true; S.rig = true; }
  else if (id == "suit_opensky") { S = {0xC8D0E0, 0x8A98B0, 0x3A6AB0}; S.rig = true; }
  else if (id == "suit_bastion") { S = {0x585C62, 0x3A3D42, 0xB8902A}; S.plate = true; S.pauldron = true; }
  else if (id == "suit_choir") { S = {0x3A1E26, 0x24121A, 0xC8A040}; S.robe = true; S.hood = true; }
  else if (id == "suit_hazmat") { S = {0xD8C030, 0xA88A20, 0xD87A20}; S.hood = true; S.visor = true; }
  else if (id == "suit_sealed") { S = {0x4E6A3A, 0x2E4224, 0x8AA0A0}; S.hood = true; S.visor = true; S.rig = true; }
  else if (id == "suit_assault") { S = {0x3A3E3A, 0x22252A, 0x6A7A58}; S.plate = true; S.pauldron = true; S.rig = true; }
  else if (id == "suit_exo") { S = {0x3A3E44, 0x24272B, 0x9AA0A8}; S.frame = true; S.plate = true; }
  else return false;
  Canvas c(52, 50, 16);
  const int M = S.main, T = S.trim, A = S.accent; const bool camo = S.camo, robe = S.robe;
  Col body = Col(ZCF { if (camo) { double n = hash3(x >> 1, y >> 1, z, 7); return n < 0.3 ? 0x3A4A2E : n < 0.55 ? 0x6A6A4A : M; } return noisy(M, 0.12, 4).at(x, y, z); });
  const int yb = robe ? 0 : 8;
  c.box(15, yb, 3, 36, 36, 10, Col(ZCF { return (y < yb + 2) ? T : (y >= 17 && y <= 19 && !robe) ? shadeC(T, 1.2) : body.at(x, y, z); }));   // torso
  if (robe) { c.box(12, 0, 3, 39, 8, 10, Col(ZCF { return (y < 2 || x < 14 || x > 37) ? A : shadeC(M, 0.85); })); }
  c.box(14, 34, 3, 37, 38, 10, shadeC(M, 1.1));                                                    // shoulders
  // sleeves: down and out
  c.rod(vec3(16, 34, 6.5f), vec3(5, 14, 6.5f), 4.6f, 4.0f, body); c.rod(vec3(35, 34, 6.5f), vec3(46, 14, 6.5f), 4.6f, 4.0f, body);
  c.rod(vec3(6, 15, 6.5f), vec3(4.6f, 11, 6.5f), 4.2f, 4.2f, T); c.rod(vec3(45, 15, 6.5f), vec3(46.4f, 11, 6.5f), 4.2f, 4.2f, T);   // cuffs
  c.box(24, yb, 10, 27, 36, 10, 0x9A9EA0); c.box(25, 30, 11, 26, 33, 11, 0xC8CCD0);               // zip and pull
  c.box(17, 22, 10, 22, 27, 11, T); c.box(29, 22, 10, 34, 27, 11, T);                               // chest pockets
  if (!robe) { c.box(17, 10, 10, 22, 15, 11, T); c.box(29, 10, 10, 34, 15, 11, T); }
  if (S.hood) { c.ell(25.5f, 38, 6, 9.5f, 7.5f, 5.5f, shadeC(M, 0.95)); c.cylY(25.5f, 8, 6.5f, 3.5f, 33, 38, shadeC(M, 0.75)); c.ell(25.5f, 36, 9, 6, 4.5f, 2.5f, S.visor ? Col(0x1A2A32 | GLOW) : Col(shadeC(M, 0.5))); }
  else { c.box(19, 36, 4, 32, 40, 9, shadeC(M, 0.8)); c.box(21, 36, 9, 30, 38, 9, T); }          // the collar
  if (S.plate) { c.box(18, 18, 11, 33, 34, 12, Col(ZCF { return (x == 18 || x == 33 || y == 18 || y == 34) ? shadeC(A, 0.7) : (x % 6 == 0 && y % 6 == 0) ? A : shadeC(M, 1.25); })); }
  if (S.rig) { for (int i = 0; i < 3; i++) c.box(19 + i * 5, 12, 12, 22 + i * 5, 17, 13, shadeC(A, 0.9)); c.box(15, 17, 11, 36, 18, 11, T); }
  if (S.pauldron) { c.ell(11, 35, 6.5f, 7.5f, 4.5f, 6, shadeC(S.plate ? A : M, 0.95)); c.ell(40, 35, 6.5f, 7.5f, 4.5f, 6, shadeC(S.plate ? A : M, 0.95)); }
  if (S.frame) { for (int xx : {13, 38}) c.box(xx, 0, 11, xx + 1, 38, 11, A); c.rod(vec3(15, 33, 11), vec3(4, 12, 11), 1.1f, 1.1f, A); c.rod(vec3(36, 33, 11), vec3(47, 12, 11), 1.1f, 1.1f, A);
    for (vec3 j : {vec3(14, 34, 11), vec3(37, 34, 11), vec3(9, 22, 11), vec3(42, 22, 11), vec3(14, 8, 11), vec3(37, 8, 11)}) c.ell(j.x, j.y, j.z, 2.6f, 2.6f, 2.2f, 0x6A6E74);
    c.box(21, 24, 12, 30, 31, 13, 0x40E0FF | GLOW); c.box(20, 23, 12, 31, 23, 13, 0x26272B); c.box(20, 32, 12, 31, 32, 13, 0x26272B); }
  if (S.visor && !S.hood) c.box(20, 40, 4, 31, 40, 6, 0x1A2A32);
  if (id == "suit_hazmat") { c.cylZ(25.5f, 27, 3.5f, 3.5f, 11, 13, 0x4A4E54); c.box(15, 20, 11, 36, 21, 11, A); c.box(15, 28, 11, 36, 29, 11, A); }
  if (id == "suit_opensky") { c.box(5, 20, 8, 7, 21, 9, A); c.ell(25.5f, 29, 11, 3.2f, 3.2f, 1, A); }
  return fin(out, c, 0.92f);
}
bool helmet(const std::string& id, VoxelModel& out) {
  Canvas c(40, 44, 34);
  if (id == "gasmask") {
    c.ell(20, 22, 14, 12.5f, 15, 10.5f, Col(ZCF { return shadeC(0x2E3A30, 0.85 + 0.3 * hash3(x, y, z, 2)); }));
    for (int xx : {13, 27}) { c.cylZ(xx, 25, 4.8f, 4.8f, 20, 22, 0x4A5048); c.cylZ(xx, 25, 3.7f, 3.7f, 22, 23, 0x203830); c.box(xx - 2, 27, 24, xx - 1, 27, 24, 0x88C0B0); }
    c.cylZ(20, 11, 6.2f, 6.2f, 18, 28, Col(ZCF { return (z % 3 == 0) ? 0x2E3220 : 0x4A5236; })); c.cylZ(20, 11, 3.2f, 3.2f, 28, 29, 0x1E2018);
    c.rod(vec3(8, 28, 12), vec3(6, 20, 3), 1.4f, 1.4f, 0x1E1E20); c.rod(vec3(32, 28, 12), vec3(34, 20, 3), 1.4f, 1.4f, 0x1E1E20); c.rod(vec3(10, 34, 11), vec3(20, 38, 5), 1.4f, 1.4f, 0x1E1E20); c.rod(vec3(30, 34, 11), vec3(20, 38, 5), 1.4f, 1.4f, 0x1E1E20);
    return fin(out, c, 0.8f);
  }
  const bool sph = id == "helmet_sphere", psy = id == "helmet_psy";
  const int M = sph ? 0x4A4E54 : psy ? 0x5A4A6A : 0x3A4030;
  if (sph) c.ell(20, 22, 17, 15, 15, 15, noisy(M, 0.12, 4));
  else c.fill(vec3(2, 14, 2), vec3(38, 40, 32), [](vec3 p) { double a = (p.x - 20) / 17.0, b = (p.y - 16) / 22.0, d = (p.z - 17) / 15.0; return p.y >= 16 && a * a + b * b + d * d <= 1.0; }, noisy(M, 0.14, 4));
  if (!sph) { c.ell(20, 16, 17, 18.5f, 1.8f, 16, shadeC(M, 0.8)); }
  if (sph) { c.fill(vec3(6, 12, 20), vec3(34, 30, 33), [](vec3 p) { return p.z > 20 && std::abs(p.x - 20) < 11 && p.y > 15 && p.y < 28 && ((p.x - 20) * (p.x - 20) / 121 + (p.y - 22) * (p.y - 22) / 49) < 1.0; }, Col(ZCF { return (x + y) % 9 == 0 ? (0x6AA0C0 | GLOW) : 0x1A2A3A; }));
    c.ringZ(20, 22, 15, 14, 3, 4, 0x2E3034); c.cylY(20, 17, 11, 11, 6, 9, 0x2E3034); c.cylX(22, 17, 3, 3, 0, 2, 0x2E3034); c.cylX(22, 17, 3, 3, 38, 39, 0x2E3034); }
  else if (psy) {
    for (int k = 0; k < 6; k++) { vec3 prev; for (int i = 0; i <= 24; i++) { double t = i / 24.0, a = t * 6.28318 * 1.0 + k * 1.047; vec3 p(20 + 17.5 * std::cos(a) * std::sin(0.4 + t * 1.1), 17 + 24 * std::cos(0.4 + t * 1.1) * 0.75, 17 + 15.5 * std::sin(a) * std::sin(0.4 + t * 1.1)); if (i && p.y > 16) c.rod(prev, p, 0.9f, 0.9f, 0xC87A3A); prev = p; } }
    c.rod(vec3(20, 38, 17), vec3(20, 44, 17), 1.f, 0.6f, 0x9A9EA4); c.ell(20, 44, 17, 2.3f, 2.3f, 2.3f, 0xC080FF | GLOW);
    c.ell(20, 29, 31, 2.6f, 2.6f, 1.8f, 0xC080FF | GLOW);
  } else {
    c.box(16, 34, 30, 24, 37, 33, 0x1E1E22); c.box(2, 18, 15, 5, 26, 21, 0x26272B); c.box(35, 18, 15, 38, 26, 21, 0x26272B);   // NVG mount, ear cups
    c.box(18, 36, 5, 22, 38, 28, 0x55575C);                                                                                         // top rail
    c.rod(vec3(4, 18, 17), vec3(9, 8, 22), 0.9f, 0.9f, 0x1E1E20); c.rod(vec3(36, 18, 17), vec3(31, 8, 22), 0.9f, 0.9f, 0x1E1E20);   // chin strap
  }
  return fin(out, c, 0.82f);
}

} // namespace

bool buildZoneItemModel(const std::string& id, VoxelModel& out) {
  if (id.rfind("zg:", 0) == 0) { out.key = id; const bool ok = buildZoneGunItem(id, out); if (ok) out.built = true; return ok; }   // a gun with its attachments
  const zone::ZItem* z = zone::zi(id);
  if (!z) return false;
  out.key = id;
  const std::string& cat = z->cat;
  bool ok = false;
  if (z->isGun || cat == "attach") ok = buildZoneGunItem(id, out);
  else if (cat == "ammo") ok = ammo(id, z, out);
  else if (cat == "grenade") ok = grenade(id, out);
  else if (cat == "med") ok = med(id, out);
  else if (cat == "food") ok = food(id, out);
  else if (cat == "detector" || cat == "gear" || cat == "throw" || cat == "melee") ok = gear(id, out);
  else if (cat == "toolkit" || cat == "repair" || cat == "money") ok = tools(id, out);
  else if (cat == "part") ok = part(id, out);
  else if (cat == "trophy") ok = trophy(id, out);
  else if (cat == "artifact") ok = artifact(id, out);
  else if (cat == "suit") ok = suit(id, out);
  else if (cat == "helmet") ok = helmet(id, out);
  if (ok) { out.key = id; out.built = true; }
  return ok;
}

bool zoneDeviceScreen(const std::string& id, vec2& lo, vec2& hi, float& z) {
  // canvas (w, h, d), the size it was scaled to, the screen's voxel rect [x0, x1) x [y0, y1) and the face's z (top of the screen voxels)
  struct D { const char* id; int w, h, d; float size; int x0, x1, y0, y1, zf; };
  static const D T[] = {{"pda", 64, 36, 8, 0.85f, 10, 54, 6, 30, 7}, {"detector_echo", 24, 40, 8, 0.9f, 3, 21, 23, 36, 7},
                        {"detector_bear", 28, 44, 9, 0.9f, 3, 24, 17, 38, 8}, {"detector_veles", 28, 48, 9, 0.95f, 4, 24, 22, 42, 8}};
  for (const D& d : T) if (id == d.id) {
    const float k = d.size / (float)std::max(d.w, d.h);
    lo = vec2((d.x0 - d.w * 0.5f) * k, (d.y0 - d.h * 0.5f) * k); hi = vec2((d.x1 - d.w * 0.5f) * k, (d.y1 - d.h * 0.5f) * k);
    z = (d.zf - d.d * 0.5f) * k;
    return true;
  }
  return false;
}

} // namespace dq
