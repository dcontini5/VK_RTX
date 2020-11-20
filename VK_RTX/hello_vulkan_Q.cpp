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

#include <random>
#include <sstream>
#include <vulkan/vulkan.hpp>

extern std::vector<std::string> defaultSearchPaths;

#define STB_IMAGE_IMPLEMENTATION
#include "fileformats/stb_image.h"
#include "obj_loader.h"

#include "hello_vulkan_Q.h"
#include "nvh/cameramanipulator.hpp"
#include "nvh/fileoperations.hpp"
#include "nvvk/commands_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/pipeline_vk.hpp"
#include "nvvk/renderpasses_vk.hpp"
#include "nvvk/shaders_vk.hpp"


// Holding the camera matrices
struct CameraMatrices
{
  nvmath::mat4f view;
  nvmath::mat4f proj;
  nvmath::mat4f viewInverse;
  // #VKRay
  nvmath::mat4f projInverse;
};

//--------------------------------------------------------------------------------------------------
// Keep the handle on the device
// Initialize the tool to do all our allocations: buffers, images
//
void HelloVulkan::setup(const vk::Instance&       instance,
                        const vk::Device&         device,
                        const vk::PhysicalDevice& physicalDevice,
                        uint32_t                  queueFamily)
{
  AppBase::setup(instance, device, physicalDevice, queueFamily);
  m_alloc.init(device, physicalDevice);
  m_debug.setup(m_device);
}

//--------------------------------------------------------------------------------------------------
// Called at each frame to update the camera matrix
//
void HelloVulkan::updateUniformBuffer()
{
  const float aspectRatio = m_size.width / static_cast<float>(m_size.height);

  CameraMatrices ubo = {};
  ubo.view           = CameraManip.getMatrix();
  ubo.proj           = nvmath::perspectiveVK(CameraManip.getFov(), aspectRatio, 0.1f, 1000.0f);
  // ubo.proj[1][1] *= -1;  // Inverting Y for Vulkan
  ubo.viewInverse = nvmath::invert(ubo.view);
  // #VKRay
  ubo.projInverse = nvmath::invert(ubo.proj);

  void* data = m_device.mapMemory(m_cameraMat.allocation, 0, sizeof(ubo));
  memcpy(data, &ubo, sizeof(ubo));
  m_device.unmapMemory(m_cameraMat.allocation);
}

//--------------------------------------------------------------------------------------------------
// Describing the layout pushed when rendering
//
void HelloVulkan::createDescriptorSetLayout()
{
  using vkDS     = vk::DescriptorSetLayoutBinding;
  using vkDT     = vk::DescriptorType;
  using vkSS     = vk::ShaderStageFlagBits;
  uint32_t nbTxt = static_cast<uint32_t>(m_textures.size());
  uint32_t nbObj = static_cast<uint32_t>(m_objModel.size());

  // Camera matrices (binding = 0)
  m_descSetLayoutBind.addBinding(
      vkDS(0, vkDT::eUniformBuffer, 1, vkSS::eVertex | vkSS::eRaygenKHR));
  // Materials (binding = 1)
  m_descSetLayoutBind.addBinding(
      vkDS(1, vkDT::eStorageBuffer, nbObj + 1, vkSS::eVertex | vkSS::eFragment | vkSS::eClosestHitKHR));
  // Scene description (binding = 2)
  m_descSetLayoutBind.addBinding(  //
      vkDS(2, vkDT::eStorageBuffer, 1, vkSS::eVertex | vkSS::eFragment | vkSS::eClosestHitKHR));
  // Textures (binding = 3)
  m_descSetLayoutBind.addBinding(
      vkDS(3, vkDT::eCombinedImageSampler, nbTxt, vkSS::eFragment | vkSS::eClosestHitKHR));
  // Materials Index (binding = 4)
  m_descSetLayoutBind.addBinding(
      vkDS(4, vkDT::eStorageBuffer, nbObj + 1, vkSS::eFragment | vkSS::eClosestHitKHR));
  // Storing vertices (binding = 5)
  m_descSetLayoutBind.addBinding(  //
      vkDS(5, vkDT::eStorageBuffer, nbObj, vkSS::eClosestHitKHR));
  // Storing indices (binding = 6)
  m_descSetLayoutBind.addBinding(  //
      vkDS(6, vkDT::eStorageBuffer, nbObj, vkSS::eClosestHitKHR));
	// Storing Spheres (binding = 7)
  m_descSetLayoutBind.addBinding(  //
      vkDS(7, vkDT::eStorageBuffer, 1, vkSS::eClosestHitKHR | vkSS::eIntersectionKHR));


  m_descSetLayout = m_descSetLayoutBind.createLayout(m_device);
  m_descPool      = m_descSetLayoutBind.createPool(m_device, 1);
  m_descSet       = nvvk::allocateDescriptorSet(m_device, m_descPool, m_descSetLayout);
}

//--------------------------------------------------------------------------------------------------
// Setting up the buffers in the descriptor set
//
void HelloVulkan::updateDescriptorSet()
{
  std::vector<vk::WriteDescriptorSet> writes;

  // Camera matrices and scene description
  vk::DescriptorBufferInfo dbiUnif{m_cameraMat.buffer, 0, VK_WHOLE_SIZE};
  writes.emplace_back(m_descSetLayoutBind.makeWrite(m_descSet, 0, &dbiUnif));
  vk::DescriptorBufferInfo dbiSceneDesc{m_sceneDesc.buffer, 0, VK_WHOLE_SIZE};
  writes.emplace_back(m_descSetLayoutBind.makeWrite(m_descSet, 2, &dbiSceneDesc));

  // All material buffers, 1 buffer per OBJ
  std::vector<vk::DescriptorBufferInfo> dbiMat;
  std::vector<vk::DescriptorBufferInfo> dbiMatIdx;
  std::vector<vk::DescriptorBufferInfo> dbiVert;
  std::vector<vk::DescriptorBufferInfo> dbiIdx;
  for(auto& obj : m_objModel)
  {
    dbiMat.emplace_back(obj.matColorBuffer.buffer, 0, VK_WHOLE_SIZE);
    dbiMatIdx.emplace_back(obj.matIndexBuffer.buffer, 0, VK_WHOLE_SIZE);
    dbiVert.emplace_back(obj.vertexBuffer.buffer, 0, VK_WHOLE_SIZE);
    dbiIdx.emplace_back(obj.indexBuffer.buffer, 0, VK_WHOLE_SIZE);
  }
  dbiMat.emplace_back(mSpheresMatColorBuffer.buffer, 0, VK_WHOLE_SIZE);
  dbiMatIdx.emplace_back(mSpheresMatIndexBuffer.buffer, 0, VK_WHOLE_SIZE);
	
  writes.emplace_back(m_descSetLayoutBind.makeWriteArray(m_descSet, 1, dbiMat.data()));
  writes.emplace_back(m_descSetLayoutBind.makeWriteArray(m_descSet, 4, dbiMatIdx.data()));
  writes.emplace_back(m_descSetLayoutBind.makeWriteArray(m_descSet, 5, dbiVert.data()));
  writes.emplace_back(m_descSetLayoutBind.makeWriteArray(m_descSet, 6, dbiIdx.data()));

  vk::DescriptorBufferInfo dbiSpheres{mSpheresBuffer.buffer, 0, VK_WHOLE_SIZE};
  writes.emplace_back(m_descSetLayoutBind.makeWrite(m_descSet, 7, &dbiSpheres));
	
  // All texture samplers
  std::vector<vk::DescriptorImageInfo> diit;
  for(auto& texture : m_textures)
  {
    diit.push_back(texture.descriptor);
  }
  writes.emplace_back(m_descSetLayoutBind.makeWriteArray(m_descSet, 3, diit.data()));

  // Writing the information
  m_device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

//--------------------------------------------------------------------------------------------------
// Creating the pipeline layout
//
void HelloVulkan::createGraphicsPipeline()
{
  using vkSS = vk::ShaderStageFlagBits;

  vk::PushConstantRange pushConstantRanges = {vkSS::eVertex | vkSS::eFragment, 0,
                                              sizeof(ObjPushConstant)};

  // Creating the Pipeline Layout
  vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo;
  vk::DescriptorSetLayout      descSetLayout(m_descSetLayout);
  pipelineLayoutCreateInfo.setSetLayoutCount(1);
  pipelineLayoutCreateInfo.setPSetLayouts(&descSetLayout);
  pipelineLayoutCreateInfo.setPushConstantRangeCount(1);
  pipelineLayoutCreateInfo.setPPushConstantRanges(&pushConstantRanges);
  m_pipelineLayout = m_device.createPipelineLayout(pipelineLayoutCreateInfo);

  // Creating the Pipeline
  std::vector<std::string>                paths = defaultSearchPaths;
  nvvk::GraphicsPipelineGeneratorCombined gpb(m_device, m_pipelineLayout, m_offscreenRenderPass);
  gpb.depthStencilState.depthTestEnable = true;
  gpb.addShader(nvh::loadFile("shaders/vert_shader.vert.spv", true, paths), vkSS::eVertex);
  gpb.addShader(nvh::loadFile("shaders/frag_shader.frag.spv", true, paths), vkSS::eFragment);
  gpb.addBindingDescription({0, sizeof(VertexObj)});
  gpb.addAttributeDescriptions(std::vector<vk::VertexInputAttributeDescription>{
      {0, 0, vk::Format::eR32G32B32Sfloat, offsetof(VertexObj, pos)},
      {1, 0, vk::Format::eR32G32B32Sfloat, offsetof(VertexObj, nrm)},
      {2, 0, vk::Format::eR32G32B32Sfloat, offsetof(VertexObj, color)},
      {3, 0, vk::Format::eR32G32Sfloat, offsetof(VertexObj, texCoord)}});

  m_graphicsPipeline = gpb.createPipeline();
  m_debug.setObjectName(m_graphicsPipeline, "Graphics");
}

//--------------------------------------------------------------------------------------------------
// Loading the OBJ file and setting up all buffers
//
void HelloVulkan::loadModel(const std::string& filename, nvmath::mat4f transform)
{
  using vkBU = vk::BufferUsageFlagBits;

  ObjLoader loader;
  loader.loadModel(filename);

  // Converting from Srgb to linear
  for(auto& m : loader.m_materials)
  {
    m.ambient  = nvmath::pow(m.ambient, 2.2f);
    m.diffuse  = nvmath::pow(m.diffuse, 2.2f);
    m.specular = nvmath::pow(m.specular, 2.2f);
  }

  ObjInstance instance;
  instance.objIndex    = static_cast<uint32_t>(m_objModel.size());
  instance.transform   = transform;
  instance.transformIT = nvmath::transpose(nvmath::invert(transform));
  instance.txtOffset   = static_cast<uint32_t>(m_textures.size());

  ObjModel model;
  model.nbIndices  = static_cast<uint32_t>(loader.m_indices.size());
  model.nbVertices = static_cast<uint32_t>(loader.m_vertices.size());

  // Create the buffers on Device and copy vertices, indices and materials
  nvvk::CommandPool cmdBufGet(m_device, m_graphicsQueueIndex);
  vk::CommandBuffer cmdBuf = cmdBufGet.createCommandBuffer();
  model.vertexBuffer =
      m_alloc.createBuffer(cmdBuf, loader.m_vertices,
                           vkBU::eVertexBuffer | vkBU::eStorageBuffer | vkBU::eShaderDeviceAddress);
  model.indexBuffer =
      m_alloc.createBuffer(cmdBuf, loader.m_indices,
                           vkBU::eIndexBuffer | vkBU::eStorageBuffer | vkBU::eShaderDeviceAddress);
  model.matColorBuffer = m_alloc.createBuffer(cmdBuf, loader.m_materials, vkBU::eStorageBuffer);
  model.matIndexBuffer = m_alloc.createBuffer(cmdBuf, loader.m_matIndx, vkBU::eStorageBuffer);
  // Creates all textures found
  createTextureImages(cmdBuf, loader.m_textures);
  cmdBufGet.submitAndWait(cmdBuf);
  m_alloc.finalizeAndReleaseStaging();

  std::string objNb = std::to_string(instance.objIndex);
  m_debug.setObjectName(model.vertexBuffer.buffer, (std::string("vertex_" + objNb).c_str()));
  m_debug.setObjectName(model.indexBuffer.buffer, (std::string("index_" + objNb).c_str()));
  m_debug.setObjectName(model.matColorBuffer.buffer, (std::string("mat_" + objNb).c_str()));
  m_debug.setObjectName(model.matIndexBuffer.buffer, (std::string("matIdx_" + objNb).c_str()));

  m_objModel.emplace_back(model);
  m_objInstance.emplace_back(instance);
}

//--------------------------------------------------------------------------------------------------
// Creating the uniform buffer holding the camera matrices
// - Buffer is host visible
//
void HelloVulkan::createUniformBuffer()
{
  using vkBU = vk::BufferUsageFlagBits;
  using vkMP = vk::MemoryPropertyFlagBits;

  m_cameraMat = m_alloc.createBuffer(sizeof(CameraMatrices), vkBU::eUniformBuffer,
                                     vkMP::eHostVisible | vkMP::eHostCoherent);
  m_debug.setObjectName(m_cameraMat.buffer, "cameraMat");
}

//--------------------------------------------------------------------------------------------------
// Create a storage buffer containing the description of the scene elements
// - Which geometry is used by which instance
// - Transformation
// - Offset for texture
//
void HelloVulkan::createSceneDescriptionBuffer()
{
  using vkBU = vk::BufferUsageFlagBits;
  nvvk::CommandPool cmdGen(m_device, m_graphicsQueueIndex);

  auto cmdBuf = cmdGen.createCommandBuffer();
  m_sceneDesc = m_alloc.createBuffer(cmdBuf, m_objInstance, vkBU::eStorageBuffer);
  cmdGen.submitAndWait(cmdBuf);
  m_alloc.finalizeAndReleaseStaging();
  m_debug.setObjectName(m_sceneDesc.buffer, "sceneDesc");
}

//--------------------------------------------------------------------------------------------------
// Creating all textures and samplers
//
void HelloVulkan::createTextureImages(const vk::CommandBuffer&        cmdBuf,
                                      const std::vector<std::string>& textures)
{
  using vkIU = vk::ImageUsageFlagBits;

  vk::SamplerCreateInfo samplerCreateInfo{
      {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear};
  samplerCreateInfo.setMaxLod(FLT_MAX);
  vk::Format format = vk::Format::eR8G8B8A8Srgb;

  // If no textures are present, create a dummy one to accommodate the pipeline layout
  if(textures.empty() && m_textures.empty())
  {
    nvvk::Texture texture;

    std::array<uint8_t, 4> color{255u, 255u, 255u, 255u};
    vk::DeviceSize         bufferSize      = sizeof(color);
    auto                   imgSize         = vk::Extent2D(1, 1);
    auto                   imageCreateInfo = nvvk::makeImage2DCreateInfo(imgSize, format);

    // Creating the dummy texure
    nvvk::Image image = m_alloc.createImage(cmdBuf, bufferSize, color.data(), imageCreateInfo);
    vk::ImageViewCreateInfo ivInfo = nvvk::makeImageViewCreateInfo(image.image, imageCreateInfo);
    texture                        = m_alloc.createTexture(image, ivInfo, samplerCreateInfo);

    // The image format must be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    nvvk::cmdBarrierImageLayout(cmdBuf, texture.image, vk::ImageLayout::eUndefined,
                                vk::ImageLayout::eShaderReadOnlyOptimal);
    m_textures.push_back(texture);
  }
  else
  {
    // Uploading all images
    for(const auto& texture : textures)
    {
      std::stringstream o;
      int               texWidth, texHeight, texChannels;
      o << "media/textures/" << texture;
      std::string txtFile = nvh::findFile(o.str(), defaultSearchPaths);

      stbi_uc* pixels =
          stbi_load(txtFile.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

      // Handle failure
      if(!pixels)
      {
        texWidth = texHeight = 1;
        texChannels          = 4;
        std::array<uint8_t, 4> color{255u, 0u, 255u, 255u};
        pixels = reinterpret_cast<stbi_uc*>(color.data());
      }

      vk::DeviceSize bufferSize = static_cast<uint64_t>(texWidth) * texHeight * sizeof(uint8_t) * 4;
      auto           imgSize    = vk::Extent2D(texWidth, texHeight);
      auto imageCreateInfo = nvvk::makeImage2DCreateInfo(imgSize, format, vkIU::eSampled, true);

      {
        nvvk::ImageDedicated image =
            m_alloc.createImage(cmdBuf, bufferSize, pixels, imageCreateInfo);
        nvvk::cmdGenerateMipmaps(cmdBuf, image.image, format, imgSize, imageCreateInfo.mipLevels);
        vk::ImageViewCreateInfo ivInfo =
            nvvk::makeImageViewCreateInfo(image.image, imageCreateInfo);
        nvvk::Texture texture = m_alloc.createTexture(image, ivInfo, samplerCreateInfo);

        m_textures.push_back(texture);
      }
    }
  }
}

//--------------------------------------------------------------------------------------------------
// Destroying all allocations
//
void HelloVulkan::destroyResources()
{
  m_device.destroy(m_graphicsPipeline);
  m_device.destroy(m_pipelineLayout);
  m_device.destroy(m_descPool);
  m_device.destroy(m_descSetLayout);
  m_alloc.destroy(m_cameraMat);
  m_alloc.destroy(m_sceneDesc);


  for(auto& m : m_objModel)
  {
    m_alloc.destroy(m.vertexBuffer);
    m_alloc.destroy(m.indexBuffer);
    m_alloc.destroy(m.matColorBuffer);
    m_alloc.destroy(m.matIndexBuffer);
  }

  for(auto& t : m_textures)
  {
    m_alloc.destroy(t);
  }

  //#Post
  m_device.destroy(m_postPipeline);
  m_device.destroy(m_postPipelineLayout);
  m_device.destroy(m_postDescPool);
  m_device.destroy(m_postDescSetLayout);
  m_alloc.destroy(m_offscreenColor);
  m_alloc.destroy(m_offscreenDepth);
  m_device.destroy(m_offscreenRenderPass);
  m_device.destroy(m_offscreenFramebuffer);

  //#VKRay

  mRtBuilder.destroy();
  m_device.destroy(mRtDescriptorPool);
  m_device.destroy(mRtDescriptorSetLayout);
  m_device.destroy(mRtPipeline);
  m_device.destroy(mRtPipelineLayout);

  m_device.destroy(mPtPipeline);

	
  m_alloc.destroy(mRtSBTBuffer);

  m_alloc.destroy(mSpheresBuffer);
  m_alloc.destroy(mSpheresAabbBuffer);
  m_alloc.destroy(mSpheresMatIndexBuffer);
  m_alloc.destroy(mSpheresMatColorBuffer);


}

//--------------------------------------------------------------------------------------------------
// Drawing the scene in raster mode
//
void HelloVulkan::rasterize(const vk::CommandBuffer& cmdBuf)
{
  using vkPBP = vk::PipelineBindPoint;
  using vkSS  = vk::ShaderStageFlagBits;
  vk::DeviceSize offset{0};

  m_debug.beginLabel(cmdBuf, "Rasterize");

  // Dynamic Viewport
  cmdBuf.setViewport(0, {vk::Viewport(0, 0, (float)m_size.width, (float)m_size.height, 0, 1)});
  cmdBuf.setScissor(0, {{{0, 0}, {m_size.width, m_size.height}}});

  // Drawing all triangles
  cmdBuf.bindPipeline(vkPBP::eGraphics, m_graphicsPipeline);
  cmdBuf.bindDescriptorSets(vkPBP::eGraphics, m_pipelineLayout, 0, {m_descSet}, {});
  for(int i = 0; i < m_objInstance.size(); ++i)
  {
    auto& inst                = m_objInstance[i];
    auto& model               = m_objModel[inst.objIndex];
    m_pushConstant.instanceId = i;  // Telling which instance is drawn
    cmdBuf.pushConstants<ObjPushConstant>(m_pipelineLayout, vkSS::eVertex | vkSS::eFragment, 0,
                                          m_pushConstant);

    cmdBuf.bindVertexBuffers(0, {model.vertexBuffer.buffer}, {offset});
    cmdBuf.bindIndexBuffer(model.indexBuffer.buffer, 0, vk::IndexType::eUint32);
    cmdBuf.drawIndexed(model.nbIndices, 1, 0, 0, 0);
  }
  m_debug.endLabel(cmdBuf);
}

//--------------------------------------------------------------------------------------------------
// Handling resize of the window
//
void HelloVulkan::onResize(int /*w*/, int /*h*/)
{
  createOffscreenRender();
  updatePostDescriptorSet();
  UpdateRtDescriptorSet();
}

//////////////////////////////////////////////////////////////////////////
// Post-processing
//////////////////////////////////////////////////////////////////////////

//--------------------------------------------------------------------------------------------------
// Creating an offscreen frame buffer and the associated render pass
//
void HelloVulkan::createOffscreenRender()
{
  m_alloc.destroy(m_offscreenColor);
  m_alloc.destroy(m_offscreenDepth);

  // Creating the color image
  {
    auto colorCreateInfo = nvvk::makeImage2DCreateInfo(m_size, m_offscreenColorFormat,
                                                       vk::ImageUsageFlagBits::eColorAttachment
                                                           | vk::ImageUsageFlagBits::eSampled
                                                           | vk::ImageUsageFlagBits::eStorage);


    nvvk::Image             image  = m_alloc.createImage(colorCreateInfo);
    vk::ImageViewCreateInfo ivInfo = nvvk::makeImageViewCreateInfo(image.image, colorCreateInfo);
    m_offscreenColor               = m_alloc.createTexture(image, ivInfo, vk::SamplerCreateInfo());
    m_offscreenColor.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  }

  // Creating the depth buffer
  auto depthCreateInfo =
      nvvk::makeImage2DCreateInfo(m_size, m_offscreenDepthFormat,
                                  vk::ImageUsageFlagBits::eDepthStencilAttachment);
  {
    nvvk::Image image = m_alloc.createImage(depthCreateInfo);

    vk::ImageViewCreateInfo depthStencilView;
    depthStencilView.setViewType(vk::ImageViewType::e2D);
    depthStencilView.setFormat(m_offscreenDepthFormat);
    depthStencilView.setSubresourceRange({vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1});
    depthStencilView.setImage(image.image);

    m_offscreenDepth = m_alloc.createTexture(image, depthStencilView);
  }

  // Setting the image layout for both color and depth
  {
    nvvk::CommandPool genCmdBuf(m_device, m_graphicsQueueIndex);
    auto              cmdBuf = genCmdBuf.createCommandBuffer();
    nvvk::cmdBarrierImageLayout(cmdBuf, m_offscreenColor.image, vk::ImageLayout::eUndefined,
                                vk::ImageLayout::eGeneral);
    nvvk::cmdBarrierImageLayout(cmdBuf, m_offscreenDepth.image, vk::ImageLayout::eUndefined,
                                vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                vk::ImageAspectFlagBits::eDepth);

    genCmdBuf.submitAndWait(cmdBuf);
  }

  // Creating a renderpass for the offscreen
  if(!m_offscreenRenderPass)
  {
    m_offscreenRenderPass =
        nvvk::createRenderPass(m_device, {m_offscreenColorFormat}, m_offscreenDepthFormat, 1, true,
                               true, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral);
  }

  // Creating the frame buffer for offscreen
  std::vector<vk::ImageView> attachments = {m_offscreenColor.descriptor.imageView,
                                            m_offscreenDepth.descriptor.imageView};

  m_device.destroy(m_offscreenFramebuffer);
  vk::FramebufferCreateInfo info;
  info.setRenderPass(m_offscreenRenderPass);
  info.setAttachmentCount(2);
  info.setPAttachments(attachments.data());
  info.setWidth(m_size.width);
  info.setHeight(m_size.height);
  info.setLayers(1);
  m_offscreenFramebuffer = m_device.createFramebuffer(info);
}

//--------------------------------------------------------------------------------------------------
// The pipeline is how things are rendered, which shaders, type of primitives, depth test and more
//
void HelloVulkan::createPostPipeline()
{
  // Push constants in the fragment shader
  vk::PushConstantRange pushConstantRanges = {vk::ShaderStageFlagBits::eFragment, 0, sizeof(float)};

  // Creating the pipeline layout
  vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo;
  pipelineLayoutCreateInfo.setSetLayoutCount(1);
  pipelineLayoutCreateInfo.setPSetLayouts(&m_postDescSetLayout);
  pipelineLayoutCreateInfo.setPushConstantRangeCount(1);
  pipelineLayoutCreateInfo.setPPushConstantRanges(&pushConstantRanges);
  m_postPipelineLayout = m_device.createPipelineLayout(pipelineLayoutCreateInfo);

  // Pipeline: completely generic, no vertices
  std::vector<std::string> paths = defaultSearchPaths;

  nvvk::GraphicsPipelineGeneratorCombined pipelineGenerator(m_device, m_postPipelineLayout,
                                                            m_renderPass);
  pipelineGenerator.addShader(nvh::loadFile("shaders/passthrough.vert.spv", true, paths),
                              vk::ShaderStageFlagBits::eVertex);
  pipelineGenerator.addShader(nvh::loadFile("shaders/post.frag.spv", true, paths),
                              vk::ShaderStageFlagBits::eFragment);
  pipelineGenerator.rasterizationState.setCullMode(vk::CullModeFlagBits::eNone);
  m_postPipeline = pipelineGenerator.createPipeline();
  m_debug.setObjectName(m_postPipeline, "post");
}

//--------------------------------------------------------------------------------------------------
// The descriptor layout is the description of the data that is passed to the vertex or the
// fragment program.
//
void HelloVulkan::createPostDescriptor()
{
  using vkDS = vk::DescriptorSetLayoutBinding;
  using vkDT = vk::DescriptorType;
  using vkSS = vk::ShaderStageFlagBits;

  m_postDescSetLayoutBind.addBinding(vkDS(0, vkDT::eCombinedImageSampler, 1, vkSS::eFragment));
  m_postDescSetLayout = m_postDescSetLayoutBind.createLayout(m_device);
  m_postDescPool      = m_postDescSetLayoutBind.createPool(m_device);
  m_postDescSet       = nvvk::allocateDescriptorSet(m_device, m_postDescPool, m_postDescSetLayout);
}

//--------------------------------------------------------------------------------------------------
// Update the output
//
void HelloVulkan::updatePostDescriptorSet()
{
  vk::WriteDescriptorSet writeDescriptorSets =
      m_postDescSetLayoutBind.makeWrite(m_postDescSet, 0, &m_offscreenColor.descriptor);
  m_device.updateDescriptorSets(writeDescriptorSets, nullptr);
}

//--------------------------------------------------------------------------------------------------
// Draw a full screen quad with the attached image
//
void HelloVulkan::drawPost(vk::CommandBuffer cmdBuf)
{
  m_debug.beginLabel(cmdBuf, "Post");

  cmdBuf.setViewport(0, {vk::Viewport(0, 0, (float)m_size.width, (float)m_size.height, 0, 1)});
  cmdBuf.setScissor(0, {{{0, 0}, {m_size.width, m_size.height}}});

  auto aspectRatio = static_cast<float>(m_size.width) / static_cast<float>(m_size.height);
  cmdBuf.pushConstants<float>(m_postPipelineLayout, vk::ShaderStageFlagBits::eFragment, 0,
                              aspectRatio);
  cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, m_postPipeline);
  cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_postPipelineLayout, 0,
                            m_postDescSet, {});
  cmdBuf.draw(3, 1, 0, 0);

  m_debug.endLabel(cmdBuf);
}

//--------------------------------------------------------------------------------------------------
// Ray Tracing initialization
// #VKRay
void HelloVulkan::InitRayTracing()
{

  //request ray tracing properties
  auto properties = m_physicalDevice.getProperties2<vk::PhysicalDeviceProperties2,
                                                    vk::PhysicalDeviceRayTracingPropertiesKHR>();

  mRtProperties = properties.get<vk::PhysicalDeviceRayTracingPropertiesKHR>();

  mRtBuilder.setup(m_device, &m_alloc, m_graphicsQueueIndex);
}


void HelloVulkan::CreateBottomLevelAS()
{

  std::vector<nvvk::RaytracingBuilderKHR::Blas> allBlas;
  allBlas.reserve(m_objModel.size());

  for(const auto& obj : m_objModel)
  {

    auto blas = ObjectToGeometryKHR(obj);

    allBlas.emplace_back(blas);
  }

  //spheres
  {
  	
    auto blas = SphereToVkGeometryKHR();
    allBlas.emplace_back(blas);
  	
  }

	
  mRtBuilder.buildBlas(allBlas, vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace);
}

void HelloVulkan::CreateTopLevelAS(){

  std::vector<nvvk::RaytracingBuilderKHR::Instance> tlas;
  tlas.reserve(m_objInstance.size());
  for(auto i = 0; i < static_cast<int>(m_objInstance.size()); i++)
  {

  	
    nvvk::RaytracingBuilderKHR::Instance rayInst;
    rayInst.transform  = m_objInstance[i].transform;
    rayInst.instanceId = i;
    rayInst.blasId     = m_objInstance[i].objIndex;
    rayInst.hitGroupId = 0;
    //rayInst.flags      = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
    rayInst.flags      = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    tlas.emplace_back(rayInst);
  	
  }


	//creating implicit geometry instances
  {

    nvvk::RaytracingBuilderKHR::Instance rayInst;
    rayInst.transform  = m_objInstance[0].transform;
    rayInst.instanceId = static_cast<uint32_t>(tlas.size());
    rayInst.blasId     = static_cast<uint32_t>(m_objModel.size());
    rayInst.hitGroupId = 1;
    rayInst.flags      = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    tlas.emplace_back(rayInst);
  	
  }


  mRtBuilder.buildTlas(tlas, vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace);
}

void HelloVulkan::CreateRtDescriptorSet()
{

  using vkDT   = vk::DescriptorType;
  using vkSS   = vk::ShaderStageFlagBits;
  using vkDSLB = vk::DescriptorSetLayoutBinding;


  mRtDescriptorSetBindings.addBinding(
      vkDSLB(0, vkDT::eAccelerationStructureKHR, 1, vkSS::eRaygenKHR | vkSS::eClosestHitKHR));

  mRtDescriptorSetBindings.addBinding(vkDSLB(1, vkDT::eStorageImage, 1, vkSS::eRaygenKHR));

  mRtDescriptorPool      = mRtDescriptorSetBindings.createPool(m_device);
  mRtDescriptorSetLayout = mRtDescriptorSetBindings.createLayout(m_device);
  mRtDescriptorSet =
      m_device.allocateDescriptorSets({mRtDescriptorPool, 1, &mRtDescriptorSetLayout})[0];

  vk::AccelerationStructureKHR                   tlas = mRtBuilder.getAccelerationStructure();
  vk::WriteDescriptorSetAccelerationStructureKHR descASInfo;
  descASInfo.setAccelerationStructureCount(1);
  descASInfo.setPAccelerationStructures(&tlas);
  vk::DescriptorImageInfo imageInfo{
      {}, m_offscreenColor.descriptor.imageView, vk::ImageLayout::eGeneral};

  std::vector<vk::WriteDescriptorSet> writes;
  writes.emplace_back(mRtDescriptorSetBindings.makeWrite(mRtDescriptorSet, 0, &descASInfo));
  writes.emplace_back(mRtDescriptorSetBindings.makeWrite(mRtDescriptorSet, 1, &imageInfo));

  m_device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void HelloVulkan::UpdateRtDescriptorSet()
{

  using vkDT = vk::DescriptorType;

  // (1) Output buffer
  vk::DescriptorImageInfo imageInfo{
      {}, m_offscreenColor.descriptor.imageView, vk::ImageLayout::eGeneral};
  vk::WriteDescriptorSet wds{mRtDescriptorSet, 1, 0, 1, vkDT::eStorageImage, &imageInfo};
  m_device.updateDescriptorSets(wds, nullptr);
	
}

void HelloVulkan::CreateRtPipeline()
{

  std::vector<std::string> paths = defaultSearchPaths;

  vk::ShaderModule raygenSM =
      nvvk::createShaderModule(m_device,  //
                               nvh::loadFile("shaders/pathtrace.rgen.spv", true, paths));
  vk::ShaderModule missSM =
      nvvk::createShaderModule(m_device,  //
                               nvh::loadFile("shaders/raytrace.rmiss.spv", true, paths));

 vk::ShaderModule shadowMissSM =
     nvvk::createShaderModule(m_device,  //
                              nvh::loadFile("shaders/raytraceShadow.rmiss.spv", true, paths));

  std::vector<vk::PipelineShaderStageCreateInfo> stages;

	//raygen
  vk::RayTracingShaderGroupCreateInfoKHR rg{
      vk::RayTracingShaderGroupTypeKHR::eGeneral,
      VK_SHADER_UNUSED_KHR,
      VK_SHADER_UNUSED_KHR,
      VK_SHADER_UNUSED_KHR,
      VK_SHADER_UNUSED_KHR,
  };

  stages.push_back({{}, vk::ShaderStageFlagBits::eRaygenKHR, raygenSM, "main"});
  rg.setGeneralShader(static_cast<uint32_t>(stages.size() - 1));
  mRtShaderGroups.push_back(rg);

	//raymiss
  vk::RayTracingShaderGroupCreateInfoKHR mg{
      vk::RayTracingShaderGroupTypeKHR::eGeneral,
      VK_SHADER_UNUSED_KHR,
      VK_SHADER_UNUSED_KHR,
      VK_SHADER_UNUSED_KHR,
      VK_SHADER_UNUSED_KHR,
  };

  stages.push_back({{}, vk::ShaderStageFlagBits::eMissKHR, missSM, "main"});
  mg.setGeneralShader(static_cast<uint32_t>(stages.size() - 1));
  mRtShaderGroups.push_back(mg);
	
  stages.push_back({{}, vk::ShaderStageFlagBits::eMissKHR, shadowMissSM, "main"});
  mg.setGeneralShader(static_cast<uint32_t>(stages.size() - 1));
  mRtShaderGroups.push_back(mg);

  //hit group 0
  vk::ShaderModule chitSM =
      nvvk::createShaderModule(m_device, nvh::loadFile("shaders/pathtrace.rchit.spv", true, paths));
  {
    vk::RayTracingShaderGroupCreateInfoKHR hg{
        vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
        VK_SHADER_UNUSED_KHR,
        VK_SHADER_UNUSED_KHR,
        VK_SHADER_UNUSED_KHR,
        VK_SHADER_UNUSED_KHR,
    };

  	  stages.push_back({{}, vk::ShaderStageFlagBits::eClosestHitKHR, chitSM, "main"});
    hg.setClosestHitShader(static_cast<uint32_t>(stages.size() - 1));
    mRtShaderGroups.push_back(hg);
  }




  //hit group 1
  vk::ShaderModule chit2SM =
      nvvk::createShaderModule(m_device, nvh::loadFile("shaders/raytrace2.rchit.spv", true, paths));
  
  vk::ShaderModule rintSM =
      nvvk::createShaderModule(m_device, nvh::loadFile("shaders/raytrace.rint.spv", true, paths));

  {
    vk::RayTracingShaderGroupCreateInfoKHR hg{
        vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup,
        VK_SHADER_UNUSED_KHR,
        VK_SHADER_UNUSED_KHR,
        VK_SHADER_UNUSED_KHR,
        VK_SHADER_UNUSED_KHR,
    };

    stages.push_back({{}, vk::ShaderStageFlagBits::eClosestHitKHR, chit2SM, "main"});
    hg.setClosestHitShader(static_cast<uint32_t>(stages.size() - 1));

    stages.push_back({{}, vk::ShaderStageFlagBits::eIntersectionKHR, rintSM, "main"});
    hg.setIntersectionShader(static_cast<uint32_t>(stages.size() - 1));

    mRtShaderGroups.push_back(hg);
  	
  }



	
  vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo;

  vk::PushConstantRange pushConstants{vk::ShaderStageFlagBits::eRaygenKHR
                                          | vk::ShaderStageFlagBits::eClosestHitKHR
                                          | vk::ShaderStageFlagBits::eMissKHR,
                                      0, sizeof(RtPushConstants)};


  pipelineLayoutCreateInfo.setPushConstantRangeCount(1);
  pipelineLayoutCreateInfo.setPPushConstantRanges(&pushConstants);

  std::vector<vk::DescriptorSetLayout> rtDescrSetLayouts = {mRtDescriptorSetLayout,
                                                            m_descSetLayout};
	
  pipelineLayoutCreateInfo.setLayoutCount = (static_cast<uint32_t>(rtDescrSetLayouts.size()));
  pipelineLayoutCreateInfo.setPSetLayouts(rtDescrSetLayouts.data());

  mRtPipelineLayout = m_device.createPipelineLayout(pipelineLayoutCreateInfo);

  vk::RayTracingPipelineCreateInfoKHR rayTracingPipelineCreateInfo;

  rayTracingPipelineCreateInfo.setStageCount(static_cast<uint32_t>(stages.size()));
  rayTracingPipelineCreateInfo.setPStages(stages.data());

  rayTracingPipelineCreateInfo.setGroupCount(static_cast<uint32_t>(mRtShaderGroups.size()));
  rayTracingPipelineCreateInfo.setPGroups(mRtShaderGroups.data());

  rayTracingPipelineCreateInfo.setMaxRecursionDepth(2); //Ray Depth
  rayTracingPipelineCreateInfo.setLayout(mRtPipelineLayout);
  mRtPipeline = static_cast<const vk::Pipeline&>(
      m_device.createRayTracingPipelineKHR({}, rayTracingPipelineCreateInfo));

  m_device.destroy(raygenSM);
  m_device.destroy(missSM);
  m_device.destroy(shadowMissSM);
  m_device.destroy(chitSM);
  m_device.destroy(chit2SM);
  m_device.destroy(rintSM);
}

void HelloVulkan::CreatePtPipeline(){

	std::vector<std::string> paths = defaultSearchPaths;

	vk::ShaderModule raygenSM =
		nvvk::createShaderModule(m_device,  //
			nvh::loadFile("shaders/pathtrace.rgen.spv", true, paths));
	vk::ShaderModule missSM =
		nvvk::createShaderModule(m_device,  //
			nvh::loadFile("shaders/raytrace.rmiss.spv", true, paths));

	vk::ShaderModule shadowMissSM =
		nvvk::createShaderModule(m_device,  //
			nvh::loadFile("shaders/raytraceShadow.rmiss.spv", true, paths));

	std::vector<vk::PipelineShaderStageCreateInfo> stages;

	//raygen
	vk::RayTracingShaderGroupCreateInfoKHR rg{
		vk::RayTracingShaderGroupTypeKHR::eGeneral,
		VK_SHADER_UNUSED_KHR,
		VK_SHADER_UNUSED_KHR,
		VK_SHADER_UNUSED_KHR,
		VK_SHADER_UNUSED_KHR,
	};

	stages.push_back({ {}, vk::ShaderStageFlagBits::eRaygenKHR, raygenSM, "main" });
	rg.setGeneralShader(static_cast<uint32_t>(stages.size() - 1));
	mPtShaderGroups.push_back(rg);

	//raymiss
	vk::RayTracingShaderGroupCreateInfoKHR mg{
		vk::RayTracingShaderGroupTypeKHR::eGeneral,
		VK_SHADER_UNUSED_KHR,
		VK_SHADER_UNUSED_KHR,
		VK_SHADER_UNUSED_KHR,
		VK_SHADER_UNUSED_KHR,
	};

	stages.push_back({ {}, vk::ShaderStageFlagBits::eMissKHR, missSM, "main" });
	mg.setGeneralShader(static_cast<uint32_t>(stages.size() - 1));
	mPtShaderGroups.push_back(mg);

	stages.push_back({ {}, vk::ShaderStageFlagBits::eMissKHR, shadowMissSM, "main" });
	mg.setGeneralShader(static_cast<uint32_t>(stages.size() - 1));
	mPtShaderGroups.push_back(mg);

	//hit group 0
	vk::ShaderModule chitSM =
		nvvk::createShaderModule(m_device, nvh::loadFile("shaders/raytrace.rchit.spv", true, paths));
	{
		vk::RayTracingShaderGroupCreateInfoKHR hg{
			vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
			VK_SHADER_UNUSED_KHR,
			VK_SHADER_UNUSED_KHR,
			VK_SHADER_UNUSED_KHR,
			VK_SHADER_UNUSED_KHR,
		};

		stages.push_back({ {}, vk::ShaderStageFlagBits::eClosestHitKHR, chitSM, "main" });
		hg.setClosestHitShader(static_cast<uint32_t>(stages.size() - 1));
		mPtShaderGroups.push_back(hg);
	}




	//hit group 1
	vk::ShaderModule chit2SM =
		nvvk::createShaderModule(m_device, nvh::loadFile("shaders/raytrace2.rchit.spv", true, paths));

	vk::ShaderModule rintSM =
		nvvk::createShaderModule(m_device, nvh::loadFile("shaders/raytrace.rint.spv", true, paths));

	{
		vk::RayTracingShaderGroupCreateInfoKHR hg{
			vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup,
			VK_SHADER_UNUSED_KHR,
			VK_SHADER_UNUSED_KHR,
			VK_SHADER_UNUSED_KHR,
			VK_SHADER_UNUSED_KHR,
		};

		stages.push_back({ {}, vk::ShaderStageFlagBits::eClosestHitKHR, chit2SM, "main" });
		hg.setClosestHitShader(static_cast<uint32_t>(stages.size() - 1));

		stages.push_back({ {}, vk::ShaderStageFlagBits::eIntersectionKHR, rintSM, "main" });
		hg.setIntersectionShader(static_cast<uint32_t>(stages.size() - 1));

		mPtShaderGroups.push_back(hg);

	}
	

	vk::RayTracingPipelineCreateInfoKHR rayTracingPipelineCreateInfo;

	rayTracingPipelineCreateInfo.setStageCount(static_cast<uint32_t>(stages.size()));
	rayTracingPipelineCreateInfo.setPStages(stages.data());

	rayTracingPipelineCreateInfo.setGroupCount(static_cast<uint32_t>(mPtShaderGroups.size()));
	rayTracingPipelineCreateInfo.setPGroups(mPtShaderGroups.data());

	rayTracingPipelineCreateInfo.setMaxRecursionDepth(2); //Ray Depth
	rayTracingPipelineCreateInfo.setLayout(mRtPipelineLayout);
	mPtPipeline = static_cast<const vk::Pipeline&>(
		m_device.createRayTracingPipelineKHR({}, rayTracingPipelineCreateInfo));

	m_device.destroy(raygenSM);
	m_device.destroy(missSM);
	m_device.destroy(shadowMissSM);
	m_device.destroy(chitSM);
	m_device.destroy(chit2SM);
	m_device.destroy(rintSM);

	
}

void HelloVulkan::UpdateFrame(){

	static nvmath::mat4f refCamMatrix;
	static nvmath::vec3  refLight;

	auto& m = CameraManip.getMatrix();
	if(memcmp(&refCamMatrix.a00, &m.a00, sizeof(nvmath::mat4f)) !=0 || mRtPushConstants.lightPosition != refLight ){

		ResetFrame();
		refCamMatrix = m;
		refLight = mRtPushConstants.lightPosition;
		
	}

	mRtPushConstants.frame++;
	
}

void HelloVulkan::ResetFrame(){

	mRtPushConstants.frame = -1;
	
	
}

void HelloVulkan::CreateRtShaderBindingTable()
{

  auto     groupCount      = static_cast<uint32_t>(mRtShaderGroups.size());
  uint32_t groupHandleSize = mRtProperties.shaderGroupHandleSize;
  uint32_t baseAlignment   = mRtProperties.shaderGroupBaseAlignment;

  uint32_t sbtSize = groupCount * baseAlignment;

  std::vector<uint8_t> shaderHandleStorage(sbtSize);
  m_device.getRayTracingShaderGroupHandlesKHR(mRtPipeline, 0, groupCount, sbtSize,
                                              shaderHandleStorage.data());
  mRtSBTBuffer = m_alloc.createBuffer(sbtSize, vk::BufferUsageFlagBits::eTransferSrc,
                                      vk::MemoryPropertyFlagBits::eHostVisible
                                          | vk::MemoryPropertyFlagBits::eHostCoherent);
  m_debug.setObjectName(mRtSBTBuffer.buffer, std::string("SBT").c_str());
	
  void* mapped = m_alloc.map(mRtSBTBuffer);
  auto* pData  = reinterpret_cast<uint8_t*>(mapped);
  for(uint32_t g = 0; g < groupCount; g++)
  {

    memcpy(pData, shaderHandleStorage.data() + g * groupHandleSize, groupHandleSize);
    pData += baseAlignment;
  }

  m_alloc.unmap(mRtSBTBuffer);

  m_alloc.finalizeAndReleaseStaging();
}

nvvk::RaytracingBuilderKHR::Blas HelloVulkan::ObjectToGeometryKHR(const ObjModel& pModel)
{

  vk::AccelerationStructureCreateGeometryTypeInfoKHR createGeometry;
  createGeometry.setGeometryType(vk::GeometryTypeKHR::eTriangles);
  createGeometry.setIndexType(vk::IndexType::eUint32);
  createGeometry.setVertexFormat(vk::Format::eR32G32B32Sfloat);
  createGeometry.setMaxPrimitiveCount(pModel.nbIndices / 3);
  createGeometry.setMaxVertexCount(pModel.nbVertices);
  createGeometry.setAllowsTransforms(VK_FALSE);


  vk::DeviceAddress vertexAddress = m_device.getBufferAddress({pModel.vertexBuffer.buffer});
  vk::DeviceAddress indexAddress  = m_device.getBufferAddress({pModel.indexBuffer.buffer});

  vk::AccelerationStructureGeometryTrianglesDataKHR triangles;
  triangles.setVertexFormat(createGeometry.vertexFormat);
  triangles.setVertexData(vertexAddress);
  triangles.setVertexStride(sizeof(VertexObj));
  triangles.setIndexType(createGeometry.indexType);
  triangles.setIndexData(indexAddress);
  triangles.setTransformData({});


  vk::AccelerationStructureGeometryKHR accelerationStructureGeometry;
  accelerationStructureGeometry.setGeometryType(createGeometry.geometryType);
  accelerationStructureGeometry.setFlags(vk::GeometryFlagBitsKHR::eOpaque);
  accelerationStructureGeometry.geometry.setTriangles(triangles);


  vk::AccelerationStructureBuildOffsetInfoKHR offset;
  offset.setFirstVertex(0);
  offset.setPrimitiveCount(createGeometry.maxPrimitiveCount);
  offset.setPrimitiveOffset(0);
  offset.setTransformOffset(0);


  nvvk::RaytracingBuilderKHR::Blas blas;
  blas.asGeometry.emplace_back(accelerationStructureGeometry);
  blas.asCreateGeometryInfo.emplace_back(createGeometry);
  blas.asBuildOffsetInfo.emplace_back(offset);

  return blas;
}

void HelloVulkan::Raytrace(const vk::CommandBuffer& pCommandBuffer,
                           const nvmath::vec4f&     pClearColor)
{

	UpdateFrame();
	
  m_debug.beginLabel(pCommandBuffer, "Ray Trace");

  mRtPushConstants.clearColor     = pClearColor;
  mRtPushConstants.lightPosition  = m_pushConstant.lightPosition;
  mRtPushConstants.lightIntensity = m_pushConstant.lightIntensity;
  mRtPushConstants.lightType      = m_pushConstant.lightType;

  pCommandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, mRtPipeline);
  pCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, mRtPipelineLayout, 0,
                                    {mRtDescriptorSet, m_descSet}, {});

  pCommandBuffer.pushConstants<RtPushConstants>(mRtPipelineLayout,
                                                vk::ShaderStageFlagBits::eRaygenKHR
                                                    | vk::ShaderStageFlagBits::eMissKHR
                                                    | vk::ShaderStageFlagBits::eClosestHitKHR,
                                                0, mRtPushConstants);

  vk::DeviceSize progSize       = mRtProperties.shaderGroupBaseAlignment;
  vk::DeviceSize rayGenOffset   = 0u * progSize;
  vk::DeviceSize missOffset     = 1u * progSize;
  vk::DeviceSize hitGroupOffset = 3u * progSize;
	


  vk::DeviceSize sbtSize = progSize * static_cast<vk::DeviceSize>(mRtShaderGroups.size());

  const vk::StridedBufferRegionKHR raygenShaderBindingTable = {mRtSBTBuffer.buffer, rayGenOffset,
                                                               progSize, sbtSize};

  const vk::StridedBufferRegionKHR missShaderBindingTable = {mRtSBTBuffer.buffer, missOffset,
                                                             progSize, sbtSize};

  const vk::StridedBufferRegionKHR hitShaderBindingTable = {mRtSBTBuffer.buffer, hitGroupOffset,
                                                            progSize, sbtSize};


  const vk::StridedBufferRegionKHR callableShaderBindingTable;

  pCommandBuffer.traceRaysKHR(&raygenShaderBindingTable, &missShaderBindingTable,
                              &hitShaderBindingTable, &callableShaderBindingTable, m_size.width,
                              m_size.height, 1);

	m_debug.endLabel(pCommandBuffer);
}

void HelloVulkan::Pathtrace(const vk::CommandBuffer& pCommandBuffer,
	const nvmath::vec4f&     pClearColor)
{

	m_debug.beginLabel(pCommandBuffer, "Path Trace");

	mRtPushConstants.clearColor = pClearColor;
	mRtPushConstants.lightPosition = m_pushConstant.lightPosition;
	mRtPushConstants.lightIntensity = m_pushConstant.lightIntensity;
	mRtPushConstants.lightType = m_pushConstant.lightType;

	pCommandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, mPtPipeline);
	pCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, mRtPipelineLayout, 0,
		{ mRtDescriptorSet, m_descSet }, {});

	pCommandBuffer.pushConstants<RtPushConstants>(mRtPipelineLayout,
		vk::ShaderStageFlagBits::eRaygenKHR
		| vk::ShaderStageFlagBits::eMissKHR
		| vk::ShaderStageFlagBits::eClosestHitKHR,
		0, mRtPushConstants);

	vk::DeviceSize progSize = mRtProperties.shaderGroupBaseAlignment;
	vk::DeviceSize rayGenOffset = 0u * progSize;
	vk::DeviceSize missOffset = 1u * progSize;
	vk::DeviceSize hitGroupOffset = 3u * progSize;



	vk::DeviceSize sbtSize = progSize * static_cast<vk::DeviceSize>(mPtShaderGroups.size());

	const vk::StridedBufferRegionKHR raygenShaderBindingTable = { mRtSBTBuffer.buffer, rayGenOffset,
																 progSize, sbtSize };

	const vk::StridedBufferRegionKHR missShaderBindingTable = { mRtSBTBuffer.buffer, missOffset,
															   progSize, sbtSize };

	const vk::StridedBufferRegionKHR hitShaderBindingTable = { mRtSBTBuffer.buffer, hitGroupOffset,
															  progSize, sbtSize };


	const vk::StridedBufferRegionKHR callableShaderBindingTable;

	pCommandBuffer.traceRaysKHR(&raygenShaderBindingTable, &missShaderBindingTable,
		&hitShaderBindingTable, &callableShaderBindingTable, m_size.width,
		m_size.height, 1);

	m_debug.endLabel(pCommandBuffer);
}

void HelloVulkan::CreateSpheres(){


const uint32_t noOfSpheres = 9;
	
  Sphere s;
  mSpheres.resize(noOfSpheres);
	
    for(uint32_t i = 0; i < noOfSpheres; i++)
  {
	if (i == noOfSpheres - 1) {
			s.center = nvmath::vec3f(6.0, 1.5, 0.0);
	}else if(i == noOfSpheres - 2){
		s.center = nvmath::vec3f(0.5, 1.5, 0.0);
    }else  s.center = nvmath::vec3f(1.0 + i, 0.0, 0.0);
   
    s.radius     = 0.5f;
    mSpheres[i]  = std::move(s);
  }
	
  std::vector<AABB> aabbs;
  aabbs.reserve(noOfSpheres);
  for(const auto& s : mSpheres){

	  AABB aabb;
    aabb.minimum = s.center - nvmath::vec3f(s.radius);
    aabb.maximum = s.center + nvmath::vec3f(s.radius);
    aabbs.emplace_back(aabb);
  	
  }

	
  std::vector<MaterialObj> materials;

  MaterialObj mat;
  mat.ambient = vec3f(0.001, 0.001, 0.001);
  mat.diffuse = vec3f(0, 0, 0);
  mat.specular = vec3f(0.8, 0.8, 0.8);
  mat.shininess = 100.f;
  //mat.ior = 1.0f / 1.31f; //Air/Ice
  mat.ior = 1.0f / 1.50f;	//Air/Glass
  mat.illum = 2;
  materials.emplace_back(mat);
  mat.ambient = vec3f(0.0, 0.0, 0.1);
  mat.diffuse = vec3f(0, 0, 0.87);
  mat.specular = vec3f(0.1, 0.1, 0.1);
  mat.shininess = 10.f;
  mat.illum = 4;
  materials.emplace_back(mat);
  mat.ambient = vec3f(0.0, 0.1, 0.0);
  mat.diffuse = vec3f(0, 0.87, 0);
  mat.specular = vec3f(0.1, 0.1, 0.1);
  mat.shininess = 2.f;
  materials.emplace_back(mat);
	//todo continue
  //mat.ambient = vec3f(0.3, 0.3, 0.3);
  mat.ambient = vec3f(0.008, 0.008, 0.008);
  mat.diffuse = vec3f(0.008, 0.008, 0.008);
 // mat.diffuse = vec3f(0.9, 0.9, 0.9);
  mat.specular = vec3f(0.8, 0.8, 0.8);
  mat.shininess = 200.f;
  //mat.specular = 0.1f;
  mat.illum = 3;
  materials.emplace_back(mat);

	
  std::vector<int> matIdx(noOfSpheres);

  for(size_t i = 0; i < mSpheres.size(); i++)
  {
    
  	if(i < 3 ) matIdx[i] = i % materials.size();
	else matIdx[i] = 1;
  }
  matIdx[matIdx.size() - 1] = materials.size() - 1;
  matIdx[matIdx.size() - 2] = materials.size() - 1;

  // Creating all buffers
  using vkBU = vk::BufferUsageFlagBits;
  nvvk::CommandPool genCmdBuf(m_device, m_graphicsQueueIndex);
  auto              cmdBuf = genCmdBuf.createCommandBuffer();
  mSpheresBuffer           = m_alloc.createBuffer(cmdBuf, mSpheres, vkBU::eStorageBuffer);
  mSpheresAabbBuffer       = m_alloc.createBuffer(cmdBuf, aabbs, vkBU::eShaderDeviceAddress);
  mSpheresMatIndexBuffer   = m_alloc.createBuffer(cmdBuf, matIdx, vkBU::eStorageBuffer);
  mSpheresMatColorBuffer   = m_alloc.createBuffer(cmdBuf, materials, vkBU::eStorageBuffer);
  genCmdBuf.submitAndWait(cmdBuf);

  // Debug information
  m_debug.setObjectName(mSpheresBuffer.buffer, "spheres");
  m_debug.setObjectName(mSpheresAabbBuffer.buffer, "spheresAabb");
  m_debug.setObjectName(mSpheresMatColorBuffer.buffer, "spheresMat");
  m_debug.setObjectName(mSpheresMatIndexBuffer.buffer, "spheresMatIdx");
	
}

void HelloVulkan::CreateSpheres(uint32_t nbSpheres)
{
  std::random_device                    rd{};
  std::mt19937                          gen{rd()};
  std::normal_distribution<float>       xzd{0.f, 5.f};
  std::normal_distribution<float>       yd{6.f, 3.f};
  std::uniform_real_distribution<float> radd{.05f, .2f};

  // All spheres
  Sphere s;
  mSpheres.resize(nbSpheres);
  for(uint32_t i = 0; i < nbSpheres; i++)
  {
    s.center     = nvmath::vec3f(xzd(gen), yd(gen), xzd(gen));
    s.radius     = radd(gen);
    mSpheres[i]  = std::move(s);
  }

  // Axis aligned bounding box of each sphere
  std::vector<AABB> aabbs;
  aabbs.reserve(nbSpheres);
  for(const auto& s : mSpheres)
  {
    AABB aabb;
    aabb.minimum = s.center - nvmath::vec3f(s.radius);
    aabb.maximum = s.center + nvmath::vec3f(s.radius);
    aabbs.emplace_back(aabb);
  }

  // Creating two materials
  MaterialObj mat;
  mat.diffuse = vec3f(0, 1, 1);
  std::vector<MaterialObj> materials;
  std::vector<int>         matIdx(nbSpheres);
  materials.emplace_back(mat);
  mat.diffuse = vec3f(1, 1, 0);
  materials.emplace_back(mat);

  // Assign a material to each sphere
  for(size_t i = 0; i < mSpheres.size(); i++)
  {
    matIdx[i] = i % 2;
  }

  // Creating all buffers
  using vkBU = vk::BufferUsageFlagBits;
  nvvk::CommandPool genCmdBuf(m_device, m_graphicsQueueIndex);
  auto              cmdBuf = genCmdBuf.createCommandBuffer();
  mSpheresBuffer = m_alloc.createBuffer(cmdBuf, mSpheres, vkBU::eStorageBuffer);
  mSpheresAabbBuffer = m_alloc.createBuffer(cmdBuf, aabbs, vkBU::eShaderDeviceAddress);
  mSpheresMatColorBuffer = m_alloc.createBuffer(cmdBuf, matIdx, vkBU::eStorageBuffer);
  mSpheresMatIndexBuffer = m_alloc.createBuffer(cmdBuf, materials, vkBU::eStorageBuffer);
  genCmdBuf.submitAndWait(cmdBuf);

  // Debug information
  m_debug.setObjectName(mSpheresBuffer.buffer, "spheres");
  m_debug.setObjectName(mSpheresAabbBuffer.buffer, "spheresAabb");
  m_debug.setObjectName(mSpheresMatColorBuffer.buffer, "spheresMat");
  m_debug.setObjectName(mSpheresMatIndexBuffer.buffer, "spheresMatIdx");
}



nvvk::RaytracingBuilderKHR::Blas HelloVulkan::SphereToVkGeometryKHR(){

  vk::AccelerationStructureCreateGeometryTypeInfoKHR asCreate;
  asCreate.setGeometryType(vk::GeometryTypeKHR::eAabbs);
  asCreate.setMaxPrimitiveCount((uint32_t)mSpheres.size());  // Nb triangles
  asCreate.setIndexType(vk::IndexType::eNoneKHR);
  asCreate.setVertexFormat(vk::Format::eUndefined);
  asCreate.setMaxVertexCount(0);
  asCreate.setAllowsTransforms(VK_FALSE);  // No adding transformation matrices


  vk::DeviceAddress dataAddress = m_device.getBufferAddress({mSpheresAabbBuffer.buffer});
  vk::AccelerationStructureGeometryAabbsDataKHR aabbs;
  aabbs.setData(dataAddress);
  aabbs.setStride(sizeof(AABB));

  // Setting up the build info of the acceleration
  vk::AccelerationStructureGeometryKHR asGeom;
  asGeom.setGeometryType(asCreate.geometryType);
  asGeom.setFlags(vk::GeometryFlagBitsKHR::eOpaque);
  asGeom.geometry.setAabbs(aabbs);

  vk::AccelerationStructureBuildOffsetInfoKHR offset;
  offset.setFirstVertex(0);
  offset.setPrimitiveCount(asCreate.maxPrimitiveCount);
  offset.setPrimitiveOffset(0);
  offset.setTransformOffset(0);

  nvvk::RaytracingBuilderKHR::Blas blas;
  blas.asGeometry.emplace_back(asGeom);
  blas.asCreateGeometryInfo.emplace_back(asCreate);
  blas.asBuildOffsetInfo.emplace_back(offset);
  return blas;
	
}