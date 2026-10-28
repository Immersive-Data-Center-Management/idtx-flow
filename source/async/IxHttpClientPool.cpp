/**
 * @file IxHttpClientPool.cpp
 * @brief Out-of-line definition of the IxHttpClientPool singleton accessor.
 *
 * This translation unit is compiled into libidtxflow. Keeping Instance()
 * out-of-line — rather than as an inline Meyers singleton in the header —
 * guarantees a single IxHttpClientPool across every shared library that links
 * libidtxflow. An inline accessor would be forced to hidden visibility by
 * -fvisibility=hidden on ELF/Android and duplicate per-.so, so an extension's
 * REST nodes would submit into a different pool than the one created at module
 * init. See the rationale on IxHttpClientPool::Instance() in the header.
 *
 * Only the accessor needs to cross the module boundary: the remaining (inline)
 * members all operate on `this`, which now refers to this one shared instance.
 */

#include <idtxflow/async/IxHttpClientPool.h>

namespace idtxflow
{
namespace async
{

IxHttpClientPool& IxHttpClientPool::Instance()
{
    // Meyers singleton — exactly one instance within libidtxflow, shared by all
    // modules that link it (IDTXFlow, third-party extensions).
    static IxHttpClientPool instance;
    return instance;
}

} // namespace async
} // namespace idtxflow
