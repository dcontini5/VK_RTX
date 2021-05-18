/* Copyright (c) 2014-2018, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// ImGui - standalone example application for Glfw + Vulkan, using programmable
// pipeline If you are new to ImGui, see examples/README.txt and documentation
// at the top of imgui.cpp.

#include <array>
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vk.h"


#include "hello_vulkan_Q.h"
#include "nvh/cameramanipulator.hpp"
#include "nvh/fileoperations.hpp"
#include "nvpsystem.hpp"
#include "nvvk/appbase_vkpp.hpp"
#include "nvvk/commands_vk.hpp"
#include "nvvk/context_vk.hpp"


//////////////////////////////////////////////////////////////////////////
#define UNUSED(x) (void)(x)
//////////////////////////////////////////////////////////////////////////

// Default search path for shaders
std::vector<std::string> defaultSearchPaths;


// GLFW Callback functions
static void onErrorCallback(int error, const char* description)
{
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

const char* const items[] = { "1","10","100","1000","10000","100000", "1000000" };

// Extra UI
void renderUI(HelloVulkan& helloVk)
{
  static int item = 1;
  static int MGitem = 6;
  static int SGitem = 6;
  if(ImGui::Combo("Up Vector", &item, "X\0Y\0Z\0\0"))
  {
    nvmath::vec3f pos, eye, up;
    CameraManip.getLookat(pos, eye, up);
    up = nvmath::vec3f(item == 0, item == 1, item == 2) ;
    CameraManip.setLookat(pos, eye, up);
  }
  ImGui::SliderFloat3("Light Position", &helloVk.m_pushConstant.lightPosition.x, -20.f, 20.f);
  ImGui::SliderFloat("Light Intensity", &helloVk.m_pushConstant.lightIntensity, 0.f, 100.f);
  ImGui::SliderFloat("Area Light Radius", &helloVk.mRtPushConstants.areaLightRadius, 0.01f, 100.f);

  ImGui::SliderFloat("Aperture Time", &helloVk.mRtPushConstants.accumulationWeight, 0.001f, 2.0f);
  //ImGui::RadioButton("Point", &helloVk.m_pushConstant.lightType, 0);
  //ImGui::SameLine();
  //ImGui::RadioButton("Infinite", &helloVk.m_pushConstant.lightType, 1);
  //ImGui::SliderFloat("Interpolation Weight", &helloVk.mRtPushConstants.accumulationWeight, 0.001f, 1.0f);
  

  
	if (ImGui::Combo("Mirror Glossiness", &MGitem, items, 7))
  {
		helloVk.mRtPushConstants.mirrorGlossiness = pow(10.f, MGitem);
  }
  if (ImGui::Combo("Sphere Glossiness", &SGitem, items, 7))
  {
	  helloVk.mRtPushConstants.sphereGlossiness = pow(10.f, SGitem);
  }
	
	
  //ImGui::SliderFloat("Mirror Glossiness", &helloVk.mRtPushConstants.mirrorGlossiness, 0.045f, 0.05f, "%.5f");
 // ImGui::SliderFloat("Sphere Glossiness", &helloVk.mRtPushConstants.sphereGlossiness, 0.005f, 0.05f);
  ImGui::SliderInt("Max Depth", &helloVk.mRtPushConstants.maxDepth, 1, 100);
  ImGui::SliderFloat("Aperture Radius", &helloVk.mRtPushConstants.apertureRadius, 0.0001f, 0.1f, "%.4f");
  ImGui::SliderFloat("Focal Length", &helloVk.mRtPushConstants.focalLength, 1.f, 50.f);
  //ImGui::SliderInt("Number of Samples", &helloVk.mRtPushConstants.noOfSamples, 1, 100);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static int const SAMPLE_WIDTH  = 1280;
static int const SAMPLE_HEIGHT = 720;


//--------------------------------------------------------------------------------------------------
// Application Entry
//
int main(int argc, char** argv)
{
  UNUSED(argc);

  // Setup GLFW window
  glfwSetErrorCallback(onErrorCallback);
  if(!glfwInit())
  {
    return 1;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(SAMPLE_WIDTH, SAMPLE_HEIGHT,
                                        "NVIDIA Vulkan Raytracing Tutorial", nullptr, nullptr);


  // Setup camera
  CameraManip.setWindowSize(SAMPLE_WIDTH, SAMPLE_HEIGHT);
  CameraManip.setLookat(nvmath::vec3f(2.0f, 2.0f, 2.0f), nvmath::vec3f(0, 0, 0), nvmath::vec3f(0, 1, 0));
  //CameraManip.setLookat(nvmath::vec3f(10.0f, -5.0f, -16.0f), nvmath::vec3f(0, 0, 0), nvmath::vec3f(0, 1, 0));

  // Setup Vulkan
  if(!glfwVulkanSupported())
  {
    printf("GLFW: Vulkan Not Supported\n");
    return 1;
  }

  // setup some basic things for the sample, logging file for example
  NVPSystem system(argv[0], "VK RTX DISS");

  // Search path for shaders and other media
  defaultSearchPaths = {
  	  "",
      "../",
      NVPSystem::exePath() + std::string("../"),
      NVPSystem::exePath() + std::string("../") + std::string("../"),
  };

  // Enabling the extension feature
  vk::PhysicalDeviceDescriptorIndexingFeaturesEXT indexFeature;
  vk::PhysicalDeviceScalarBlockLayoutFeaturesEXT  scalarFeature;
  vk::PhysicalDeviceRayTracingFeaturesKHR raytracingFeature;

  // Requesting Vulkan extensions and layers
  nvvk::ContextCreateInfo contextInfo(true);
  contextInfo.setVersion(1, 2);
  contextInfo.addInstanceLayer("VK_LAYER_LUNARG_monitor", true);
  contextInfo.addInstanceExtension(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef WIN32
  contextInfo.addInstanceExtension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#else
  contextInfo.addInstanceExtension(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
  contextInfo.addInstanceExtension(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif
  contextInfo.addInstanceExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
  contextInfo.addDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  contextInfo.addDeviceExtension(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
  contextInfo.addDeviceExtension(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
  contextInfo.addDeviceExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
  contextInfo.addDeviceExtension(VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME);
  // #VKRay: Activate the ray tracing extension
  contextInfo.addDeviceExtension(VK_KHR_RAY_TRACING_EXTENSION_NAME, false, &raytracingFeature);
  contextInfo.addDeviceExtension(VK_KHR_MAINTENANCE3_EXTENSION_NAME);
  contextInfo.addDeviceExtension(VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
  contextInfo.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
  contextInfo.addDeviceExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

  //shader Clock extension
  contextInfo.addDeviceExtension(VK_KHR_SHADER_CLOCK_EXTENSION_NAME);
	
  // Creating Vulkan base application
  nvvk::Context vkctx{};
  vkctx.initInstance(contextInfo);
  // Find all compatible devices
  auto compatibleDevices = vkctx.getCompatibleDevices(contextInfo);
  assert(!compatibleDevices.empty());
  // Use a compatible device
  vkctx.initDevice(compatibleDevices[0], contextInfo);

  // Create example
  HelloVulkan helloVk;

  // Window need to be opened to get the surface on which to draw
  const vk::SurfaceKHR surface = helloVk.getVkSurface(vkctx.m_instance, window);
  vkctx.setGCTQueueWithPresent(surface);

  helloVk.setup(vkctx.m_instance, vkctx.m_device, vkctx.m_physicalDevice,
                vkctx.m_queueGCT.familyIndex);
  helloVk.createSurface(surface, SAMPLE_WIDTH, SAMPLE_HEIGHT);
  helloVk.createDepthBuffer();
  helloVk.createRenderPass();
  helloVk.createFrameBuffers();

  // Setup Imgui
  helloVk.initGUI(0);  // Using sub-pass 0

  // Creation of the example

	//Sphere used for furnace test
	//helloVk.loadModel(nvh::findFile("media/scenes/sphere.obj", defaultSearchPaths));
	
	//Multicolor Cube
	helloVk.loadModel(nvh::findFile("media/scenes/cube_multi.obj", defaultSearchPaths));
 	
	
	//Mirrors
	helloVk.loadModel(nvh::findFile("media/scenes/cube.obj", defaultSearchPaths),
                nvmath::translation_mat4(nvmath::vec3(1.f, 1.7f, -5.f))
                    * nvmath::scale_mat4(nvmath::vec3f(10.f, 5.f, .1f)));
	
	helloVk.loadModel(nvh::findFile("media/scenes/cube.obj", defaultSearchPaths),
                nvmath::translation_mat4(nvmath::vec3(1.f, 1.7f, 5.f))
                    * nvmath::scale_mat4(nvmath::vec3f(10.f, 5.f, .1f)));
	
	//Plane
	helloVk.loadModel(nvh::findFile("media/scenes/plane.obj", defaultSearchPaths), 
	  nvmath::translation_mat4(nvmath::vec3f(2.f, -0.5f, 0.f))
	  * nvmath::scale_mat4(nvmath::vec3f(0.5f, 1.f, .5f)));

	//White Wall
	//helloVk.loadModel(nvh::findFile("media/scenes/plane.obj", defaultSearchPaths),
    //nvmath::translation_mat4(nvmath::vec3f(-3.f, 0.1f, 0.f))
	// * nvmath::rotation_mat4_z(3.14f * 1.5f)
	// * nvmath::scale_mat4(nvmath::vec3f(0.2f, 1.f, .27f))
	// );
	
	//Roof
	helloVk.loadModel(nvh::findFile("media/scenes/plane.obj", defaultSearchPaths),
	   nvmath::translation_mat4(nvmath::vec3f(3.f, 4.06f, 1.0f))
	  * nvmath::rotation_mat4_x(3.1415927f)
	  * nvmath::scale_mat4(nvmath::vec3f(0.05f, 1.f, .05f))
	 // * nvmath::scale_mat4(nvmath::vec3f(0.5f, 1.f, .5f))
	 );
	
	//Light
	helloVk.loadModel(nvh::findFile("media/scenes/light.obj", defaultSearchPaths),
	nvmath::translation_mat4(nvmath::vec3(3.f, 4.f, 1.f))
	  * nvmath::scale_mat4(nvmath::vec3f(1.2f, 0.05f, 1.2f)));

	//helloVk.loadModel(nvh::findFile("media/scenes/Donut.obj", defaultSearchPaths),
	//	nvmath::translation_mat4(nvmath::vec3(0.f, 0.f, 0.f))
	//	* nvmath::scale_mat4(nvmath::vec3f(2.0f, 2.0f, 2.0f)));

	
  //using intersection shader
  helloVk.CreateSpheres();

  helloVk.createOffscreenRender();
  helloVk.createDescriptorSetLayout();
  helloVk.createUniformBuffer();
  helloVk.createSceneDescriptionBuffer();
  helloVk.updateDescriptorSet();


  // #VKRay
  helloVk.InitRayTracing();
  helloVk.CreateBottomLevelAS();
  helloVk.CreateTopLevelAS();
  helloVk.CreateRtDescriptorSet();
  //helloVk.CreateRtPipeline();
  helloVk.CreatePtPipeline();
  helloVk.CreateRtShaderBindingTable();

 

	
  helloVk.createPostDescriptor();
  helloVk.createPostPipeline();
  helloVk.updatePostDescriptorSet();

 
	

	nvmath::vec4f clearColor = nvmath::vec4f(0.52, 0.80, 0.92, 1.00f);
  bool          useRayTracer = true;

  helloVk.setupGlfwCallbacks(window);
  ImGui_ImplGlfw_InitForVulkan(window, true);

  // Main loop
  while(!glfwWindowShouldClose(window))
  {
    glfwPollEvents();
    if(helloVk.isMinimized())
      continue;

    // Start the Dear ImGui frame
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Updating camera buffer
    helloVk.updateUniformBuffer();


  	
    // Show UI window.
    if(1 == 1)
    {
      ImGui::ColorEdit3("Clear color", reinterpret_cast<float*>(&clearColor));
      renderUI(helloVk);
      ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                  1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	  
     // ImGui::Checkbox("Ray Tracer Mode", &useRayTracer);
      ImGui::Render();
    }

    // Start rendering the scene
    helloVk.prepareFrame();

    // Start command buffer of this frame
    auto                     curFrame = helloVk.getCurFrame();
    const vk::CommandBuffer& cmdBuff  = helloVk.getCommandBuffers()[curFrame];

    cmdBuff.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    // Clearing screen
    vk::ClearValue clearValues[2];
    clearValues[0].setColor(
        std::array<float, 4>({clearColor[0], clearColor[1], clearColor[2], clearColor[3]}));
    clearValues[1].setDepthStencil({1.0f, 0});

    // Offscreen render pass
    {
      vk::RenderPassBeginInfo offscreenRenderPassBeginInfo;
      offscreenRenderPassBeginInfo.setClearValueCount(2);
      offscreenRenderPassBeginInfo.setPClearValues(clearValues);
      offscreenRenderPassBeginInfo.setRenderPass(helloVk.m_offscreenRenderPass);
      offscreenRenderPassBeginInfo.setFramebuffer(helloVk.m_offscreenFramebuffer);
      offscreenRenderPassBeginInfo.setRenderArea({{}, helloVk.getSize()});

      // Rendering Scene
      

		helloVk.Raytrace(cmdBuff, clearColor);
 
    	
    }


    // 2nd rendering pass: tone mapper, UI
    {
      vk::RenderPassBeginInfo postRenderPassBeginInfo;
      postRenderPassBeginInfo.setClearValueCount(2);
      postRenderPassBeginInfo.setPClearValues(clearValues);
      postRenderPassBeginInfo.setRenderPass(helloVk.getRenderPass());
      postRenderPassBeginInfo.setFramebuffer(helloVk.getFramebuffers()[curFrame]);
      postRenderPassBeginInfo.setRenderArea({{}, helloVk.getSize()});

      cmdBuff.beginRenderPass(postRenderPassBeginInfo, vk::SubpassContents::eInline);
      // Rendering tonemapper
      helloVk.drawPost(cmdBuff);
      // Rendering UI
      ImGui::RenderDrawDataVK(cmdBuff, ImGui::GetDrawData());
      cmdBuff.endRenderPass();
    }

    // Submit for display
    cmdBuff.end();
    helloVk.submitFrame();
  }

  // Cleanup
  helloVk.getDevice().waitIdle();
  helloVk.destroyResources();
  helloVk.destroy();
  vkctx.deinit();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
