#pragma once

namespace ids
{
// Object
inline constexpr const char* model    = "model";
inline constexpr const char* pitch    = "pitch";
inline constexpr const char* keyTrack = "keytrack";
inline constexpr const char* decay    = "decay";
inline constexpr const char* damping  = "damping";
inline constexpr const char* shape    = "shape";
inline constexpr const char* position = "position";

// Exciter
inline constexpr const char* exciter  = "exciter";
inline constexpr const char* hardness = "hardness";
inline constexpr const char* color    = "color";
inline constexpr const char* length   = "length";
inline constexpr const char* bounce   = "bounce";
inline constexpr const char* noise    = "noise";

// Nonlinear
inline constexpr const char* drop     = "drop";
inline constexpr const char* tension  = "tension";
inline constexpr const char* rattle   = "rattle";
inline constexpr const char* gap      = "gap";

// Body
inline constexpr const char* bodyModel  = "bodymodel";
inline constexpr const char* bodyTune   = "bodytune";
inline constexpr const char* bodyDecay  = "bodydecay";
inline constexpr const char* bodyBlend  = "bodyblend";
inline constexpr const char* bodyCouple = "bodycouple";

// Output
inline constexpr const char* width    = "width";
inline constexpr const char* gain     = "gain";

// Non-automatable UI state stored in the plugin state tree
inline constexpr const char* themeProperty = "theme";
} // namespace ids
