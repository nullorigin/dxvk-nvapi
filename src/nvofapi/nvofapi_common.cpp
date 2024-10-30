/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "util/util_log.h"
#include "util/util_string.h"

#include "../inc/nvofapi/nvOpticalFlowVulkan.h"

#include "nvofapi_image.h"
#include "nvofapi_instance.h"

namespace nvofapi {

    typedef struct NV_OF_PRIV_DATA {
        uint32_t size;
        uint32_t id;
        void* data;
    } NV_OF_PRIV_DATA;

    typedef struct NV_OF_EXECUTE_PRIV_DATA_INPUT_MIPS {
        NvOFGPUBufferHandle input[6];
        NvOFGPUBufferHandle reference[6];
        uint8_t reserved[100];
    } NV_OF_EXECUTE_PRIV_DATA_INPUT_MIPS;

    constexpr uint32_t NV_OF_EXECUTE_PRIV_DATA_ID_INPUT_MIPS = 6;

    uint32_t NvOFInstance::GetVkOFAQueue() {
        uint32_t count = 0;
        m_vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysicalDevice, &count, nullptr);
        VkQueueFamilyProperties* queueFamProps = (VkQueueFamilyProperties*)calloc(sizeof(VkQueueFamilyProperties), count);
        m_vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysicalDevice, &count, queueFamProps);

        for (int i = 0; i < count; i++) {
            if (queueFamProps[i].queueFlags & VK_QUEUE_OPTICAL_FLOW_BIT_NV) {
                free(queueFamProps);
                return i;
            }
        }
        free(queueFamProps);
        return -1;
    }

    NV_OF_STATUS NvOFInstance::InitSession(const NV_OF_INIT_PARAMS* initParams) {
        dxvk::log::info(
            dxvk::str::format("OFSessionInit params:",
                " width: ", initParams->width,
                " height: ", initParams->height,
                " outGrid: ", initParams->outGridSize,
                " hintGrid: ", initParams->hintGridSize,
                " mode: ", initParams->mode,
                " perfLevel: ", initParams->perfLevel,
                " enableExternalHints: ", initParams->enableExternalHints,
                " enableOutputCost: ", initParams->enableOutputCost,
                " hPrivData: ", initParams->hPrivData,
                " enableRoi: ", initParams->enableRoi,
                " predDirection: ", initParams->predDirection,
                " enableGlobalFlow: ", initParams->enableGlobalFlow,
                " inputBufferFormat: ", initParams->inputBufferFormat));

        VkOpticalFlowSessionCreateInfoNV createInfo = {VK_STRUCTURE_TYPE_OPTICAL_FLOW_SESSION_CREATE_INFO_NV};
        createInfo.width = initParams->width;
        createInfo.height = initParams->height;
        createInfo.outputGridSize = VK_OPTICAL_FLOW_GRID_SIZE_4X4_BIT_NV;

        switch (initParams->perfLevel) {
            case NV_OF_PERF_LEVEL_SLOW:
                createInfo.performanceLevel = VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_SLOW_NV;
                break;
            case NV_OF_PERF_LEVEL_MEDIUM:
                createInfo.performanceLevel = VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_MEDIUM_NV;
                break;
            case NV_OF_PERF_LEVEL_FAST:
                createInfo.performanceLevel = VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_FAST_NV;
                break;
            default:
                createInfo.performanceLevel = VK_OPTICAL_FLOW_PERFORMANCE_LEVEL_UNKNOWN_NV;
                break;
        }

        switch (initParams->inputBufferFormat) {
            case NV_OF_BUFFER_FORMAT_GRAYSCALE8:
                createInfo.imageFormat = VK_FORMAT_R8_UNORM;
                break;
            case NV_OF_BUFFER_FORMAT_NV12:
                createInfo.imageFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
                break;
            case NV_OF_BUFFER_FORMAT_ABGR8:
                createInfo.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
                break;
        }

        // Need to get the size/id for the private data to pass it along to VK...
        VkOpticalFlowSessionCreatePrivateDataInfoNV privData = {VK_STRUCTURE_TYPE_OPTICAL_FLOW_SESSION_CREATE_PRIVATE_DATA_INFO_NV};
        privData.size = ((NV_OF_PRIV_DATA*)initParams->hPrivData)->size;
        privData.id = ((NV_OF_PRIV_DATA*)initParams->hPrivData)->id;
        privData.pPrivateData = ((NV_OF_PRIV_DATA*)initParams->hPrivData)->data;

        createInfo.pNext = &privData;

        auto ret = m_vkCreateOpticalFlowSessionNV(m_vkDevice, &createInfo, NULL, &m_vkOfaSession);

        if (ret == VK_SUCCESS) {
            return NV_OF_SUCCESS;
        }

        return NV_OF_ERR_GENERIC;
    }

    NV_OF_STATUS NvOFInstance::BindImageToSession(NvOFImage* image, VkOpticalFlowSessionBindingPointNV bindingPoint) {
        VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL;

        if (!image)
            return NV_OF_ERR_GENERIC;

        auto ret = m_vkBindOpticalFlowSessionImageNV(m_vkDevice,
            m_vkOfaSession,
            bindingPoint,
            image->ImageView(),
            layout);
        if (ret != VK_SUCCESS) {
            return NV_OF_ERR_GENERIC;
        }
        return NV_OF_SUCCESS;
    }

    NV_OF_STATUS NvOFInstance::getCaps(NV_OF_CAPS param, uint32_t* capsVal, uint32_t* size) {
        if (param == NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES) {
            *size = 1;
            if (capsVal) {
                *capsVal = NV_OF_OUTPUT_VECTOR_GRID_SIZE_4;
            }

            // XXX[ljm] query VkPhysicalDevice for actual support
            return NV_OF_SUCCESS;
        }
        return NV_OF_ERR_GENERIC;
    }

    NV_OF_STATUS NvOFInstance::RegisterBuffer(const NV_OF_REGISTER_RESOURCE_PARAMS_VK* registerParams) {
        NvOFImage* nvOFImage = new NvOFImage(m_vkDevice, registerParams->image, registerParams->format);
        nvOFImage->Initialize(m_vkCreateImageView, m_vkDestroyImageView);
        *registerParams->hOFGpuBuffer = reinterpret_cast<NvOFGPUBufferHandle>(nvOFImage);
        return NV_OF_SUCCESS;
    }

    NV_OF_STATUS NvOFInstance::RecordCmdBuf(const NV_OF_EXECUTE_INPUT_PARAMS_VK* inParams, NV_OF_EXECUTE_OUTPUT_PARAMS_VK* outParams, VkCommandBuffer cmdBuf) {
        BindImageToSession(reinterpret_cast<NvOFImage*>(inParams->inputFrame), VK_OPTICAL_FLOW_SESSION_BINDING_POINT_INPUT_NV);
        BindImageToSession(reinterpret_cast<NvOFImage*>(inParams->referenceFrame), VK_OPTICAL_FLOW_SESSION_BINDING_POINT_REFERENCE_NV);
        BindImageToSession(reinterpret_cast<NvOFImage*>(outParams->outputBuffer), VK_OPTICAL_FLOW_SESSION_BINDING_POINT_FLOW_VECTOR_NV);
        BindImageToSession(reinterpret_cast<NvOFImage*>(outParams->outputCostBuffer), VK_OPTICAL_FLOW_SESSION_BINDING_POINT_COST_NV);
        BindImageToSession(reinterpret_cast<NvOFImage*>(outParams->bwdOutputBuffer), VK_OPTICAL_FLOW_SESSION_BINDING_POINT_BACKWARD_FLOW_VECTOR_NV);
        BindImageToSession(reinterpret_cast<NvOFImage*>(outParams->bwdOutputCostBuffer), VK_OPTICAL_FLOW_SESSION_BINDING_POINT_BACKWARD_COST_NV);
        BindImageToSession(reinterpret_cast<NvOFImage*>(outParams->globalFlowBuffer), VK_OPTICAL_FLOW_SESSION_BINDING_POINT_GLOBAL_FLOW_NV);
        // Support INPUT_MIPS execute priv data
        if (((NV_OF_PRIV_DATA*)inParams->hPrivData)->id == NV_OF_EXECUTE_PRIV_DATA_ID_INPUT_MIPS) {
            NV_OF_EXECUTE_PRIV_DATA_INPUT_MIPS* mipData = ((NV_OF_EXECUTE_PRIV_DATA_INPUT_MIPS*)((NV_OF_PRIV_DATA*)inParams->hPrivData)->data);
            for (int i = 0; i < 6; i++) {
                if (mipData->input[i] && mipData->reference[i]) {
                    BindImageToSession(reinterpret_cast<NvOFImage*>(mipData->input[i]), VK_OPTICAL_FLOW_SESSION_BINDING_POINT_INPUT_NV);
                    BindImageToSession(reinterpret_cast<NvOFImage*>(mipData->reference[i]), VK_OPTICAL_FLOW_SESSION_BINDING_POINT_REFERENCE_NV);
                }
            }
        }
        VkRect2D* regions = nullptr;

        if (inParams->numRois) {
            regions = (VkRect2D*)calloc(sizeof(VkRect2D), inParams->numRois);
            for (int i = 0; i < inParams->numRois; i++) {
                regions[i].offset.x = inParams->roiData[i].start_x;
                regions[i].offset.y = inParams->roiData[i].start_y;
                regions[i].extent.width = inParams->roiData[i].width;
                regions[i].extent.height = inParams->roiData[i].height;
            }
        }

        VkOpticalFlowExecuteInfoNV ofaExecuteInfo = {VK_STRUCTURE_TYPE_OPTICAL_FLOW_EXECUTE_INFO_NV};
        ofaExecuteInfo.regionCount = inParams->numRois;
        ofaExecuteInfo.pRegions = regions;

        if (inParams->disableTemporalHints) {
            ofaExecuteInfo.flags |= VK_OPTICAL_FLOW_EXECUTE_DISABLE_TEMPORAL_HINTS_BIT_NV;
        }

        m_vkCmdOpticalFlowExecuteNV(cmdBuf, m_vkOfaSession, &ofaExecuteInfo);

        free(regions);
        return NV_OF_SUCCESS;
    }
}
