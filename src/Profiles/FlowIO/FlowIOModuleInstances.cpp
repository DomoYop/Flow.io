#include "Profiles/FlowIO/FlowIOProfile.h"

#include "Board/BoardCatalog.h"

namespace Profiles {
namespace FlowIO {

ModuleInstances::ModuleInstances(const BoardSpec& board)
    : wifiModule(board),
      i2cCfgServerModule(board),
      ioModule(board)
#if defined(FLOW_WAVESHARE_STANDALONE)
    , webInterfaceModule(board)
    , firmwareUpdateModule(board)
#endif
{
}

ModuleInstances& moduleInstances()
{
    static ModuleInstances instances{BoardCatalog::flowIODINv1()};
    return instances;
}

}  // namespace FlowIO
}  // namespace Profiles
