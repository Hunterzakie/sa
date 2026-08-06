/*****************************************************************************
 * device.h: Vulkan device and utilities
 *****************************************************************************
 * Copyright (C) 2025-2026 VideoLabs
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef VLC_VULKAN_DEVICE_H
#define VLC_VULKAN_DEVICE_H

#include "common.h"
#include "instance.h"

/**
 * Instance-level functions loaded via vkGetInstanceProcAddr.
 * All mandatory — NULL means broken driver.
 */
#define VK_INSTANCE_FUNCS(X) \
    X(vkDestroyInstance) \
    X(vkEnumeratePhysicalDevices) \
    X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceFormatProperties) \
    X(vkGetPhysicalDeviceImageFormatProperties2) \
    X(vkEnumerateDeviceExtensionProperties) \
    X(vkCreateDevice) \
    X(vkGetDeviceProcAddr) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkGetPhysicalDeviceProperties)

/**
 * Device-level functions loaded via vkGetDeviceProcAddr.
 * All mandatory — NULL means broken driver.
 */
#define VK_DEVICE_FUNCS(X) \
    X(vkDeviceWaitIdle) \
    X(vkDestroyDevice) \
    X(vkGetDeviceQueue) \
    X(vkCreateImage) \
    X(vkDestroyImage) \
    X(vkCreateImageView) \
    X(vkDestroyImageView) \
    X(vkCreateBuffer) \
    X(vkDestroyBuffer) \
    X(vkAllocateMemory) \
    X(vkFreeMemory) \
    X(vkMapMemory) \
    X(vkUnmapMemory) \
    X(vkBindImageMemory) \
    X(vkBindImageMemory2) \
    X(vkBindBufferMemory) \
    X(vkGetImageMemoryRequirements) \
    X(vkGetImageMemoryRequirements2) \
    X(vkGetBufferMemoryRequirements) \
    X(vkCreateSampler) \
    X(vkDestroySampler) \
    X(vkCreateRenderPass) \
    X(vkDestroyRenderPass) \
    X(vkCreateFramebuffer) \
    X(vkDestroyFramebuffer) \
    X(vkCreateShaderModule) \
    X(vkDestroyShaderModule) \
    X(vkCreateDescriptorSetLayout) \
    X(vkDestroyDescriptorSetLayout) \
    X(vkCreateDescriptorPool) \
    X(vkDestroyDescriptorPool) \
    X(vkCreatePipelineLayout) \
    X(vkDestroyPipelineLayout) \
    X(vkCreatePipelineCache) \
    X(vkDestroyPipelineCache) \
    X(vkCreateGraphicsPipelines) \
    X(vkDestroyPipeline) \
    X(vkAllocateDescriptorSets) \
    X(vkUpdateDescriptorSets) \
    X(vkCreateCommandPool) \
    X(vkDestroyCommandPool) \
    X(vkAllocateCommandBuffers) \
    X(vkFreeCommandBuffers) \
    X(vkBeginCommandBuffer) \
    X(vkEndCommandBuffer) \
    X(vkCreateFence) \
    X(vkDestroyFence) \
    X(vkWaitForFences) \
    X(vkResetFences) \
    X(vkCreateSamplerYcbcrConversion) \
    X(vkDestroySamplerYcbcrConversion) \
    X(vkCreateSemaphore) \
    X(vkDestroySemaphore) \
    X(vkQueueSubmit) \
    X(vkAcquireNextImageKHR) \
    X(vkQueuePresentKHR) \
    X(vkCreateSwapchainKHR) \
    X(vkDestroySwapchainKHR) \
    X(vkGetSwapchainImagesKHR) \
    X(vkCmdPipelineBarrier) \
    X(vkCmdBeginRenderPass) \
    X(vkCmdEndRenderPass) \
    X(vkCmdBindPipeline) \
    X(vkCmdBindDescriptorSets) \
    X(vkCmdBindVertexBuffers) \
    X(vkCmdSetViewport) \
    X(vkCmdSetScissor) \
    X(vkCmdDraw) \
    X(vkCmdCopyBufferToImage) \
    X(vkCmdCopyImage)

/**
 * Vulkan device - shared by all components (similar to vlc_gl_api)
 *
 * All component structures contain a pointer to this as their first member.
 */
struct vlc_vk_device {
    struct vlc_logger *logger;  /* For logging (replaces msg_Err(vd, ...)) */

    vlc_vk_instance_t inst;  /* VLC wrapper: VkInstance + get_proc_address */
    VkDebugUtilsMessengerEXT debug_messenger;

    VkPhysicalDevice gpu;
    VkPhysicalDeviceMemoryProperties gpu_mem_props;
    VkDevice device;
    uint32_t queue_family_index;

    /* Mandatory Vulkan function pointers (X-macro generated) */
#define VK_PFN_FIELD(name) PFN_##name name;
    VK_INSTANCE_FUNCS(VK_PFN_FIELD)
    VK_DEVICE_FUNCS(VK_PFN_FIELD)
#undef VK_PFN_FIELD

    /* Optional extension function pointers (manual, NULL-checked) */
    PFN_vkSetHdrMetadataEXT vkSetHdrMetadataEXT;
    PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFd;  /* For read fence export */

    /* Debug utils function pointers (NULL when debug utils unavailable) */
    PFN_vkSetDebugUtilsObjectNameEXT vkSetObjectName;

    /* External device ownership flags (for libvlc callbacks) */
    bool owns_instance;  /* false if instance provided externally */
    bool owns_device;    /* false if device provided externally */
};

#define VK_LOG_ERR(dev, ...)   vlc_error((dev)->logger, __VA_ARGS__)
#define VK_LOG_WARN(dev, ...)  vlc_warning((dev)->logger, __VA_ARGS__)
#define VK_LOG_INFO(dev, ...)  vlc_info((dev)->logger, __VA_ARGS__)
#define VK_LOG_DBG(dev, ...)   vlc_debug((dev)->logger, __VA_ARGS__)

/* Vulkan error checking helper - returns true on error, false on success */
static inline bool vlc_vk_check(const struct vlc_vk_device *dev, VkResult result,
                                const char *msg, const char *file, int line)
{
    if (result != VK_SUCCESS)
    {
        vlc_error(dev->logger, "%s: error %s (%d) at %s:%d",
            msg, vlc_vk_result_string(result), result, file, line);
        return true;
    }
    return false;
}

void vlc_vk_device_Init(struct vlc_vk_device *dev, struct vlc_logger *logger);

/**
 * Populate the device wrapper from externally owned Vulkan handles.
 *
 * Sets owns_instance/owns_device to false so Destroy skips them.
 * Requires dev->inst (instance + get_proc_address) to be populated before
 * calling.
 */
int vlc_vk_device_SetExternal(struct vlc_vk_device *dev,
                               VkPhysicalDevice physical_device,
                               VkDevice device,
                               uint32_t queue_family);

void vlc_vk_device_Destroy(struct vlc_vk_device *dev);

#endif /* VLC_VULKAN_DEVICE_H */
