// expect: \[cppcoreguidelines-avoid-non-const-global-variables[],]
// Seeded violation (ICS-005): a mutable global. See CMakeLists.txt.
namespace ics::seeded {

int frames_seen = 0;

}  // namespace ics::seeded
