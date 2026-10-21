#pragma once
/**
 * @file PrimConverterRegistryGodot.h
 * @brief Godot-specialised PrimConverterRegistry with DLL boundary-safe singleton.
 *
 * Problem: PrimConverterRegistry<TargetEngineGodot> uses a static-local singleton inside
 * a header-only template. When a third-party GDExtension DLL includes the same header,
 * the linker gives it its own copy of the static, so converters registered by the
 * extension never appear in the registry that StageConverter queries inside IDTXFlow.
 *
 * Solution:
 *   - IDTXFlow explicitly instantiates the template in PrimConverterRegistryGodot.cpp
 *     and exports the symbols via IDTXFLOW_GODOT_API.
 *   - This header declares `extern template` to suppress implicit instantiation in
 *     any translation unit that includes it.
 *   - Third-party extensions include this header instead of using the shared registry
 *     header directly, guaranteeing they link to the IDTXFlow-owned singleton.
 *
 * Usage in an extension:
 *   #include <idtxflow_godot/PrimConverterRegistryGodot.h>
 *   auto& registry = idtxflow::converter::PrimConverterRegistry<
 *       idtxflow::types::TargetEngineGodot>::Instance();
 *   registry.Register(myConverter);
 */

#include "idtxflow_godot_api.h"

// Pull in the full template definition (needed for the extern template declaration)
#include <idtxflow/converter/PrimConverterRegistry.h>

// Pull in the Godot target-engine tag and type mapping
#include "types/GodotTypes.h"

// ---- Extern template declaration ----
// This suppresses implicit instantiation of the template in consumer TUs.
// The single instantiation lives in PrimConverterRegistryGodot.cpp inside the
// IDTXFlow DLL, ensuring exactly one singleton across all modules.
extern template class IDTXFLOW_GODOT_API idtxflow::converter::PrimConverterRegistry<idtxflow::types::TargetEngineGodot>;

// ---- Cross-module singleton accessor (the ELF-safe entry point) ----
//
// On Windows, __declspec(dllimport) on the extern-template above binds every
// module to IDTXFlow's single instantiation, so Instance() "just works".
//
// On ELF targets (Android/Linux) it does NOT. godot-cpp compiles with
// -fvisibility-inlines-hidden, which forces the *inline* Instance() template
// method — and its function-local `static` singleton — to HIDDEN visibility,
// regardless of the visibility("default") attribute on the class. The symbol
// therefore never enters libidtxflow's .dynsym, there is no dllimport-equivalent
// to redirect the consumer, and each dependent .so instantiates its OWN copy of
// the singleton. The result: converters a plugin registers land in a different
// registry than the one IDTXFlow's StageConverter queries, so they never fire.
//
// The fix is a single NON-inline accessor with C language linkage. `extern "C"`
// has no vague/COMDAT linkage, is not mangled into a namespace, and is not
// subject to -fvisibility-inlines-hidden, so it always lands in libidtxflow's
// .dynsym with default visibility. It is defined exactly once (in
// PrimConverterRegistryGodot.cpp, compiled into libidtxflow) and returns
// IDTXFlow's own Instance(), so every module that calls it shares that object.
//
// ANY code compiled into a separate shared library (third-party extensions)
// MUST reach the registry through this accessor — or the
// idtxflow::converter::GodotPrimConverterRegistry() wrapper below — rather than
// calling PrimConverterRegistry<TargetEngineGodot>::Instance() directly.
extern "C" IDTXFLOW_GODOT_API idtxflow::converter::PrimConverterRegistry<idtxflow::types::TargetEngineGodot>&
IDTXFlow_GodotPrimConverterRegistry();

namespace idtxflow
{
namespace converter
{
/**
 * Canonical cross-module accessor for the Godot prim-converter registry.
 *
 * Forwards to the exported extern "C" accessor, guaranteeing the single
 * IDTXFlow-owned singleton on every platform (Windows, macOS, Android/Linux).
 * Prefer this over PrimConverterRegistry<TargetEngineGodot>::Instance() in any
 * translation unit that is linked into its own shared library.
 */
inline PrimConverterRegistry<types::TargetEngineGodot>& GodotPrimConverterRegistry()
{
    return ::IDTXFlow_GodotPrimConverterRegistry();
}
} // namespace converter
} // namespace idtxflow