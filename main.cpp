#include "format.h"
#include "vulkan_context.h"

#include <SDL2pp/SDL2pp.hh>
#include <SDL_vulkan.h>
#include <imgui_impl_sdl.h>

static void checkVkResult(VkResult Err) {
  if (Err == 0)
    return;
  errsv("[vulkan] Error: VkResult = {}", Err);
  throw std::runtime_error("Caught vulkan error");
}

static void frameRender(VulkanContext &Vulkan, ImDrawData *draw_data) {
  VkResult Err;
  auto &Wd = Vulkan.getMainWindowData();

  VkSemaphore ImageAcquiredSemaphore =
      Wd.FrameSemaphores[Wd.SemaphoreIndex].ImageAcquiredSemaphore;
  VkSemaphore RenderCompleteSemaphore =
      Wd.FrameSemaphores[Wd.SemaphoreIndex].RenderCompleteSemaphore;
  Err = vkAcquireNextImageKHR(Vulkan.getDevice(), Wd.Swapchain, UINT64_MAX,
                              ImageAcquiredSemaphore, VK_NULL_HANDLE,
                              &Wd.FrameIndex);
  if (Err == VK_ERROR_OUT_OF_DATE_KHR || Err == VK_SUBOPTIMAL_KHR) {
    Vulkan.getSwapChainRebuild() = true;
    return;
  }
  checkVkResult(Err);

  ImGui_ImplVulkanH_Frame *Fd = &Wd.Frames[Wd.FrameIndex];
  {
    Err = vkWaitForFences(
        Vulkan.getDevice(), 1, &Fd->Fence, VK_TRUE,
        UINT64_MAX); // wait indefinitely instead of periodically checking
    checkVkResult(Err);

    Err = vkResetFences(Vulkan.getDevice(), 1, &Fd->Fence);
    checkVkResult(Err);
  }
  {
    Err = vkResetCommandPool(Vulkan.getDevice(), Fd->CommandPool, 0);
    checkVkResult(Err);
    VkCommandBufferBeginInfo Info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    Err = vkBeginCommandBuffer(Fd->CommandBuffer, &Info);
    checkVkResult(Err);
  }
  {
    VkRenderPassBeginInfo Info = {.sType =
                                      VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                  .renderPass = Wd.RenderPass,
                                  .framebuffer = Fd->Framebuffer,
                                  .clearValueCount = 1,
                                  .pClearValues = &Wd.ClearValue};
    Info.renderArea.extent.width = Wd.Width;
    Info.renderArea.extent.height = Wd.Height;
    vkCmdBeginRenderPass(Fd->CommandBuffer, &Info, VK_SUBPASS_CONTENTS_INLINE);
  }

  // Record dear imgui primitives into command buffer
  ImGui_ImplVulkan_RenderDrawData(draw_data, Fd->CommandBuffer);

  // Submit command buffer
  vkCmdEndRenderPass(Fd->CommandBuffer);
  {
    VkPipelineStageFlags WaitStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo Info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                         .waitSemaphoreCount = 1,
                         .pWaitSemaphores = &ImageAcquiredSemaphore,
                         .pWaitDstStageMask = &WaitStage,
                         .commandBufferCount = 1,
                         .pCommandBuffers = &Fd->CommandBuffer,
                         .signalSemaphoreCount = 1,
                         .pSignalSemaphores = &RenderCompleteSemaphore};


    Err = vkEndCommandBuffer(Fd->CommandBuffer);
    checkVkResult(Err);
    Err = vkQueueSubmit(Vulkan.getQueue(), 1, &Info, Fd->Fence);
    checkVkResult(Err);
  }
}

static void framePresent(VulkanContext &Vulkan) {
  if (Vulkan.getSwapChainRebuild())
    return;
  auto &Wd = Vulkan.getMainWindowData();
  VkSemaphore RenderCompleteSemaphore =
      Wd.FrameSemaphores[Wd.SemaphoreIndex].RenderCompleteSemaphore;

  VkPresentInfoKHR Info = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                           .waitSemaphoreCount = 1,
                           .pWaitSemaphores = &RenderCompleteSemaphore,
                           .swapchainCount = 1,
                           .pSwapchains = &Wd.Swapchain,
                           .pImageIndices = &Wd.FrameIndex};

  VkResult Err = vkQueuePresentKHR(Vulkan.getQueue(), &Info);
  if (Err == VK_ERROR_OUT_OF_DATE_KHR || Err == VK_SUBOPTIMAL_KHR) {
    Vulkan.getSwapChainRebuild() = true;
    return;
  }

  checkVkResult(Err);

  // Now we can use the next set of semaphores
  Wd.SemaphoreIndex = (Wd.SemaphoreIndex + 1) % Wd.ImageCount;
}

int main(int, char **) {
  try {
    SDL2pp::SDL SDL(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER);
    SDL2pp::Window Window(
        "Dear ImGui SDL2+Vulkan example", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 1280, 720,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

    // Setup Vulkan
    uint32_t ExtensionsCount = 0;
    SDL_Vulkan_GetInstanceExtensions(Window.Get(), &ExtensionsCount, nullptr);
    std::vector<char const *> Extensions(ExtensionsCount);
    SDL_Vulkan_GetInstanceExtensions(Window.Get(), &ExtensionsCount,
                                     Extensions.data());

    VulkanContext Vulkan("AppName", "EngineName", Extensions, {});

    vk::SurfaceKHR Surface;
    { // Create Window Surface
      VkSurfaceKHR LSurface;
      if (SDL_Vulkan_CreateSurface(Window.Get(), Vulkan.getInstance(),
                                   &LSurface) == 0) {
        errsv("Failed to create Vulkan surface.");
        return 1;
      }
      Surface = LSurface;
    }

    auto WindowW = Window.GetWidth();
    auto WindowH = Window.GetHeight();

    Vulkan.setupWindow(Surface, WindowW, WindowH);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &Io = ImGui::GetIO();

    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForVulkan(Window.Get());

    ImGui_ImplVulkan_InitInfo InitInfo = {
        .Instance = Vulkan.getInstance(),
        .PhysicalDevice = Vulkan.getPhysicalDevice(),
        .Device = Vulkan.getDevice(),
        .QueueFamily = Vulkan.getQueueFamilyIndex(),
        .Queue = Vulkan.getQueue(),
        .PipelineCache = Vulkan.getPipelineCache(),
        .DescriptorPool = Vulkan.getDescriptorPool(),
        .Subpass = 0,
        .MinImageCount = Vulkan.getMinImageCount(),
        .ImageCount = Vulkan.getMinImageCount(),
        .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
        .Allocator = reinterpret_cast<VkAllocationCallbacks const *>(
            &Vulkan.getAllocationCallbacks()),
        .CheckVkResultFn = checkVkResult};

    ImGui_ImplVulkan_Init(&InitInfo, Vulkan.getMainWindowData().RenderPass);

    // Upload Fonts
    {
      auto &Wd = Vulkan.getMainWindowData();
      // Use any command queue
      vk::CommandPool CommandPool = Wd.Frames[Wd.FrameIndex].CommandPool;
      vk::CommandBuffer CommandBuffer = Wd.Frames[Wd.FrameIndex].CommandBuffer;

      Vulkan.getDevice().resetCommandPool(CommandPool);

      vk::CommandBufferBeginInfo BeginInfo{
          vk::CommandBufferUsageFlagBits::eOneTimeSubmit};

      CommandBuffer.begin(BeginInfo);

      ImGui_ImplVulkan_CreateFontsTexture(CommandBuffer);

      vk::SubmitInfo EndInfo{VkSubmitInfo{
          .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .commandBufferCount = 1,
          .pCommandBuffers =
              reinterpret_cast<VkCommandBuffer const *>(&CommandBuffer)}};
      CommandBuffer.end();
      Vulkan.getQueue().submit(1, &EndInfo, VK_NULL_HANDLE);
      Vulkan.getDevice().waitIdle();
      ImGui_ImplVulkan_DestroyFontUploadObjects();
    }

    ImVec4 ClearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    bool Done = false;
    while (!Done) {
      // Poll and handle events (inputs, window resize, etc.)
      // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to
      // tell if dear imgui wants to use your inputs.
      // - When io.WantCaptureMouse is true, do not dispatch mouse input data to
      // your main application, or clear/overwrite your copy of the mouse data.
      // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input
      // data to your main application, or clear/overwrite your copy of the
      // keyboard data. Generally you may always pass all inputs to dear imgui,
      // and hide them from your application based on those two flags.
      SDL_Event Event;
      while (SDL_PollEvent(&Event)) {
        ImGui_ImplSDL2_ProcessEvent(&Event);
        if (Event.type == SDL_QUIT)
          Done = true;
        if (Event.type == SDL_WINDOWEVENT &&
            Event.window.event == SDL_WINDOWEVENT_CLOSE &&
            Event.window.windowID == SDL_GetWindowID(Window.Get()))
          Done = true;
      }

      // Resize swap chain?
      if (Vulkan.getSwapChainRebuild()) {
        int Width = Window.GetWidth();
        int Height = Window.GetHeight();
        if (Width > 0 && Height > 0) {
          ImGui_ImplVulkan_SetMinImageCount(Vulkan.getMinImageCount());
          ImGui_ImplVulkanH_CreateOrResizeWindow(
              Vulkan.getInstance(), Vulkan.getPhysicalDevice(),
              Vulkan.getDevice(), &Vulkan.getMainWindowData(),
              Vulkan.getQueueFamilyIndex(),
              reinterpret_cast<VkAllocationCallbacks const *>(
                  &Vulkan.getAllocationCallbacks()),
              Width, Height, Vulkan.getMinImageCount());
          Vulkan.getMainWindowData().FrameIndex = 0;
          Vulkan.getSwapChainRebuild() = false;
        }
      }

      // Start the Dear ImGui frame
      ImGui_ImplVulkan_NewFrame();
      ImGui_ImplSDL2_NewFrame();
      ImGui::NewFrame();

      // Show a simple window that we create ourselves. We use a Begin/End
      // pair to created a named window.
      {
        static float F = 0.0f;
        static int Counter = 0;

        ImGui::Begin("Hello, world!"); // Create a window called "Hello, world!"
                                       // and append into it.

        ImGui::Text("This is some useful text."); // Display some text (you can
                                                  // use a format strings too)
        ImGui::SliderFloat(
            "float", &F, 0.0f,
            1.0f); // Edit 1 float using a slider from 0.0f to 1.0f
        ImGui::ColorEdit3(
            "clear color",
            (float *)&ClearColor); // Edit 3 floats representing a color

        if (ImGui::Button(
                "Button")) // Buttons return true when clicked (most widgets
                           // return true when edited/activated)
          Counter++;
        ImGui::SameLine();
        ImGui::Text("counter = %d", Counter);

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                    1000.0f / ImGui::GetIO().Framerate,
                    ImGui::GetIO().Framerate);
        ImGui::End();
      }

      // Rendering
      ImGui::Render();
      ImDrawData *DrawData = ImGui::GetDrawData();
      const bool IsMinimized =
          (DrawData->DisplaySize.x <= 0.0f || DrawData->DisplaySize.y <= 0.0f);
      if (!IsMinimized) {
        Vulkan.getMainWindowData().ClearValue.color.float32[0] =
            ClearColor.x * ClearColor.w;
        Vulkan.getMainWindowData().ClearValue.color.float32[1] =
            ClearColor.y * ClearColor.w;
        Vulkan.getMainWindowData().ClearValue.color.float32[2] =
            ClearColor.z * ClearColor.w;
        Vulkan.getMainWindowData().ClearValue.color.float32[3] = ClearColor.w;
        frameRender(Vulkan, DrawData);
        framePresent(Vulkan);
      }
    }

  } catch (std::exception &E) {
    errsv("Exception occurred: {}", E.what());
    return -1;
  }
}