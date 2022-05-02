#ifndef HOME_STARORPHEUS_DEVEL_VULKAN_SDL2_DEMO_VULKAN_CONTEXT_H
#define HOME_STARORPHEUS_DEVEL_VULKAN_SDL2_DEMO_VULKAN_CONTEXT_H

#pragma once

#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.hpp>

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT MessageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT MessageTypes,
    VkDebugUtilsMessengerCallbackDataEXT const *CallbackData,
    void * /*pUserData*/) {
#if !defined(NDEBUG)
  if (CallbackData->messageIdNumber == 648835635) {
    // UNASSIGNED-khronos-Validation-debug-build-warning-message
    return VK_FALSE;
  }
  if (CallbackData->messageIdNumber == 767975156) {
    // UNASSIGNED-BestPractices-vkCreateInstance-specialuse-extension
    return VK_FALSE;
  }
#endif

  errsv("{}: {}:",
        vk::to_string(static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(
            MessageSeverity)),
        vk::to_string(
            static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(MessageTypes)));

  errsv("\tmessageIDName   = <{}>", CallbackData->pMessageIdName);
  errsv("\tmessageIdNumber = {}", CallbackData->messageIdNumber);
  errsv("\tmessage         = <{}>", CallbackData->pMessage);

  if (0 < CallbackData->queueLabelCount) {
    errsv("\tQueue Labels:");
    for (uint32_t I = 0; I < CallbackData->queueLabelCount; I++)
      errsv("\t\tlabelName = <{}>", CallbackData->pQueueLabels[I].pLabelName);
  }

  if (0 < CallbackData->cmdBufLabelCount) {
    errsv("\tCommandBuffer Labels:");
    for (uint32_t I = 0; I < CallbackData->cmdBufLabelCount; I++)
      errsv("\t\tlabelName = <{}>", CallbackData->pCmdBufLabels[I].pLabelName);
  }

  if (CallbackData->objectCount > 0) {
    errsv("\tObjects:");
    for (uint32_t I = 0; I < CallbackData->objectCount; I++) {
      errsv("\t\tobject {}", I);
      errsv("\t\t\tobjectType   = {}",
            vk::to_string(static_cast<vk::ObjectType>(
                CallbackData->pObjects[I].objectType)));
      errsv("\t\t\tobjectHandle   = {}",
            CallbackData->pObjects[I].objectHandle);
      if (CallbackData->pObjects[I].pObjectName) {
        errsv("\t\t\tobjectName   = {}", CallbackData->pObjects[I].pObjectName);
      }
    }
  }
  return VK_TRUE;
}

#if defined(NDEBUG)
vk::StructureChain<vk::InstanceCreateInfo>
#else
vk::StructureChain<vk::InstanceCreateInfo, vk::DebugUtilsMessengerCreateInfoEXT>
#endif
makeInstanceCreateInfoChain(vk::ApplicationInfo const &ApplicationInfo,
                            std::vector<char const *> const &Layers,
                            std::vector<char const *> const &Extensions) {
#if defined(NDEBUG)
  // in non-debug mode just use the InstanceCreateInfo for instance creation
  vk::StructureChain<vk::InstanceCreateInfo> InstanceCreateInfo(
      {{}, &ApplicationInfo, Layers, Extensions});
#else
  // in debug mode, addionally use the debugUtilsMessengerCallback in instance
  // creation!
  vk::DebugUtilsMessageSeverityFlagsEXT SeverityFlags(
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
  vk::DebugUtilsMessageTypeFlagsEXT MessageTypeFlags(
      vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
      vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
      vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
  vk::StructureChain<vk::InstanceCreateInfo,
                     vk::DebugUtilsMessengerCreateInfoEXT>
      InstanceCreateInfo(
          {{}, &ApplicationInfo, Layers, Extensions},
          {{}, SeverityFlags, MessageTypeFlags, &debugUtilsMessengerCallback});
#endif
  return InstanceCreateInfo;
}

} // namespace

struct VulkanContext {
  VulkanContext(std::string const &AppName, std::string const &EngineName,
                std::vector<char const *> const &Extensions = {},
                std::vector<char const *> const &Layers = {}) {

    vk::ApplicationInfo ApplicationInfo(AppName.data(), 1, EngineName.data(), 1,
                                        VK_API_VERSION_1_0);
    Instance = vk::createInstance(
        makeInstanceCreateInfoChain(ApplicationInfo, Layers, Extensions)
            .get<vk::InstanceCreateInfo>());

    std::vector<vk::PhysicalDevice> PhysicalDevices =
        Instance.enumeratePhysicalDevices();
    PhysicalDevice = PhysicalDevices.front();
    for (auto &&D : PhysicalDevices) {
      VkPhysicalDeviceProperties Properties;
      vkGetPhysicalDeviceProperties(D, &Properties);
      if (Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        errsv("Selected device: <{}>", Properties.deviceName);
        PhysicalDevice = D;
        break;
      }
    }

    std::vector<vk::QueueFamilyProperties> Queues =
        PhysicalDevice.getQueueFamilyProperties();
    for (size_t I = 0; I < Queues.size(); ++I) {
      auto &&QueueFamilyProperties = Queues[I];
      if (QueueFamilyProperties.queueFlags & vk::QueueFlagBits::eGraphics) {
        QueueFamilyIndex = I;
        break;
      }
    }

    { // Create Logical Device (with 1 queue)
      float QueuePriority = 1.0f;
      std::vector<char const *> DeviceExtensions{"VK_KHR_swapchain"};
      vk::DeviceQueueCreateInfo DeviceQueueCreateInfo({}, QueueFamilyIndex, 1,
                                                      &QueuePriority);
      vk::DeviceCreateInfo DeviceCreateInfo({}, DeviceQueueCreateInfo, {},
                                            DeviceExtensions, nullptr);
      Device = PhysicalDevice.createDevice(DeviceCreateInfo);
      Queue = Device.getQueue(QueueFamilyIndex, 0);
    }

    { // Create Descriptor Pool
      std::vector<vk::DescriptorPoolSize> PoolSizes = {
          {vk::DescriptorType::eSampler, 1000},
          {vk::DescriptorType::eCombinedImageSampler, 1000},
          {vk::DescriptorType::eSampledImage, 1000},
          {vk::DescriptorType::eStorageImage, 1000},
          {vk::DescriptorType::eUniformTexelBuffer, 1000},
          {vk::DescriptorType::eUniformBuffer, 1000},
          {vk::DescriptorType::eStorageBuffer, 1000},
          {vk::DescriptorType::eUniformBufferDynamic, 1000},
          {vk::DescriptorType::eStorageBufferDynamic, 1000},
          {vk::DescriptorType::eInputAttachment, 1000}};

      vk::DescriptorPoolCreateInfo DescriptorPoolCreateInfo{
          vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 10'000,
          PoolSizes};
      DescriptorPool = Device.createDescriptorPool(DescriptorPoolCreateInfo,
                                                   AllocationCallbacks);
    }
  }

  void setupWindow(VkSurfaceKHR Surface, int Width, int Height) {
    MainWindowData.Surface = Surface;
    // Check for WSI support
    vk::Bool32 Result{false};
    PhysicalDevice.getSurfaceSupportKHR(QueueFamilyIndex, Surface, &Result);
    if (Result != VK_TRUE)
      throw std::invalid_argument("No WSI support");

    const VkFormat RequestSurfaceImageFormat[] = {
        VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};
    const VkColorSpaceKHR RequestSurfaceColorSpace =
        VK_COLORSPACE_SRGB_NONLINEAR_KHR;

    MainWindowData.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        PhysicalDevice, Surface, RequestSurfaceImageFormat,
        (size_t)IM_ARRAYSIZE(RequestSurfaceImageFormat),
        RequestSurfaceColorSpace);

#ifdef IMGUI_UNLIMITED_FRAME_RATE
    VkPresentModeKHR PresentModes[] = {VK_PRESENT_MODE_MAILBOX_KHR,
                                       VK_PRESENT_MODE_IMMEDIATE_KHR,
                                       VK_PRESENT_MODE_FIFO_KHR};
#else
    VkPresentModeKHR PresentModes[] = {VK_PRESENT_MODE_FIFO_KHR};
#endif

    MainWindowData.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
        PhysicalDevice, Surface, PresentModes, IM_ARRAYSIZE(PresentModes));
    errsv("Selected PresentMode = <{}>", MainWindowData.PresentMode);

    ImGui_ImplVulkanH_CreateOrResizeWindow(
        Instance, PhysicalDevice, Device, &MainWindowData, QueueFamilyIndex,
        reinterpret_cast<VkAllocationCallbacks const *>(&AllocationCallbacks),
        Width, Height, MinImageCount);
  }

  auto &getInstance() noexcept { return Instance; }
  auto &getMainWindowData() noexcept  { return MainWindowData; }
  auto &getPhysicalDevice() noexcept  { return PhysicalDevice; }
  auto &getDevice() noexcept { return Device; }
  auto getQueueFamilyIndex() const noexcept { return QueueFamilyIndex; }
  auto &getQueue() noexcept { return Queue; }
  auto &getPipelineCache() noexcept { return PipelineCache; }
  auto &getDescriptorPool() noexcept { return DescriptorPool; }
  auto &getMinImageCount() noexcept { return MinImageCount; }
  auto &getAllocationCallbacks() noexcept { return AllocationCallbacks; }
  auto &getSwapChainRebuild() noexcept { return SwapChainRebuild; }

private:
  vk::AllocationCallbacks AllocationCallbacks;
  vk::Instance Instance;
  vk::PhysicalDevice PhysicalDevice;
  vk::Device Device;
  uint32_t QueueFamilyIndex = -1;
  vk::Queue Queue;
  vk::PipelineCache PipelineCache;
  vk::DescriptorPool DescriptorPool;

  ImGui_ImplVulkanH_Window MainWindowData;
  uint32_t MinImageCount = 2;
  bool SwapChainRebuild{false};
};

#endif
