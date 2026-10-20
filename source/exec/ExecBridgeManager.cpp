/**
 * @file ExecBridgeManager.cpp
 * @brief Out-of-line definition of the ExecBridgeManager singleton accessor.
 *
 * This translation unit is compiled into libidtxflow. Keeping Instance()
 * out-of-line — rather than as an inline Meyers singleton in the header —
 * guarantees a single ExecBridgeManager across every shared library that links
 * libidtxflow. An inline accessor would be forced to hidden visibility by
 * -fvisibility-inlines-hidden on ELF/Android and duplicate per-.so, so an
 * extension's exec bridges/handlers would register into a different manager than
 * the one IDTXFlow's worker thread drives. See the rationale on
 * ExecBridgeManager::Instance() in the header.
 *
 * Only the accessor needs to cross the module boundary: the remaining (inline)
 * members all operate on `this`, which now refers to this one shared instance.
 */

#include <idtxflow/exec/ExecBridgeManager.h>

namespace idtxflow
{
namespace exec
{

ExecBridgeManager& ExecBridgeManager::Instance()
{
    // Meyers singleton — exactly one instance within libidtxflow, shared by all
    // modules that link it (IDTXFlow, IDTXFlowIDCM, third-party extensions).
    static ExecBridgeManager instance;
    return instance;
}

} // namespace exec
} // namespace idtxflow
