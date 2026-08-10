#pragma once

// mingw-w64's client header suppresses all of its shared enums when
// uiautomationcoreapi.h was included first, although that header lacks these
// three newer client enums.
#ifdef _INC_UIAUTOMATIONCOREAPI
enum TreeTraversalOptions
{
    TreeTraversalOptions_Default = 0,
    TreeTraversalOptions_PostOrder = 1,
    TreeTraversalOptions_LastToFirstOrder = 2,
};
enum ConnectionRecoveryBehaviorOptions
{
    ConnectionRecoveryBehaviorOptions_Disabled = 0,
    ConnectionRecoveryBehaviorOptions_Enabled = 1,
};
enum CoalesceEventsOptions
{
    CoalesceEventsOptions_Disabled = 0,
    CoalesceEventsOptions_Enabled = 1,
};
#endif

#include <uiautomationclient.h>
