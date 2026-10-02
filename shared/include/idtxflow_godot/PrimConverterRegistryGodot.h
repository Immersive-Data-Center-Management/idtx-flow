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