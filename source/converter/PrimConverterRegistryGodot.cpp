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

// Explicit instantiation – the exported definition of the entire class template.
template class IDTXFLOW_GODOT_API idtxflow::converter::PrimConverterRegistry<idtxflow::types::TargetEngineGodot>;