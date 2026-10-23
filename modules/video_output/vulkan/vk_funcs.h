/*****************************************************************************
 * vk_funcs.h: Vulkan call redirect macros
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

/*
 * Define VK_DEV to a pointer expression of type `struct vlc_vk_device *`
 * before including this file. Each .c file defines VK_DEV to its local
 * device pointer (e.g., `dev`, `(interop->dev)`).
 *
 * This redirects bare vkXxx() calls to VK_DEV->vkXxx() so call sites
 * stay unchanged after removing Vulkan prototypes (VK_NO_PROTOTYPES).
 */

#ifndef VK_DEV
# error "Define VK_DEV before including vk_funcs.h"
#endif

/* Instance-level */
#define vkDestroyInstance                        VK_DEV->vkDestroyInstance
#define vkEnumeratePhysicalDevices               VK_DEV->vkEnumeratePhysicalDevices
#define vkGetPhysicalDeviceMemoryProperties      VK_DEV->vkGetPhysicalDeviceMemoryProperties
#define vkGetPhysicalDeviceQueueFamilyProperties VK_DEV->vkGetPhysicalDeviceQueueFamilyProperties
#define vkGetPhysicalDeviceFormatProperties      VK_DEV->vkGetPhysicalDeviceFormatProperties
#define vkGetPhysicalDeviceImageFormatProperties2 VK_DEV->vkGetPhysicalDeviceImageFormatProperties2
#define vkEnumerateDeviceExtensionProperties     VK_DEV->vkEnumerateDeviceExtensionProperties
#define vkCreateDevice                           VK_DEV->vkCreateDevice
#define vkGetDeviceProcAddr                      VK_DEV->vkGetDeviceProcAddr
#define vkGetPhysicalDeviceSurfaceCapabilitiesKHR VK_DEV->vkGetPhysicalDeviceSurfaceCapabilitiesKHR
#define vkGetPhysicalDeviceSurfaceFormatsKHR     VK_DEV->vkGetPhysicalDeviceSurfaceFormatsKHR
#define vkGetPhysicalDeviceSurfacePresentModesKHR VK_DEV->vkGetPhysicalDeviceSurfacePresentModesKHR
#define vkGetPhysicalDeviceSurfaceSupportKHR VK_DEV->vkGetPhysicalDeviceSurfaceSupportKHR
#define vkGetPhysicalDeviceProperties        VK_DEV->vkGetPhysicalDeviceProperties

/* Device-level */
#define vkDeviceWaitIdle                         VK_DEV->vkDeviceWaitIdle
#define vkDestroyDevice                          VK_DEV->vkDestroyDevice
#define vkGetDeviceQueue                         VK_DEV->vkGetDeviceQueue
#define vkCreateImage                            VK_DEV->vkCreateImage
#define vkDestroyImage                           VK_DEV->vkDestroyImage
#define vkCreateImageView                        VK_DEV->vkCreateImageView
#define vkDestroyImageView                       VK_DEV->vkDestroyImageView
#define vkCreateBuffer                           VK_DEV->vkCreateBuffer
#define vkDestroyBuffer                          VK_DEV->vkDestroyBuffer
#define vkAllocateMemory                         VK_DEV->vkAllocateMemory
#define vkFreeMemory                             VK_DEV->vkFreeMemory
#define vkMapMemory                              VK_DEV->vkMapMemory
#define vkUnmapMemory                            VK_DEV->vkUnmapMemory
#define vkBindImageMemory                        VK_DEV->vkBindImageMemory
#define vkBindImageMemory2                       VK_DEV->vkBindImageMemory2
#define vkBindBufferMemory                       VK_DEV->vkBindBufferMemory
#define vkGetImageMemoryRequirements             VK_DEV->vkGetImageMemoryRequirements
#define vkGetImageMemoryRequirements2            VK_DEV->vkGetImageMemoryRequirements2
#define vkGetBufferMemoryRequirements            VK_DEV->vkGetBufferMemoryRequirements
#define vkCreateSampler                          VK_DEV->vkCreateSampler
#define vkDestroySampler                         VK_DEV->vkDestroySampler
#define vkCreateSamplerYcbcrConversion           VK_DEV->vkCreateSamplerYcbcrConversion
#define vkDestroySamplerYcbcrConversion          VK_DEV->vkDestroySamplerYcbcrConversion
#define vkCreateRenderPass                       VK_DEV->vkCreateRenderPass
#define vkDestroyRenderPass                      VK_DEV->vkDestroyRenderPass
#define vkCreateFramebuffer                      VK_DEV->vkCreateFramebuffer
#define vkDestroyFramebuffer                     VK_DEV->vkDestroyFramebuffer
#define vkCreateShaderModule                     VK_DEV->vkCreateShaderModule
#define vkDestroyShaderModule                    VK_DEV->vkDestroyShaderModule
#define vkCreateDescriptorSetLayout              VK_DEV->vkCreateDescriptorSetLayout
#define vkDestroyDescriptorSetLayout             VK_DEV->vkDestroyDescriptorSetLayout
#define vkCreateDescriptorPool                   VK_DEV->vkCreateDescriptorPool
#define vkDestroyDescriptorPool                  VK_DEV->vkDestroyDescriptorPool
#define vkCreatePipelineLayout                   VK_DEV->vkCreatePipelineLayout
#define vkDestroyPipelineLayout                  VK_DEV->vkDestroyPipelineLayout
#define vkCreatePipelineCache                    VK_DEV->vkCreatePipelineCache
#define vkDestroyPipelineCache                   VK_DEV->vkDestroyPipelineCache
#define vkCreateGraphicsPipelines                VK_DEV->vkCreateGraphicsPipelines
#define vkDestroyPipeline                        VK_DEV->vkDestroyPipeline
#define vkAllocateDescriptorSets                 VK_DEV->vkAllocateDescriptorSets
#define vkUpdateDescriptorSets                   VK_DEV->vkUpdateDescriptorSets
#define vkCreateCommandPool                      VK_DEV->vkCreateCommandPool
#define vkDestroyCommandPool                     VK_DEV->vkDestroyCommandPool
#define vkAllocateCommandBuffers                 VK_DEV->vkAllocateCommandBuffers
#define vkFreeCommandBuffers                     VK_DEV->vkFreeCommandBuffers
#define vkBeginCommandBuffer                     VK_DEV->vkBeginCommandBuffer
#define vkEndCommandBuffer                       VK_DEV->vkEndCommandBuffer
#define vkCreateFence                            VK_DEV->vkCreateFence
#define vkDestroyFence                           VK_DEV->vkDestroyFence
#define vkWaitForFences                          VK_DEV->vkWaitForFences
#define vkResetFences                            VK_DEV->vkResetFences
#define vkCreateSemaphore                        VK_DEV->vkCreateSemaphore
#define vkDestroySemaphore                       VK_DEV->vkDestroySemaphore
#define vkQueueSubmit                            VK_DEV->vkQueueSubmit
#define vkAcquireNextImageKHR                    VK_DEV->vkAcquireNextImageKHR
#define vkQueuePresentKHR                        VK_DEV->vkQueuePresentKHR
#define vkCreateSwapchainKHR                     VK_DEV->vkCreateSwapchainKHR
#define vkDestroySwapchainKHR                    VK_DEV->vkDestroySwapchainKHR
#define vkGetSwapchainImagesKHR                  VK_DEV->vkGetSwapchainImagesKHR
#define vkCmdPipelineBarrier                     VK_DEV->vkCmdPipelineBarrier
#define vkCmdBeginRenderPass                     VK_DEV->vkCmdBeginRenderPass
#define vkCmdEndRenderPass                       VK_DEV->vkCmdEndRenderPass
#define vkCmdBindPipeline                        VK_DEV->vkCmdBindPipeline
#define vkCmdBindDescriptorSets                  VK_DEV->vkCmdBindDescriptorSets
#define vkCmdBindVertexBuffers                   VK_DEV->vkCmdBindVertexBuffers
#define vkCmdSetViewport                         VK_DEV->vkCmdSetViewport
#define vkCmdSetScissor                          VK_DEV->vkCmdSetScissor
#define vkCmdDraw                                VK_DEV->vkCmdDraw
#define vkCmdCopyBufferToImage                   VK_DEV->vkCmdCopyBufferToImage
#define vkCmdCopyImage                           VK_DEV->vkCmdCopyImage
