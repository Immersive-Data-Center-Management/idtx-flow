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
 *   - PrimConverterRegistry<TargetEngine>::Instance() is declared only (no body) in
 *     the shared PrimConverterRegistry.h header; its single definition lives in
 *     PrimConverterRegistryGodot.cpp, next to the explicit instantiation, and is
 *     exported from the IDTXFlow DLL via IDTXFLOW_GODOT_API.
 *   - This header declares `extern template` to suppress implicit instantiation in
 *     any translation unit that includes it.
 *   - Because the Instance() body is never visible to consumers, an extension in its
 *     own DLL/.so can neither inline nor re-instantiate it; it emits an undefined
 *     reference that binds to IDTXFlow's one exported singleton at link/load time.
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
// Suppresses implicit instantiation of the template in consumer TUs. The single
// instantiation lives in PrimConverterRegistryGodot.cpp inside the IDTXFlow DLL.
//
// On Windows, __declspec(dllimport) on this extern-template binds every module to
// IDTXFlow's instantiation. On ELF targets (Android/Linux), godot-cpp compiles
// with -fvisibility=hidden, so the exported symbols reach IDTXFlow's .dynsym only
// because the explicit instantiation carries the IDTXFLOW_GODOT_API default-
// visibility attribute. The crucial part for the singleton is that Instance() has
// no body in the header (see PrimConverterRegistry.h): a consumer cannot inline it
// into its own module and therefore always binds to IDTXFlow's one instance.
extern template class IDTXFLOW_GODOT_API idtxflow::converter::PrimConverterRegistry<idtxflow::types::TargetEngineGodot>;