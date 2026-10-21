/**
 * @file PrimConverterRegistryGodot.cpp
 * @brief Explicit template instantiation of PrimConverterRegistry for Godot.
 *
 * This translation unit is compiled into the IDTXFlow DLL. It ensures that
 * exactly one copy of PrimConverterRegistry<TargetEngineGodot>::Instance()
 * (and all other member functions) exists across the process, even when
 * third-party GDExtension DLLs link against IDTXFlow.
 *
 * Third-party extensions include PrimConverterRegistryGodot.h which contains
 * the matching `extern template class` declaration that suppresses implicit
 * instantiation in their own translation units.
 */

#include <idtxflow/converter/PrimConverterRegistry.h>

#include "../include/idtxflow_godot/idtxflow_godot_api.h"
#include "../include/idtxflow_godot/types/GodotTypes.h"

// Out-of-line definition of the singleton accessor. It must precede the explicit
// instantiation below so that instantiation emits this definition (exported with
// default visibility via IDTXFLOW_GODOT_API) into libidtxflow. Consumer TUs — in
// their own DLL/.so — only ever see the declaration in PrimConverterRegistry.h,
// so they cannot inline or re-instantiate it and instead bind to this one symbol.
namespace idtxflow
{
namespace converter
{
template <typename TargetEngine>
    requires types::ValidTargetEngine<TargetEngine>
PrimConverterRegistry<TargetEngine>& PrimConverterRegistry<TargetEngine>::Instance()
{
    // Meyers singleton — exactly one instance within libidtxflow, shared by every
    // module that links it (IDTXFlow, third-party extensions).
    static PrimConverterRegistry instance;
    return instance;
}
} // namespace converter
} // namespace idtxflow

// Explicit instantiation – the exported definition of the entire class template.
template class IDTXFLOW_GODOT_API idtxflow::converter::PrimConverterRegistry<idtxflow::types::TargetEngineGodot>;

// Non-inline, C-linkage accessor — the single cross-module entry point to the
// Godot registry singleton. Unlike the inline Instance() template method, an
// extern "C" function has no vague/COMDAT linkage and is immune to
// -fvisibility-inlines-hidden, so it always lands in libidtxflow's .dynsym with
// default visibility. Every consumer (including extensions in other .so files)
// that calls this shares IDTXFlow's own singleton. See the rationale in
// PrimConverterRegistryGodot.h. This definition must exist exactly once, here,
// in the translation unit that owns the explicit instantiation above.
extern "C" IDTXFLOW_GODOT_API idtxflow::converter::PrimConverterRegistry<idtxflow::types::TargetEngineGodot>&
IDTXFlow_GodotPrimConverterRegistry()
{
    return idtxflow::converter::PrimConverterRegistry<idtxflow::types::TargetEngineGodot>::Instance();
}