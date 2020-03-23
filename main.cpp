// https://vulkan-tutorial.com
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <stdexcept>
#include <functional>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <string_view>
#include <optional>
#include <set>
#include <fstream>
#include <filesystem>
#include <tuple>
#include <array>
#include <cstring>
#include <chrono>



class VulkanApplication
{
public:
  VulkanApplication( std::filesystem::path path ) : applicationPath_{ path } {}

  void run()
  {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

private:
  void initWindow()
  {
    if( glfwInit() == GLFW_FALSE )
      throw std::runtime_error{ "Error when intializing GLFW" };

    glfwWindowHint( GLFW_CLIENT_API, GLFW_NO_API );
    glfwWindowHint( GLFW_RESIZABLE, GLFW_TRUE );

    window_ = glfwCreateWindow( windowWidth_, windowHeight_, "Vulkan", nullptr, nullptr );

    if( window_ == nullptr )
      throw std::runtime_error{ "Error when creating GLFW window" };

    glfwSetWindowUserPointer( window_, this );
    glfwSetFramebufferSizeCallback( window_, framebufferResizeCallback );
  }

  template< typename T >
  void mapDataInStagingBuffer( VkDeviceSize bufferSize, const T *bufferData, VkBuffer &stagingBuffer, VkDeviceMemory &stagingBufferMemory )
  {
    createBuffer( bufferSize,
                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  stagingBuffer, stagingBufferMemory );

    void *data;
    vkMapMemory( logicalDevice_, stagingBufferMemory, 0, bufferSize, 0, &data );
    std::memcpy( data, bufferData, static_cast< size_t >( bufferSize ) );
    vkUnmapMemory( logicalDevice_, stagingBufferMemory );
  }

  void createIndexBuffer()
  {
    VkDeviceSize bufferSize = sizeof( typename decltype( indices_ )::value_type ) * indices_.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    mapDataInStagingBuffer( bufferSize, indices_.data(), stagingBuffer, stagingBufferMemory );

    createBuffer( bufferSize,
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                  indexBuffer_,
                  indexBufferMemory_ );

    copyBuffer( stagingBuffer, indexBuffer_, bufferSize );

    vkDestroyBuffer( logicalDevice_, stagingBuffer, nullptr );
    vkFreeMemory( logicalDevice_, stagingBufferMemory, nullptr );
  }

  void createDescriptorSetLayout()
  {
    VkDescriptorSetLayoutBinding uboLayoutBinding
    {
      .binding{ 0 },
      .descriptorType{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER },
      .descriptorCount{ 1 },
      .stageFlags{ VK_SHADER_STAGE_VERTEX_BIT }
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo
    {
      .sType{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO },
      .bindingCount{ 1 },
      .pBindings{ &uboLayoutBinding }
    };

    if( vkCreateDescriptorSetLayout( logicalDevice_, &layoutInfo, nullptr, &descriptorSetLayout_ ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to create descriptor set layout!" };
  }

  void createUniformBuffers()
  {
    VkDeviceSize bufferSize = sizeof( UniformBufferObject );

    uniformBuffers_.resize( swapChainImages_.size() );
    uniformBuffersMemory_.resize( swapChainImages_.size() );

    for( std::size_t i = 0; i < swapChainImages_.size(); i++ )
      createBuffer( bufferSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    uniformBuffers_[ i ],
                    uniformBuffersMemory_[ i ] );
  }

  void createDescriptorPool()
  {
    VkDescriptorPoolSize poolSize
    {
      .type{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER },
      .descriptorCount{ static_cast< std::uint32_t >( swapChainImages_.size() ) }
    };

    VkDescriptorPoolCreateInfo poolInfo
    {
      .sType{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO },
      .maxSets{ static_cast< std::uint32_t >( swapChainImages_.size() ) },
      .poolSizeCount{ 1 },
      .pPoolSizes{ &poolSize }
    };

    if( vkCreateDescriptorPool( logicalDevice_, &poolInfo, nullptr, &descriptorPool_ ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to create descriptor pool!" };
  }

  void createDescriptorSets()
  {
    std::vector< VkDescriptorSetLayout > layouts{ swapChainImages_.size(), descriptorSetLayout_ };
    VkDescriptorSetAllocateInfo allocInfo
    {
      .sType{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO },
      .descriptorPool{ descriptorPool_ },
      .descriptorSetCount{ static_cast< std::uint32_t >( swapChainImages_.size() ) },
      .pSetLayouts{ layouts.data() }
    };

    descriptorSets_.resize( swapChainImages_.size() );
    if( vkAllocateDescriptorSets( logicalDevice_, &allocInfo, descriptorSets_.data() ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to allocate descriptor sets!" };

    for( std::size_t i = 0; i < swapChainImages_.size(); i++ )
    {
      VkDescriptorBufferInfo buffersInfo[]
      {
        {
          .buffer{ uniformBuffers_[ i ] },
          .offset{ 0 },
          .range{ sizeof( UniformBufferObject ) }
        }
      };

      VkWriteDescriptorSet descriptorWrite
      {
        .sType{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET },
        .dstSet{ descriptorSets_[ i ] },
        .dstBinding{ 0 },
        .dstArrayElement{ 0 },
        .descriptorCount{ static_cast< std::uint32_t >( sizeof( buffersInfo ) / sizeof( VkDescriptorBufferInfo ) ) },
        .descriptorType{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER },
        .pBufferInfo{ buffersInfo }
      };

      vkUpdateDescriptorSets( logicalDevice_, descriptorWrite.descriptorCount, &descriptorWrite, 0, nullptr );
    }
  }

  void initVulkan()
  {
    checkValidationSupport();
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickFirstSuitablePhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createDescriptorSetLayout();
    createGraphicPipeline();
    createFramebuffers();
    createCommandPools();
    createVertexBuffer();
    createIndexBuffer();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createDrawCommandBuffers();
    createSynchronizationObjects();
  }

  std::uint32_t findMemoryType( std::uint32_t typeFilter, VkMemoryPropertyFlags properties )
  {
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties( physicalDevice_, &memoryProperties );

    for( std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++ )
      if( ( typeFilter & ( 1 << i ) ) && ( memoryProperties.memoryTypes[ i ].propertyFlags & properties ) == properties )
        return i;

    throw std::runtime_error{ "Error failed to find suitable memory type!" };
  }

  void allocateAndBindBuffer( VkBuffer &buffer, VkMemoryPropertyFlags properties, VkDeviceMemory &bufferMemory )
  {
    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements( logicalDevice_, buffer, &memoryRequirements );

    VkMemoryAllocateInfo allocInfo
    {
      .sType{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO },
      .allocationSize{ memoryRequirements.size },
      .memoryTypeIndex{ findMemoryType( memoryRequirements.memoryTypeBits, properties ) }
    };

    if( vkAllocateMemory( logicalDevice_, &allocInfo, nullptr, &bufferMemory ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to allocate vertex buffer memory!" };

    vkBindBufferMemory( logicalDevice_, buffer, bufferMemory, 0 );
  }

  void createBuffer( VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &buffer, VkDeviceMemory &bufferMemory )
  {
    std::uint32_t queueFamilyIndices[]
    {
      requiredQueueFamilyIndices_.graphicsQueueFamilyIndex.value(),
      requiredQueueFamilyIndices_.transfertQueueFamilyIndex.value()
    };

    VkBufferCreateInfo bufferInfo
    {
      .sType{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO },
      .size{ size },
      .usage{ usage },
      .sharingMode{ VK_SHARING_MODE_EXCLUSIVE }
    };

    if( requiredQueueFamilyIndices_.graphicsQueueFamilyIndex != requiredQueueFamilyIndices_.transfertQueueFamilyIndex )
    {
      bufferInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
      bufferInfo.queueFamilyIndexCount = 2;
      bufferInfo.pQueueFamilyIndices = queueFamilyIndices;
    }

    if( vkCreateBuffer( logicalDevice_, &bufferInfo, nullptr, &buffer ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to create vertex buffer!" };

    allocateAndBindBuffer( buffer, properties, bufferMemory );
  }

  void allocateCommandBuffers( VkCommandPool pool, std::uint32_t bufferCount, VkCommandBuffer *commandBuffers )
  {
    VkCommandBufferAllocateInfo allocInfo
    {
      .sType{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO },
      .commandPool{ pool },
      .level{ VK_COMMAND_BUFFER_LEVEL_PRIMARY },
      .commandBufferCount{ bufferCount }
    };

    if( vkAllocateCommandBuffers( logicalDevice_, &allocInfo, commandBuffers ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to allocate command buffers" };
  }

  void copyBufferSync( VkCommandBuffer &commandBuffer, VkDeviceSize size, VkBuffer srcBuffer, VkBuffer dstBuffer )
  {
    VkCommandBufferBeginInfo beginInfo
    {
      .sType{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO },
      .flags{ VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }
    };

    vkBeginCommandBuffer( commandBuffer, &beginInfo );

    VkBufferCopy copyRegion
    {
      .srcOffset{ 0 }, // Optional
      .dstOffset{ 0 }, // Optional
      .size{ size }
    };

    vkCmdCopyBuffer( commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion );

    vkEndCommandBuffer( commandBuffer );

    VkSubmitInfo submitInfo
    {
      .sType{ VK_STRUCTURE_TYPE_SUBMIT_INFO },
      .commandBufferCount{ 1 },
      .pCommandBuffers{ &commandBuffer }
    };

    vkQueueSubmit( transfertQueue_, 1, &submitInfo, VK_NULL_HANDLE );
    vkQueueWaitIdle( transfertQueue_ );
  }

  void copyBuffer( VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size )
  {
    VkCommandBuffer commandBuffer;

    allocateCommandBuffers( transfertCommandPool_, 1, &commandBuffer );
    copyBufferSync( commandBuffer, size, srcBuffer, dstBuffer );
    vkFreeCommandBuffers( logicalDevice_, transfertCommandPool_, 1, &commandBuffer );
  }

  void createVertexBuffer()
  {
    VkDeviceSize bufferSize = sizeof( typename decltype( vertices_ )::value_type ) * vertices_.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    mapDataInStagingBuffer( bufferSize, vertices_.data(), stagingBuffer, stagingBufferMemory );

    createBuffer( bufferSize,
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                  vertexBuffer_,
                  vertexBufferMemory_ );

    copyBuffer( stagingBuffer, vertexBuffer_, bufferSize );

    vkDestroyBuffer( logicalDevice_, stagingBuffer, nullptr );
    vkFreeMemory( logicalDevice_, stagingBufferMemory, nullptr );
  }

  void recreateSwapChain()
  {
    handleMinimizedWindow();

    vkDeviceWaitIdle( logicalDevice_ );

    cleanupSwapChain();

    setupSwapChainSupportForPhysicalDevice( physicalDevice_ );
    createSwapChain();
    createImageViews();
    createRenderPass();
    createGraphicPipeline();
    createFramebuffers();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createDrawCommandBuffers();
  }

  void handleMinimizedWindow()
  {
    int width{}, height{};

    glfwGetFramebufferSize( window_, &width, &height );

    while( width == 0 || height == 0 ) {
      glfwGetFramebufferSize( window_, &width, &height );
      glfwWaitEvents();
    }
  }

  void createSynchronizationObjects()
  {
    VkSemaphoreCreateInfo semaphoreInfo
    {
      .sType{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO }
    };

    VkFenceCreateInfo fenceInfo
    {
      .sType{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO },
      .flags{ VK_FENCE_CREATE_SIGNALED_BIT }
    };

    imageAvailableSemaphore_.resize( maxFrameInFlight_ );
    renderFinishedSemaphore_.resize( maxFrameInFlight_ );
    inFlightFences_.resize( maxFrameInFlight_ );
    inFlightImageFences_.resize( swapChainImages_.size(), VK_NULL_HANDLE );

    for( std::uint8_t i = 0; i < maxFrameInFlight_; ++i )
      if( vkCreateSemaphore( logicalDevice_, &semaphoreInfo, nullptr, &imageAvailableSemaphore_[ i ] ) != VK_SUCCESS ||
          vkCreateSemaphore( logicalDevice_, &semaphoreInfo, nullptr, &renderFinishedSemaphore_[ i ] ) != VK_SUCCESS ||
          vkCreateFence( logicalDevice_, &fenceInfo, nullptr, &inFlightFences_[ i ] ) != VK_SUCCESS )
        throw std::runtime_error{ "Error failed to create synchronization objects!" };
  }

  void createDrawCommandBuffer( VkFramebuffer targetFrameBuffer, VkCommandBuffer targetCommandBuffer, const VkDescriptorSet *targetDescriptorSet )
  {
    VkCommandBufferBeginInfo beginInfo
    {
      .sType{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO },
      .flags{ 0 }, // Optional
      .pInheritanceInfo{ nullptr } // Optional
    };

    if( vkBeginCommandBuffer( targetCommandBuffer, &beginInfo ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to begin recording command buffer!" };

    VkClearValue clearColors[]
    {
      {
        0.2f, 0.2f, 0.2f, 1.0f
      }
    };

    VkRenderPassBeginInfo renderPassInfo
    {
      .sType{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO },
      .renderPass{ renderPass_},
      .framebuffer{ targetFrameBuffer },
      .renderArea
      {
        .offset{ 0, 0 },
        .extent{ swapChainExtent_ }
      },
      .clearValueCount{ sizeof( clearColors ) / sizeof( VkClearValue ) },
      .pClearValues{ clearColors }
    };

    vkCmdBeginRenderPass( targetCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE );
    vkCmdBindPipeline( targetCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicPipelines_.front() );

    VkBuffer vertexBuffers[] = { vertexBuffer_ };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers( targetCommandBuffer, 0, 1, vertexBuffers, offsets );
    vkCmdBindIndexBuffer( targetCommandBuffer, indexBuffer_, 0, VK_INDEX_TYPE_UINT16 );
    vkCmdBindDescriptorSets( targetCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, targetDescriptorSet, 0, nullptr );

    vkCmdDrawIndexed( targetCommandBuffer, static_cast< uint32_t >( indices_.size() ), 1, 0, 0, 0 );
    vkCmdEndRenderPass( targetCommandBuffer );

    if( vkEndCommandBuffer( targetCommandBuffer ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to record command buffer!" };
  }

  void createDrawCommandBuffers()
  {
    commandBuffers_.resize( swapChainFramebuffers_.size() );

    allocateCommandBuffers( graphicCommandPool_, static_cast< std::uint32_t >( commandBuffers_.size() ), commandBuffers_.data() );

    for( std::size_t i = 0; i < commandBuffers_.size(); i++ )
      createDrawCommandBuffer( swapChainFramebuffers_[ i ], commandBuffers_[ i ], &descriptorSets_[ i ] );
  }

  void createCommandPool( VkCommandPoolCreateFlags flags, std::uint32_t queueFamilyIndex, VkCommandPool *commandPool, const char *const exceptionMessage )
  {
    VkCommandPoolCreateInfo poolInfo
    {
      .sType{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO },
      .flags{ flags },
      .queueFamilyIndex{ queueFamilyIndex }
    };

    if( vkCreateCommandPool( logicalDevice_, &poolInfo, nullptr, commandPool ) != VK_SUCCESS )
      throw std::runtime_error{ exceptionMessage };
  }

  void createGraphicPool()
  {
    createCommandPool( 0,
                       requiredQueueFamilyIndices_.graphicsQueueFamilyIndex.value(),
                       &graphicCommandPool_,
                       "Error failed to create the graphic command pool!" );
  }

  void createTransfertPool()
  {
    createCommandPool( VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                       requiredQueueFamilyIndices_.transfertQueueFamilyIndex.value(),
                       &transfertCommandPool_,
                       "Error failed to create the transfert command pool!" );
  }

  void createCommandPools()
  {
    createGraphicPool();
    createTransfertPool();
  }

  void createFramebuffer( VkImageView imageView, VkFramebuffer *targetFramebuffer )
  {
    VkImageView attachments[]
    {
      imageView
    };

    VkFramebufferCreateInfo framebufferInfo
    {
      .sType{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO },
      .renderPass{ renderPass_ },
      .attachmentCount{ sizeof( attachments ) / sizeof( VkImageView ) },
      .pAttachments{ attachments },
      .width{ swapChainExtent_.width },
      .height{ swapChainExtent_.height },
      .layers{ 1 }
    };

    if( vkCreateFramebuffer( logicalDevice_, &framebufferInfo, nullptr, targetFramebuffer ) != VK_SUCCESS )
      throw std::runtime_error{ "failed to create framebuffer!" };
  }

  void createFramebuffers()
  {
    swapChainFramebuffers_.resize( swapChainImageViews_.size() );

    for( std::size_t i = 0; i < swapChainImageViews_.size(); i++ )
      createFramebuffer( swapChainImageViews_[ i ], &swapChainFramebuffers_[ i ] );
  }

  void createRenderPass()
  {
    VkAttachmentDescription colorAttachmentDescriptions[]
    {
      {
        .format{ swapChainSurfaceFormat_.format },
        .samples{ VK_SAMPLE_COUNT_1_BIT },
        .loadOp{ VK_ATTACHMENT_LOAD_OP_CLEAR },
        .storeOp{ VK_ATTACHMENT_STORE_OP_STORE },
        .stencilLoadOp{ VK_ATTACHMENT_LOAD_OP_DONT_CARE },
        .stencilStoreOp{ VK_ATTACHMENT_STORE_OP_DONT_CARE },
        .initialLayout{ VK_IMAGE_LAYOUT_UNDEFINED },
        .finalLayout{ VK_IMAGE_LAYOUT_PRESENT_SRC_KHR },
      }
    };

    VkAttachmentReference colorAttachmentReferences[]
    {
      {
        .attachment{ 0 },
        .layout{ VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
      }
    };

    VkSubpassDescription subPasses[]
    {
      {
        .pipelineBindPoint{ VK_PIPELINE_BIND_POINT_GRAPHICS },
        .colorAttachmentCount{ sizeof( colorAttachmentReferences ) / sizeof( VkAttachmentReference ) },
        .pColorAttachments{ colorAttachmentReferences }
      }
    };

    VkSubpassDependency dependencies[]
    {
      {
        .srcSubpass{ VK_SUBPASS_EXTERNAL },
        .dstSubpass{ 0 },
        .srcStageMask{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT },
        .dstStageMask{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT },
        .srcAccessMask{ 0 },
        .dstAccessMask{ VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT }
      }
    };

    VkRenderPassCreateInfo renderPassInfo
    {
      .sType{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO },
      .attachmentCount{ sizeof( colorAttachmentDescriptions ) / sizeof( VkAttachmentDescription ) },
      .pAttachments{ colorAttachmentDescriptions },
      .subpassCount{ sizeof( subPasses ) / sizeof( VkSubpassDescription ) },
      .pSubpasses{ subPasses },
      .dependencyCount{ sizeof( dependencies ) / sizeof( VkSubpassDependency ) },
      .pDependencies{ dependencies }
    };

    if( vkCreateRenderPass( logicalDevice_, &renderPassInfo, nullptr, &renderPass_ ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to create render pass!" };
  }

  auto getShaderModules()
  {
    auto shadersDirectory = applicationPath_.parent_path() / "shaders";

    auto vertexShaderCode = loadShaderModule( shadersDirectory / "vert.spv" );
    auto fragmentShaderCode = loadShaderModule( shadersDirectory / "frag.spv" );

    return std::make_tuple( createShaderModule( std::move( vertexShaderCode ) ),
                            createShaderModule( std::move( fragmentShaderCode ) ) );
  }

  void createGraphicPipelineLayout()
  {
    VkPipelineLayoutCreateInfo pipelineLayoutInfo
    {
      .sType{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO },
      .setLayoutCount{ 1 },
      .pSetLayouts{ &descriptorSetLayout_ },
      .pushConstantRangeCount{ 0 }, // Optional
      .pPushConstantRanges{ nullptr }, // Optional
    };

    if( vkCreatePipelineLayout( logicalDevice_, &pipelineLayoutInfo, nullptr, &pipelineLayout_ ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to create pipeline layout!" };
  }

  void createGraphicPipeline()
  {
    createGraphicPipelineLayout();

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo
    {
      .sType{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO },
      .vertexBindingDescriptionCount{ 1 },
      .pVertexBindingDescriptions{ &bindingDescription },
      .vertexAttributeDescriptionCount{ static_cast< std::uint32_t >( attributeDescriptions.size() ) },
      .pVertexAttributeDescriptions{ attributeDescriptions.data() }
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly
    {
      .sType{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO },
      .topology{ VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST },
      .primitiveRestartEnable{ VK_FALSE }
    };

    VkViewport viewports[]
    {
      {
        .x{ 0.0f },
        .y{ 0.0f },
        .width{ static_cast< float >( swapChainExtent_.width ) },
        .height{ static_cast< float >( swapChainExtent_.height ) },
        .minDepth{ 0.0f },
        .maxDepth{ 1.0f },
      }
    };

    VkRect2D scissors[]
    {
      {
        .offset{ 0, 0 },
        .extent{ swapChainExtent_ }
      }
    };

    VkPipelineViewportStateCreateInfo viewportState
    {
      .sType{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO },
      .viewportCount{ sizeof( viewports ) / sizeof( VkViewport ) },
      .pViewports{ viewports },
      .scissorCount{ sizeof( scissors ) / sizeof( VkRect2D ) },
      .pScissors{ scissors },
    };

    VkPipelineRasterizationStateCreateInfo rasterizer
    {
      .sType{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO },
      .depthClampEnable{ VK_FALSE },
      .rasterizerDiscardEnable{ VK_FALSE },
      .polygonMode{ VK_POLYGON_MODE_FILL },
      .cullMode{ VK_CULL_MODE_BACK_BIT },
      .frontFace{ VK_FRONT_FACE_COUNTER_CLOCKWISE },
      .depthBiasEnable{ VK_FALSE },
      .depthBiasConstantFactor{ 0.0f }, // Optional
      .depthBiasClamp{ 0.0f }, // Optional
      .depthBiasSlopeFactor{ 0.0f }, // Optional
      .lineWidth{ 1.0f },
    };

    VkPipelineMultisampleStateCreateInfo multisampling
    {
      .sType{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO },
      .rasterizationSamples{ VK_SAMPLE_COUNT_1_BIT },
      .sampleShadingEnable{ VK_FALSE },
      .minSampleShading{ 1.0f }, // Optional
      .pSampleMask{ nullptr }, // Optional
      .alphaToCoverageEnable{ VK_FALSE }, // Optional
      .alphaToOneEnable{ VK_FALSE }, // Optional
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachments[]
    {
      {
        .blendEnable{ VK_FALSE },
        .srcColorBlendFactor{ VK_BLEND_FACTOR_ONE }, // Optional
        .dstColorBlendFactor{ VK_BLEND_FACTOR_ZERO }, // Optional
        .colorBlendOp{ VK_BLEND_OP_ADD }, // Optional
        .srcAlphaBlendFactor{ VK_BLEND_FACTOR_ONE }, // Optional
        .dstAlphaBlendFactor{ VK_BLEND_FACTOR_ZERO }, // Optional
        .alphaBlendOp{ VK_BLEND_OP_ADD }, // Optional
        .colorWriteMask{ VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT }
      }
    };

    VkPipelineColorBlendStateCreateInfo colorBlending
    {
      .sType{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO },
      .logicOpEnable{ VK_FALSE },
      .logicOp{ VK_LOGIC_OP_COPY }, // Optional
      .attachmentCount{ sizeof( colorBlendAttachments ) / sizeof( VkPipelineColorBlendAttachmentState ) },
      .pAttachments{ colorBlendAttachments },
      .blendConstants{ 0.0f, 0.0f, 0.0f, 0.0f }
    };

    auto [vertexShaderModule, fragmentShaderModule] = getShaderModules();

    VkPipelineShaderStageCreateInfo vertexShaderStageInfo
    {
      .sType{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO },
      .stage{ VK_SHADER_STAGE_VERTEX_BIT },
      .module{ vertexShaderModule },
      .pName{ "main" }
    };

    VkPipelineShaderStageCreateInfo fragmentShaderStageInfo
    {
      .sType{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO },
      .stage{ VK_SHADER_STAGE_FRAGMENT_BIT },
      .module{ fragmentShaderModule },
      .pName{ "main" }
    };

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertexShaderStageInfo, fragmentShaderStageInfo };

    VkGraphicsPipelineCreateInfo pipelinesInfo[]
    {
      {
        .sType{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO },
        .stageCount{ sizeof( shaderStages ) / sizeof( VkPipelineShaderStageCreateInfo ) },
        .pStages{ shaderStages },
        .pVertexInputState{ &vertexInputInfo },
        .pInputAssemblyState{ &inputAssembly },
        .pViewportState{ &viewportState },
        .pRasterizationState{ &rasterizer },
        .pMultisampleState{ &multisampling },
        .pDepthStencilState{ nullptr }, // Optional
        .pColorBlendState{ &colorBlending },
        .pDynamicState{ nullptr }, // Optional
        .layout{ pipelineLayout_ },
        .renderPass{ renderPass_ },
        .subpass{ 0 },
        .basePipelineHandle{ VK_NULL_HANDLE }, // Optional
        .basePipelineIndex{ -1 } // Optional
      }
    };

    graphicPipelines_.resize( sizeof( pipelinesInfo ) / sizeof( VkGraphicsPipelineCreateInfo ) );

    if( vkCreateGraphicsPipelines( logicalDevice_, VK_NULL_HANDLE, 1, pipelinesInfo, nullptr, graphicPipelines_.data() ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to create graphics pipeline!" };

    vkDestroyShaderModule( logicalDevice_, fragmentShaderModule, nullptr );
    vkDestroyShaderModule( logicalDevice_, vertexShaderModule, nullptr );
  }

  VkShaderModule createShaderModule( std::vector< char > &&code )
  {
    VkShaderModuleCreateInfo createInfo
    {
      .sType{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO },
      .codeSize{ code.size() },
      .pCode{ reinterpret_cast< const uint32_t * >( code.data() ) }
    };

    VkShaderModule shaderModule;
    if( vkCreateShaderModule( logicalDevice_, &createInfo, nullptr, &shaderModule ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to create shader module!" };

    return shaderModule;
  }

  void createImageView( VkImage image, VkImageView *targetImageView )
  {
    VkImageViewCreateInfo createInfo
    {
      .sType{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO },
      .image{ image },
      .viewType{ VK_IMAGE_VIEW_TYPE_2D },
      .format{ swapChainSurfaceFormat_.format },
      .components
      {
        .r{ VK_COMPONENT_SWIZZLE_IDENTITY },
        .g{ VK_COMPONENT_SWIZZLE_IDENTITY },
        .b{ VK_COMPONENT_SWIZZLE_IDENTITY },
        .a{ VK_COMPONENT_SWIZZLE_IDENTITY }
      },
      .subresourceRange
      {
        .aspectMask{ VK_IMAGE_ASPECT_COLOR_BIT },
        .baseMipLevel{ 0 },
        .levelCount{ 1 },
        .baseArrayLayer{ 0 },
        .layerCount{ 1 }
      }
    };

    if( vkCreateImageView( logicalDevice_, &createInfo, nullptr, targetImageView ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to create image views!" };
  }

  void createImageViews()
  {
    swapChainImageViews_.resize( swapChainImages_.size() );

    for( std::size_t i = 0; i < swapChainImages_.size(); ++i )
      createImageView( swapChainImages_[ i ], &swapChainImageViews_[ i ] );
  }

  void setupSwapChainImageSharingMode( VkSwapchainCreateInfoKHR &createInfo )
  {
    uint32_t queueFamilyIndices[]
    {
      requiredQueueFamilyIndices_.graphicsQueueFamilyIndex.value(),
      requiredQueueFamilyIndices_.presentationQueueFamilyIndex.value()
    };

    if( requiredQueueFamilyIndices_.graphicsQueueFamilyIndex != requiredQueueFamilyIndices_.presentationQueueFamilyIndex )
    {
      createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      createInfo.queueFamilyIndexCount = 2;
      createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
      createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
      createInfo.queueFamilyIndexCount = 0; // Optional
      createInfo.pQueueFamilyIndices = nullptr; // Optional
    }
  }

  auto getRelevantSwapChainImageCount()
  {
    std::uint32_t imageCount = swapChainSupportDetails_.surfaceCapabilities_.minImageCount + 1;

    if( swapChainSupportDetails_.surfaceCapabilities_.maxImageCount > 0 && imageCount > swapChainSupportDetails_.surfaceCapabilities_.maxImageCount )
      imageCount = swapChainSupportDetails_.surfaceCapabilities_.maxImageCount;

    // use all the swap chain capacity, dunno if it is a good idea yet
    maxFrameInFlight_ = swapChainSupportDetails_.surfaceCapabilities_.maxImageCount;

    return imageCount;
  }

  void createSwapChain()
  {
    setupSwapChainSurfaceFormat();
    setupSwapChainExtent();

    auto imageCount = getRelevantSwapChainImageCount();

    VkSwapchainCreateInfoKHR createInfo
    {
      .sType{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR },
      .surface{ surface_ },
      .minImageCount{ imageCount },
      .imageFormat{ swapChainSurfaceFormat_.format },
      .imageColorSpace{ swapChainSurfaceFormat_.colorSpace },
      .imageExtent{ swapChainExtent_ },
      .imageArrayLayers{ 1 },
      .imageUsage{ VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT },
      .preTransform{ swapChainSupportDetails_.surfaceCapabilities_.currentTransform },
      .compositeAlpha{ VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR },
      .presentMode{ chooseSwapPresentMode() },
      .clipped{ VK_TRUE },
      .oldSwapchain{ VK_NULL_HANDLE }
    };

    setupSwapChainImageSharingMode( createInfo );

    if( vkCreateSwapchainKHR( logicalDevice_, &createInfo, nullptr, &swapChain_ ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to create swap chain!" };

    vkGetSwapchainImagesKHR( logicalDevice_, swapChain_, &imageCount, nullptr );
    swapChainImages_.resize( imageCount );
    vkGetSwapchainImagesKHR( logicalDevice_, swapChain_, &imageCount, swapChainImages_.data() );
  }

  void setupSwapChainSurfaceFormat()
  {
    swapChainSurfaceFormat_ = swapChainSupportDetails_.surfaceFormats_.front();

    for( const auto &availableFormat : swapChainSupportDetails_.surfaceFormats_ )
      if( availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR )
      {
        swapChainSurfaceFormat_ = availableFormat;
        return;
      }
  }

  VkPresentModeKHR chooseSwapPresentMode()
  {
    for( const auto &availablePresentMode : swapChainSupportDetails_.presentModes_ )
      if( availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR )
        return availablePresentMode;

    return VK_PRESENT_MODE_FIFO_KHR;
  }

  void setupSwapChainExtent()
  {
    const auto &capabilities = swapChainSupportDetails_.surfaceCapabilities_;

    if( capabilities.currentExtent.width != std::numeric_limits< std::uint32_t >::max() )
    {
      swapChainExtent_ = capabilities.currentExtent;
      return;
    }

    int currentWindowWidth, currentWindowHeight;
    glfwGetFramebufferSize( window_, &currentWindowWidth, &currentWindowHeight );

    VkExtent2D actualExtent{ static_cast< std::uint32_t >( currentWindowWidth ), static_cast< std::uint32_t >( currentWindowHeight ) };

    actualExtent.width = std::max( capabilities.minImageExtent.width, std::min( capabilities.maxImageExtent.width, actualExtent.width ) );
    actualExtent.height = std::max( capabilities.minImageExtent.height, std::min( capabilities.maxImageExtent.height, actualExtent.height ) );

    swapChainExtent_ = actualExtent;
  }

  void createSurface()
  {
    if( glfwCreateWindowSurface( vulkanInstance_, window_, nullptr, &surface_ ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to create window surface!" };
  }

  auto getAllDeviceQueueCreateInfo()
  {
    std::vector< VkDeviceQueueCreateInfo > allQueueCreateInfo;

    float queuePriorities[] = { 1.0f };

    for( std::uint32_t queueFamilyIndex : requiredQueueFamilyIndices_.toSet() )
      allQueueCreateInfo.emplace_back( VkDeviceQueueCreateInfo
                                       {
                                         .sType{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO },
                                         .queueFamilyIndex{ queueFamilyIndex },
                                         .queueCount{ 1 }, // as today
                                         .pQueuePriorities{ queuePriorities }
                                       } );
    return allQueueCreateInfo;
  }

  void createLogicalDevice()
  {
    auto allQueueCreateInfo = getAllDeviceQueueCreateInfo();

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo deviceCreateInfo
    {
      .sType{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO },
      .queueCreateInfoCount{ static_cast< std::uint32_t >( allQueueCreateInfo.size() ) },
      .pQueueCreateInfos{ allQueueCreateInfo.data() },
      .enabledLayerCount{ 0 },
      .enabledExtensionCount{ static_cast< std::uint32_t >( vulkanProductionExtensions_.size() ) },
      .ppEnabledExtensionNames{ vulkanProductionExtensions_.data() },
      .pEnabledFeatures{ &deviceFeatures }
    };

    if( IsVulkanValidationEnabled() )
    {
      deviceCreateInfo.enabledLayerCount = static_cast< std::uint32_t >( vulkanValidationLayers_.size() );
      deviceCreateInfo.ppEnabledLayerNames = vulkanValidationLayers_.data();
    }

    if( vkCreateDevice( physicalDevice_, &deviceCreateInfo, nullptr, &logicalDevice_ ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to create logical device!" };

    vkGetDeviceQueue( logicalDevice_, requiredQueueFamilyIndices_.graphicsQueueFamilyIndex.value(), 0, &graphicsQueue_ );
    vkGetDeviceQueue( logicalDevice_, requiredQueueFamilyIndices_.presentationQueueFamilyIndex.value(), 0, &presentationQueue_ );
    vkGetDeviceQueue( logicalDevice_, requiredQueueFamilyIndices_.transfertQueueFamilyIndex.value(), 0, &transfertQueue_ );
  }

  void pickFirstSuitablePhysicalDevice()
  {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices( vulkanInstance_, &deviceCount, nullptr );

    if( deviceCount == 0 )
      throw std::runtime_error{ "Error when attempting to find GPUs with Vulkan support!" };

    std::vector< VkPhysicalDevice > devices{ deviceCount };
    vkEnumeratePhysicalDevices( vulkanInstance_, &deviceCount, devices.data() );

    for( const auto &device : devices )
      if( isPhysicalDeviceSuitable( device ) )
      {
        physicalDevice_ = device;
        break;
      }

    if( physicalDevice_ == VK_NULL_HANDLE )
      throw std::runtime_error{ "Error failed to find a suitable GPU!" };
  }

  bool isPhysicalDeviceSuitable( VkPhysicalDevice device )
  {
    setupRequiredQueueFamiliesForPhysicalDevice( device );
    setupSwapChainSupportForPhysicalDevice( device );

    return ( requiredQueueFamilyIndices_.isComplete()
             && isDeviceSupportingRequiredExtensions( device )
             && swapChainSupportDetails_.isComplete() );
  }

  void setupSwapChainSupportForPhysicalDevice( VkPhysicalDevice device )
  {
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR( device, surface_, &swapChainSupportDetails_.surfaceCapabilities_ );

    std::uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR( device, surface_, &formatCount, nullptr );

    if( formatCount != 0 )
    {
      swapChainSupportDetails_.surfaceFormats_.resize( formatCount );
      vkGetPhysicalDeviceSurfaceFormatsKHR( device, surface_, &formatCount, swapChainSupportDetails_.surfaceFormats_.data() );
    }

    std::uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR( device, surface_, &presentModeCount, nullptr );

    if( presentModeCount != 0 )
    {
      swapChainSupportDetails_.presentModes_.resize( presentModeCount );
      vkGetPhysicalDeviceSurfacePresentModesKHR( device, surface_, &presentModeCount, swapChainSupportDetails_.presentModes_.data() );
    }
  }

  bool isDeviceSupportingRequiredExtensions( VkPhysicalDevice device )
  {
    std::uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties( device, nullptr, &extensionCount, nullptr );

    std::vector< VkExtensionProperties > availableExtensions( extensionCount );
    vkEnumerateDeviceExtensionProperties( device, nullptr, &extensionCount, availableExtensions.data() );

    for( std::string_view extension : vulkanProductionExtensions_ )
      if( std::find_if( std::cbegin( availableExtensions ),
                        std::cend( availableExtensions ),
                        [ &extension ]( const VkExtensionProperties &extensionProperties )
      {
        return extension.compare( extensionProperties.extensionName ) == 0;
      } ) == std::cend( availableExtensions ) )
        return false;

      return true;
  }

  void setupRequiredQueueFamilyForPhysicalDevice( VkPhysicalDevice device, const VkQueueFamilyProperties &queueFamily, std::uint32_t &mutableQueueFamilyIndex )
  {
    if( requiredQueueFamilyIndices_.isComplete() )
    {
      mutableQueueFamilyIndex = std::numeric_limits< std::uint32_t >::max() - 1;
      return;
    }

    if( queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT )
      requiredQueueFamilyIndices_.graphicsQueueFamilyIndex = mutableQueueFamilyIndex;

    VkBool32 hasPresentationSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR( device, mutableQueueFamilyIndex, surface_, &hasPresentationSupport );

    if( hasPresentationSupport )
      requiredQueueFamilyIndices_.presentationQueueFamilyIndex = mutableQueueFamilyIndex;

    if( queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT )
      requiredQueueFamilyIndices_.transfertQueueFamilyIndex = mutableQueueFamilyIndex;
  }

  void setupRequiredQueueFamiliesForPhysicalDevice( VkPhysicalDevice device )
  {
    std::uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, nullptr );

    std::vector< VkQueueFamilyProperties > queueFamilies{ queueFamilyCount };
    vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, queueFamilies.data() );

    for( std::uint32_t i = 0; i < queueFamilies.size(); ++i )
      setupRequiredQueueFamilyForPhysicalDevice( device, queueFamilies[ i ], i );
  }

  void setupDebugMessenger()
  {
    if( !IsVulkanValidationEnabled() )
      return;

    VkDebugUtilsMessengerCreateInfoEXT createInfos
    {
      .sType{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT },
      .messageSeverity{ VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT },
      .messageType{ VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT },
      .pfnUserCallback{ debugCallback },
      .pUserData{ nullptr }
    };

    if( createDebugUtilsMessengerEXT( vulkanInstance_, &createInfos, nullptr, &debugMessenger_ ) != VK_SUCCESS )
      throw std::runtime_error{ "Error while setup the debug messenger" };
  }

  void appendProductionExtensionsIn( std::vector< const char * > &extensions )
  {
    uint32_t glfwExtensionCount = 0u;
    auto rawGlfwExtensions = glfwGetRequiredInstanceExtensions( &glfwExtensionCount );

    if( rawGlfwExtensions == nullptr )
      throw std::runtime_error{ "Error while querying glfw required vulkan extensions" };

    for( std::size_t i = 0; i < glfwExtensionCount; ++i )
      extensions.push_back( rawGlfwExtensions[ i ] );
  }

  void appendValidationExtensionsIn( std::vector< const char * > &extensions )
  {
    std::for_each( std::cbegin( vulkanValidationExtensions_ ),
                   std::cend( vulkanValidationExtensions_ ),
                   [ &extensions ]( const char *const &extensionName ) mutable { extensions.push_back( extensionName ); } );
  }

  auto getEnabledExtensions()
  {
    std::vector< const char *> enabledExtensions;

    appendProductionExtensionsIn( enabledExtensions );

    if( IsVulkanValidationEnabled() )
      appendValidationExtensionsIn( enabledExtensions );

    return enabledExtensions;
  }

  auto getEnabledLayers()
  {
    std::vector< const char * > enabledLayers;

    if( IsVulkanValidationEnabled() )
      std::for_each( std::cbegin( vulkanValidationLayers_ ),
                     std::cend( vulkanValidationLayers_ ),
                     [ &enabledLayers ]( const char *const &layerName ) mutable { enabledLayers.push_back( layerName ); } );

    return enabledLayers;
  }

  void createInstance()
  {
    VkApplicationInfo appInfo
    {
      .sType{ VK_STRUCTURE_TYPE_APPLICATION_INFO },
      .pApplicationName{ "Hello Triangle" },
      .applicationVersion{ VK_MAKE_VERSION( 1, 0, 0 ) },
      .pEngineName{ "No Engine" },
      .engineVersion{ VK_MAKE_VERSION( 0, 0, 0 ) },
      .apiVersion{ VK_API_VERSION_1_2 }
    };

    auto enabledExtensions = getEnabledExtensions();
    auto enabledLayers = getEnabledLayers();

    VkInstanceCreateInfo instanceInfo
    {
      .sType{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO },
      .pApplicationInfo{ &appInfo },
      .enabledLayerCount{ static_cast< std::uint32_t >( enabledLayers.size() ) },
      .ppEnabledLayerNames{ enabledLayers.data() },
      .enabledExtensionCount{ static_cast< std::uint32_t >( enabledExtensions.size() ) },
      .ppEnabledExtensionNames{ enabledExtensions.data() }
    };

    if( vkCreateInstance( &instanceInfo, nullptr, &vulkanInstance_ ) != VK_SUCCESS )
      throw std::runtime_error{ "Error while creating vulkan instance" };
  }

  void checkValidationSupport()
  {
    checkSupportedValidationLayers();
    checkSupportedValidationExtensions();
  }

  void checkSupportedValidationLayers()
  {
    if( !IsVulkanValidationEnabled() )
      return;

    std::uint32_t availableLayerCount = 0u;

    if( vkEnumerateInstanceLayerProperties( &availableLayerCount, nullptr ) != VK_SUCCESS )
      throw std::runtime_error{ "Error while counting vulkan instance available layers" };

    std::vector< VkLayerProperties > instanceLayers{ availableLayerCount };

    if( vkEnumerateInstanceLayerProperties( &availableLayerCount, instanceLayers.data() ) != VK_SUCCESS )
      throw std::runtime_error{ "Error while querying vulkan instance available layers" };

    std::for_each( std::cbegin( vulkanValidationLayers_ ),
                   std::cend( vulkanValidationLayers_ ),
                   [ &instanceLayers ]( std::string_view validationLayer )
    {
      if( std::find_if( std::cbegin( instanceLayers ),
                        std::cend( instanceLayers ),
                        [ &validationLayer ]( const VkLayerProperties &layerProperties )
      {
        return validationLayer.compare( layerProperties.layerName ) == 0;
      } ) == std::cend( instanceLayers ) )
        throw std::runtime_error{ "Error while querying for unsupported required validation layer" };
    } );
  }

  void checkSupportedValidationExtensions()
  {
    if( !IsVulkanValidationEnabled() )
      return;

    std::uint32_t availableExtensionCount = 0u;

    if( vkEnumerateInstanceExtensionProperties( nullptr, &availableExtensionCount, nullptr ) != VK_SUCCESS )
      throw std::runtime_error{ "Error while counting vulkan instance available extensions" };

    std::vector<VkExtensionProperties> instanceExtensions{ availableExtensionCount };

    if( vkEnumerateInstanceExtensionProperties( nullptr, &availableExtensionCount, instanceExtensions.data() ) != VK_SUCCESS )
      throw std::runtime_error{ "Error while querying vulkan instance available extensions" };

    std::for_each( std::cbegin( vulkanValidationExtensions_ ),
                   std::cend( vulkanValidationExtensions_ ),
                   [ &instanceExtensions ]( std::string_view validationExtension )
    {
      if( std::find_if( std::cbegin( instanceExtensions ),
                        std::cend( instanceExtensions ),
                        [ &validationExtension ]( const VkExtensionProperties &extensionProperties )
      {
        return validationExtension.compare( extensionProperties.extensionName ) == 0;
      } ) == std::cend( instanceExtensions ) )
        throw std::runtime_error{ "Error while querying for unsupported required validation extension" };
    } );
  }

  void mainLoop()
  {
    while( glfwWindowShouldClose( window_ ) != GLFW_TRUE )
    {
      glfwPollEvents();
      drawFrame();
    }

    vkDeviceWaitIdle( logicalDevice_ );
  }

  void synchronizeFrames( std::uint32_t imageIndex )
  {
    // Check if a previous frame is using this image (i.e. there is its fence to wait on)
    if( inFlightImageFences_[ imageIndex ] != VK_NULL_HANDLE )
      vkWaitForFences( logicalDevice_, 1, &inFlightImageFences_[ imageIndex ], VK_TRUE, std::numeric_limits< std::uint64_t >::max() );

    // Mark the image as now being in use by this frame
    inFlightImageFences_[ imageIndex ] = inFlightFences_[ currentFrame_ ];
  }

  auto acquireNextImage()
  {
    std::uint32_t imageIndex{};
    auto acquireNextResult = vkAcquireNextImageKHR( logicalDevice_,
                                                    swapChain_,
                                                    std::numeric_limits< std::uint64_t >::max(),
                                                    imageAvailableSemaphore_[ currentFrame_ ],
                                                    VK_NULL_HANDLE,
                                                    &imageIndex );

    if( acquireNextResult == VK_ERROR_OUT_OF_DATE_KHR )
    {
      recreateSwapChain();
      return imageIndex;
    }
    else if( acquireNextResult != VK_SUCCESS && acquireNextResult != VK_SUBOPTIMAL_KHR )
      throw std::runtime_error{ "Error failed to acquire swap chain image!" };

    synchronizeFrames( imageIndex );

    return imageIndex;
  }

  template< std::size_t N >
  void submitPresentationQueue( std::uint32_t imageIndex, const VkSemaphore( &signalSemaphores )[ N ] )
  {
    VkSwapchainKHR swapChains[] = { swapChain_ };
    VkPresentInfoKHR presentInfo
    {
      .sType{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR },
      .waitSemaphoreCount{ sizeof( signalSemaphores ) / sizeof( VkSemaphore ) },
      .pWaitSemaphores{ signalSemaphores },
      .swapchainCount{ sizeof( swapChains ) / sizeof( VkSwapchainKHR ) },
      .pSwapchains{ swapChains },
      .pImageIndices{ &imageIndex },
      .pResults{ nullptr } // Optional
    };

    auto presentResult = vkQueuePresentKHR( presentationQueue_, &presentInfo );

    if( presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || framebufferResized_ )
    {
      framebufferResized_ = false;
      recreateSwapChain();
    }
    else if( presentResult != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to present swap chain image!" };
  }

  template< std::size_t WaitSemaphoreCount, std::size_t SignalSemaphoreCount >
  void submitGraphicQueue( std::uint32_t imageIndex,
                           const VkSemaphore( &waitSemaphores )[ WaitSemaphoreCount ],
                           const VkSemaphore( &signalSemaphores )[ SignalSemaphoreCount ] )
  {
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submitInfo
    {
      .sType{ VK_STRUCTURE_TYPE_SUBMIT_INFO },
      .waitSemaphoreCount{ WaitSemaphoreCount },
      .pWaitSemaphores{ waitSemaphores },
      .pWaitDstStageMask{ waitStages },
      .commandBufferCount{ 1 },
      .pCommandBuffers{ &commandBuffers_[ imageIndex ] },
      .signalSemaphoreCount{ SignalSemaphoreCount },
      .pSignalSemaphores{ signalSemaphores }
    };

    if( vkQueueSubmit( graphicsQueue_, 1, &submitInfo, inFlightFences_[ currentFrame_ ] ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to submit draw command buffer!" };
  }

  void updateUniformBuffer( std::uint32_t imageIndex )
  {
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float delta = std::chrono::duration< float, std::chrono::seconds::period >( currentTime - startTime ).count();

    UniformBufferObject ubo
    {
      .model
      {
          glm::rotate( glm::mat4( 1.0f ),
                       delta * glm::radians( 90.0f ),
                       glm::vec3( 0.0f, 0.0f, 1.0f ) )
      },
      .view
      {
        glm::lookAt( glm::vec3( 2.0f, 2.0f, 2.0f ),
                     glm::vec3( 0.0f, 0.0f, 0.0f ),
                     glm::vec3( 0.0f, 0.0f, 1.0f ) )
      },
      .proj
      {
        glm::perspective( glm::radians( 20.0f ),
                          swapChainExtent_.width / static_cast< float >( swapChainExtent_.height ),
                          0.1f,
                          9.9f )
      }
    };

    ubo.proj[ 1 ][ 1 ] *= -1;

    void *data;
    vkMapMemory( logicalDevice_, uniformBuffersMemory_[ imageIndex ], 0, sizeof( UniformBufferObject ), 0, &data );
    memcpy( data, &ubo, sizeof( UniformBufferObject ) );
    vkUnmapMemory( logicalDevice_, uniformBuffersMemory_[ imageIndex ] );
  }

  void drawFrame()
  {
    vkWaitForFences( logicalDevice_, 1, &inFlightFences_[ currentFrame_ ], VK_TRUE, std::numeric_limits< std::uint64_t >::max() );

    auto imageIndex = acquireNextImage();

    updateUniformBuffer( imageIndex );

    vkResetFences( logicalDevice_, 1, &inFlightFences_[ currentFrame_ ] );

    VkSemaphore waitSemaphores[] = { imageAvailableSemaphore_[ currentFrame_ ] };
    VkSemaphore signalSemaphores[] = { renderFinishedSemaphore_[ currentFrame_ ] };

    submitGraphicQueue( imageIndex, waitSemaphores, signalSemaphores );
    submitPresentationQueue( imageIndex, signalSemaphores );

    currentFrame_ = ( currentFrame_ + 1 ) % maxFrameInFlight_;
  }

  void cleanupSwapChain()
  {
    for( auto &&framebuffer : swapChainFramebuffers_ )
      vkDestroyFramebuffer( logicalDevice_, framebuffer, nullptr );

    vkFreeCommandBuffers( logicalDevice_, graphicCommandPool_, static_cast< std::uint32_t >( commandBuffers_.size() ), commandBuffers_.data() );

    for( auto &&graphicsPipeline : graphicPipelines_ )
      vkDestroyPipeline( logicalDevice_, graphicsPipeline, nullptr );

    vkDestroyPipelineLayout( logicalDevice_, pipelineLayout_, nullptr );
    vkDestroyRenderPass( logicalDevice_, renderPass_, nullptr );

    for( auto &&imageView : swapChainImageViews_ )
      vkDestroyImageView( logicalDevice_, imageView, nullptr );

    vkDestroySwapchainKHR( logicalDevice_, swapChain_, nullptr );

    for( auto &&uniformBuffer : uniformBuffers_ )
      vkDestroyBuffer( logicalDevice_, uniformBuffer, nullptr );

    for( auto &&uniformBufferMemory : uniformBuffersMemory_ )
      vkFreeMemory( logicalDevice_, uniformBufferMemory, nullptr );

    vkDestroyDescriptorPool( logicalDevice_, descriptorPool_, nullptr );
  }

  void cleanupSynchronizationObjects()
  {
    for( std::uint8_t i = 0; i < maxFrameInFlight_; ++i )
    {
      vkDestroySemaphore( logicalDevice_, renderFinishedSemaphore_[ i ], nullptr );
      vkDestroySemaphore( logicalDevice_, imageAvailableSemaphore_[ i ], nullptr );
      vkDestroyFence( logicalDevice_, inFlightFences_[ i ], nullptr );
    }
  }

  void cleanup()
  {
    cleanupSwapChain();
    vkDestroyDescriptorSetLayout( logicalDevice_, descriptorSetLayout_, nullptr );
    vkDestroyBuffer( logicalDevice_, indexBuffer_, nullptr );
    vkFreeMemory( logicalDevice_, indexBufferMemory_, nullptr );
    vkDestroyBuffer( logicalDevice_, vertexBuffer_, nullptr );
    vkFreeMemory( logicalDevice_, vertexBufferMemory_, nullptr );
    cleanupSynchronizationObjects();
    vkDestroyCommandPool( logicalDevice_, graphicCommandPool_, nullptr );
    vkDestroyCommandPool( logicalDevice_, transfertCommandPool_, nullptr );
    vkDestroyDevice( logicalDevice_, nullptr );
    destroyDebugUtilsMessengerEXT( vulkanInstance_, debugMessenger_, nullptr );
    vkDestroySurfaceKHR( vulkanInstance_, surface_, nullptr );
    vkDestroyInstance( vulkanInstance_, nullptr );
    glfwDestroyWindow( window_ );
    glfwTerminate();
  }

private:
  static constexpr bool IsVulkanValidationEnabled()
  {
    return
#ifdef NDEBUG
      false;
#else
      true;
#endif // NDEBUG
  }

  static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback( VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                       VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                       const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                                       void *pUserData )
  {
    if( !IsVulkanValidationEnabled() )
      return {};

    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
  }

  static VkResult createDebugUtilsMessengerEXT( VkInstance instance,
                                                const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
                                                const VkAllocationCallbacks *pAllocator,
                                                VkDebugUtilsMessengerEXT *pDebugMessenger )
  {
    if( !IsVulkanValidationEnabled() )
      return {};

    auto func = reinterpret_cast< PFN_vkCreateDebugUtilsMessengerEXT >( vkGetInstanceProcAddr( instance, "vkCreateDebugUtilsMessengerEXT" ) );

    if( func != nullptr )
      return func( instance, pCreateInfo, pAllocator, pDebugMessenger );

    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }

  static void destroyDebugUtilsMessengerEXT( VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks *pAllocator )
  {
    if( !IsVulkanValidationEnabled() )
      return;

    auto func = reinterpret_cast< PFN_vkDestroyDebugUtilsMessengerEXT >( vkGetInstanceProcAddr( instance, "vkDestroyDebugUtilsMessengerEXT" ) );

    if( func != nullptr )
      func( instance, debugMessenger, pAllocator );
  }

  static std::vector< char > loadShaderModule( const std::filesystem::path &path )
  {
    std::ifstream file{ path, std::ios::ate | std::ios::binary };

    if( !file.is_open() )
      throw std::runtime_error{ "Error while loading shader module" };

    const auto fileSize = static_cast< std::size_t >( file.tellg() );
    std::vector< char > shaderModuleContent;
    shaderModuleContent.resize( fileSize );

    file.seekg( 0 );
    file.read( shaderModuleContent.data(), fileSize );

    file.close();

    return shaderModuleContent;
  }

  static void framebufferResizeCallback( GLFWwindow *window, int, int )
  {
    auto app = reinterpret_cast< VulkanApplication * >( glfwGetWindowUserPointer( window ) );
    app->framebufferResized_ = true;
  }

private:
  struct RequiredQueueFamilyIndices
  {
    std::optional< std::uint32_t > graphicsQueueFamilyIndex;
    std::optional< std::uint32_t > presentationQueueFamilyIndex;
    std::optional< std::uint32_t > transfertQueueFamilyIndex;

    constexpr bool isComplete() const noexcept
    {
      return ( graphicsQueueFamilyIndex.has_value()
               && presentationQueueFamilyIndex.has_value()
               && transfertQueueFamilyIndex.has_value() );
    }

    std::set< std::uint32_t > toSet() const noexcept
    {
      std::set< std::uint32_t > set;

      if( isComplete() )
        set.insert( {
        graphicsQueueFamilyIndex.value(),
        presentationQueueFamilyIndex.value(),
        transfertQueueFamilyIndex.value() } );

      return set;
    }
  };

  struct SwapChainSupportDetails
  {
    VkSurfaceCapabilitiesKHR surfaceCapabilities_;
    std::vector< VkSurfaceFormatKHR > surfaceFormats_;
    std::vector< VkPresentModeKHR > presentModes_;

    bool isComplete() const noexcept
    {
      return !surfaceFormats_.empty() && !presentModes_.empty();
    }
  };

  struct Vertex
  {
    glm::vec2 position;
    glm::vec3 color;

    static VkVertexInputBindingDescription getBindingDescription()
    {
      return VkVertexInputBindingDescription
      {
        .binding{ 0 },
        .stride{ sizeof( Vertex ) },
        .inputRate{ VK_VERTEX_INPUT_RATE_VERTEX }
      };
    }

    static std::array< VkVertexInputAttributeDescription, 2 > getAttributeDescriptions()
    {
      return std::array< VkVertexInputAttributeDescription, 2 >
      {
        {
          {
            .location{ 0 },
              .binding{ 0 },
              .format{ VK_FORMAT_R32G32_SFLOAT },
              .offset{ offsetof( Vertex, position ) },
          },
          {
            .location{ 1 },
            .binding{ 0 },
            .format{ VK_FORMAT_R32G32B32_SFLOAT },
            .offset{ offsetof( Vertex, color ) }
          }
        }
      };
    }
  };

  struct UniformBufferObject
  {
    alignas( 16 ) glm::mat4 model;
    alignas( 16 ) glm::mat4 view;
    alignas( 16 ) glm::mat4 proj;
  };

private:
  std::filesystem::path applicationPath_;
  GLFWwindow *window_{};
  VkInstance vulkanInstance_{};
  VkDebugUtilsMessengerEXT debugMessenger_{};
  VkSurfaceKHR surface_{};
  VkPhysicalDevice physicalDevice_{};
  VkDevice logicalDevice_{};
  RequiredQueueFamilyIndices requiredQueueFamilyIndices_{};
  VkQueue graphicsQueue_{};
  VkQueue presentationQueue_{};
  VkQueue transfertQueue_{};
  SwapChainSupportDetails swapChainSupportDetails_{};
  VkSwapchainKHR swapChain_{};
  VkSurfaceFormatKHR swapChainSurfaceFormat_{};
  VkExtent2D swapChainExtent_{};
  std::vector< VkImage > swapChainImages_;
  std::vector< VkImageView > swapChainImageViews_;
  VkRenderPass renderPass_;
  VkDescriptorSetLayout descriptorSetLayout_;
  VkPipelineLayout pipelineLayout_;
  std::vector< VkPipeline > graphicPipelines_;
  std::vector< VkFramebuffer > swapChainFramebuffers_;
  VkCommandPool graphicCommandPool_;
  VkCommandPool transfertCommandPool_;
  std::vector< VkCommandBuffer > commandBuffers_;
  std::vector< VkSemaphore > imageAvailableSemaphore_;
  std::vector< VkSemaphore > renderFinishedSemaphore_;
  std::uint8_t currentFrame_{ 0 };
  std::vector< VkFence > inFlightFences_;
  std::vector< VkFence > inFlightImageFences_;
  std::uint8_t maxFrameInFlight_{ 0 };
  bool framebufferResized_{ false };
  VkBuffer vertexBuffer_;
  VkDeviceMemory vertexBufferMemory_{};
  VkBuffer indexBuffer_;
  VkDeviceMemory indexBufferMemory_;
  std::vector< VkBuffer > uniformBuffers_;
  std::vector< VkDeviceMemory > uniformBuffersMemory_;
  VkDescriptorPool descriptorPool_;
  std::vector< VkDescriptorSet > descriptorSets_;

private:
  inline static constexpr int windowWidth_{ 800 };
  inline static constexpr int windowHeight_{ 600 };

  inline static const std::vector< const char * > vulkanValidationLayers_
  {
    "VK_LAYER_KHRONOS_validation",
    "VK_LAYER_LUNARG_standard_validation"
  };

  inline static const std::vector< const char * > vulkanValidationExtensions_
  {
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME
  };

  inline static const std::vector< const char * > vulkanProductionExtensions_
  {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
  };

  inline static const std::vector< Vertex > vertices_
  {
    { { -0.5f, -0.5f}, { 1.0f, 0.0f, 0.0f} },
    { { 0.5f, -0.5f}, { 0.0f, 1.0f, 0.0f} },
    { { 0.5f, 0.5f}, { 0.0f, 0.0f, 1.0f} },
    { { -0.5f, 0.5f}, { 1.0f, 1.0f, 1.0f} }
  };

  inline static const std::vector< std::uint16_t > indices_{ 0,1,2,2,3,0 };
};

int main( int argc, char *argv[] )
{
  // assuming program path is specified in argv[ 0 ], that might not necessary be the case, fragile assumption but working in that context
  VulkanApplication app{ argv[ 0 ] };

  try
  {
    app.run();
  }
  catch( const std::exception &e )
  {
    std::cerr << e.what() << std::endl;

    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}