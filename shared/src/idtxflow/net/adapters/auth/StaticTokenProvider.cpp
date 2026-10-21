#include <idtxflow/net/adapters/auth/StaticTokenProvider.h>

namespace idtxflow
{
namespace net
{
namespace adapters
{
StaticTokenProvider& StaticTokenProvider::instance()
{
    static StaticTokenProvider provider;
    return provider;
}
} // namespace adapters
} // namespace net
} // namespace idtxflow