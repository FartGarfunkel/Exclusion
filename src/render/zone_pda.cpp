// render/zone_pda.cpp — (the held PDA is drawn by render/zone_fpgun.cpp with the rest of first person, which also
// owns zonePdaRect / zonePdaRectOn.) Kept so callers of registerZonePda() still link.
namespace dq {
void registerZonePda() {}
} // namespace dq
