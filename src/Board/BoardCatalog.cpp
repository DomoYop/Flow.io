#include "Board/BoardCatalog.h"

#include "App/BuildFlags.h"
#include "Board/FlowIODINBoards.h"
#include "Board/MicronovaBoard.h"
#include "Board/SupervisorBoardRev1.h"
#include "Board/WaveshareS3ETHBoard.h"

namespace BoardCatalog {

const BoardSpec& flowIODINv1()
{
    return BoardProfiles::kFlowIODINv1;
}

const BoardSpec& flowIODINv2()
{
    return BoardProfiles::kFlowIODINv2;
}

const BoardSpec& supervisorBoardRev1()
{
    return BoardProfiles::kSupervisorBoardRev1;
}

const BoardSpec& micronovaBoardRev1()
{
    return BoardProfiles::kMicronovaBoardRev1;
}

const BoardSpec& waveshareS3ETH8DI8RO()
{
    return BoardProfiles::kWaveshareS3ETH8DI8RO;
}

const BoardSpec& activeBoard()
{
#if FLOW_BUILD_IS_FLOWIO && defined(FLOW_BOARD_WAVESHARE_S3_ETH)
    return waveshareS3ETH8DI8RO();
#elif FLOW_BUILD_IS_FLOWIO
    return flowIODINv1();
#elif FLOW_BUILD_IS_SUPERVISOR
    return supervisorBoardRev1();
#elif FLOW_BUILD_IS_MICRONOVA
    return micronovaBoardRev1();
#else
    return flowIODINv1();
#endif
}

}  // namespace BoardCatalog
