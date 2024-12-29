#include "nvofapi_tests_private.h"
#include "nvofapi/mock_factory.h"

using namespace trompeloeil;

TEST_CASE("CreateInstanceVk returns not-available", "[.cuda]") {
    struct NV_OF_CUDA_API_FUNCTION_LIST {
    } functionList;
    REQUIRE(NvOFAPICreateInstanceCuda(80, &functionList) == NV_OF_ERR_OF_NOT_AVAILABLE);
}
