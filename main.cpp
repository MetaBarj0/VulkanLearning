// https://vulkan-tutorial.com
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

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

class HelloTriangleApplication
{
public:
  HelloTriangleApplication( std::filesystem::path path ) :
    applicationPath_{ path },
    window_{},
    vulkanInstance_{},
    debugMessenger_{},
    surface_{},
    physicalDevice_{},
    logicalDevice_{},
    requiredQueueFamilyIndices_{},
    graphicsQueue_{},
    presentationQueue_{},
    swapChainSupportDetails_{},
    swapChain_{},
    swapChainSurfaceFormat_{},
    swapChainExtent_{},
    swapChainImages_{},
    swapChainImageViews_{} {}

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
    glfwWindowHint( GLFW_RESIZABLE, GLFW_FALSE );

    window_ = glfwCreateWindow( windowWidth_, windowHeight_, "Vulkan", nullptr, nullptr );

    if( window_ == nullptr )
      throw std::runtime_error{ "Error when creating GLFW window" };
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
    createGraphicsPipeline();
    createFramebuffers();
    createCommandPool();
    createCommandBuffers();
    createSynchronizationObjects();
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

  void createCommandBuffers()
  {
    commandBuffers_.resize( swapChainFramebuffers_.size() );

    VkCommandBufferAllocateInfo allocInfo
    {
      .sType{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO },
      .commandPool{ commandPool_ },
      .level{ VK_COMMAND_BUFFER_LEVEL_PRIMARY },
      .commandBufferCount{ static_cast< std::uint32_t >( commandBuffers_.size() ) }
    };

    if( vkAllocateCommandBuffers( logicalDevice_, &allocInfo, commandBuffers_.data() ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to allocate command buffers!" };

    for( std::size_t i = 0; i < commandBuffers_.size(); i++ )
    {
      VkCommandBufferBeginInfo beginInfo
      {
        .sType{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO },
        .flags{ 0 }, // Optional
        .pInheritanceInfo{ nullptr } // Optional
      };

      if( vkBeginCommandBuffer( commandBuffers_[ i ], &beginInfo ) != VK_SUCCESS )
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
        .framebuffer{ swapChainFramebuffers_[ i ] },
        .renderArea
        {
          .offset{ 0, 0 },
          .extent{ swapChainExtent_ }
        },
        .clearValueCount{ sizeof( clearColors ) / sizeof( VkClearValue ) },
        .pClearValues{ clearColors }
      };

      vkCmdBeginRenderPass( commandBuffers_[ i ], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE );
      vkCmdBindPipeline( commandBuffers_[ i ], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelines_.front() );
      vkCmdDraw( commandBuffers_[ i ], 3, 1, 0, 0 );
      vkCmdEndRenderPass( commandBuffers_[ i ] );

      if( vkEndCommandBuffer( commandBuffers_[ i ] ) != VK_SUCCESS )
        throw std::runtime_error{ "Error failed to record command buffer!" };
    }
  }

  void createCommandPool()
  {
    VkCommandPoolCreateInfo poolInfo
    {
      .sType{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO },
      .flags{ 0 }, // Optional
      .queueFamilyIndex{ requiredQueueFamilyIndices_.graphicsQueueFamilyIndex.value() }
    };

    if( vkCreateCommandPool( logicalDevice_, &poolInfo, nullptr, &commandPool_ ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to create command pool!" };
  }

  void createFramebuffers()
  {
    swapChainFramebuffers_.resize( swapChainImageViews_.size() );

    for( std::size_t i = 0; i < swapChainImageViews_.size(); i++ )
    {
      VkImageView attachments[]
      {
        swapChainImageViews_[ i ]
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

      if( vkCreateFramebuffer( logicalDevice_, &framebufferInfo, nullptr, &swapChainFramebuffers_[ i ] ) != VK_SUCCESS )
        throw std::runtime_error{ "failed to create framebuffer!" };
    }
  }

  void createRenderPass()
  {
    VkAttachmentReference colorAttachmentReferences[]
    {
      {
        .attachment{ 0 },
        .layout{ VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
      }
    };

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

  void createGraphicsPipeline()
  {
    auto shadersDirectory = applicationPath_.parent_path() / "shaders";

    auto vertexShaderCode = loadShaderModule( shadersDirectory / "vert.spv" );
    auto fragmentShaderCode = loadShaderModule( shadersDirectory / "frag.spv" );

    VkShaderModule vertexShaderModule = createShaderModule( std::move( vertexShaderCode ) );
    VkShaderModule fragmentShaderModule = createShaderModule( std::move( fragmentShaderCode ) );

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

    VkPipelineVertexInputStateCreateInfo vertexInputInfo
    {
      .sType{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO },
      .vertexBindingDescriptionCount{ 0 },
      .pVertexBindingDescriptions{ nullptr }, // Optional
      .vertexAttributeDescriptionCount{ 0 },
      .pVertexAttributeDescriptions{ nullptr } // Optional
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
      .frontFace{ VK_FRONT_FACE_CLOCKWISE },
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

    VkDynamicState dynamicStates[]
    {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_LINE_WIDTH
    };

    VkPipelineDynamicStateCreateInfo dynamicState
    {
      .sType{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO },
      .dynamicStateCount{ sizeof( dynamicStates ) / sizeof( VkDynamicState ) },
      .pDynamicStates{ dynamicStates }
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo
    {
      .sType{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO },
      .setLayoutCount{ 0 }, // Optional
      .pSetLayouts{ nullptr }, // Optional
      .pushConstantRangeCount{ 0 }, // Optional
      .pPushConstantRanges{ nullptr }, // Optional
    };

    if( vkCreatePipelineLayout( logicalDevice_, &pipelineLayoutInfo, nullptr, &pipelineLayout_ ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to create pipeline layout!" };

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

    graphicsPipelines_.resize( sizeof( pipelinesInfo ) / sizeof( VkGraphicsPipelineCreateInfo ) );

    if( vkCreateGraphicsPipelines( logicalDevice_, VK_NULL_HANDLE, 1, pipelinesInfo, nullptr, graphicsPipelines_.data() ) != VK_SUCCESS )
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

  void createImageViews()
  {
    swapChainImageViews_.resize( swapChainImages_.size() );

    for( std::size_t i = 0; i < swapChainImages_.size(); ++i )
    {
      VkImageViewCreateInfo createInfo
      {
        .sType{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO },
        .image{ swapChainImages_[ i ] },
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

      if( vkCreateImageView( logicalDevice_, &createInfo, nullptr, &swapChainImageViews_[ i ] ) != VK_SUCCESS )
        throw std::runtime_error{ "Error failed to create image views!" };
    }
  }

  void createSwapChain()
  {
    setupSwapChainSurfaceFormat();
    setupSwapChainExtent();

    VkPresentModeKHR presentMode = chooseSwapPresentMode();
    std::uint32_t imageCount = swapChainSupportDetails_.surfaceCapabilities_.minImageCount + 1;

    if( swapChainSupportDetails_.surfaceCapabilities_.maxImageCount > 0 && imageCount > swapChainSupportDetails_.surfaceCapabilities_.maxImageCount )
      imageCount = swapChainSupportDetails_.surfaceCapabilities_.maxImageCount;

    maxFrameInFlight_ = imageCount;

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
      .presentMode{ presentMode },
      .clipped{ VK_TRUE },
      .oldSwapchain{ VK_NULL_HANDLE }
    };

    if( requiredQueueFamilyIndices_.graphicsQueueFamilyIndex != requiredQueueFamilyIndices_.presentationQueueFamilyIndex )
    {
      uint32_t queueFamilyIndices[]
      {
        requiredQueueFamilyIndices_.graphicsQueueFamilyIndex.value(),
        requiredQueueFamilyIndices_.presentationQueueFamilyIndex.value()
      };

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

    VkExtent2D actualExtent{ windowWidth_, windowHeight_ };

    actualExtent.width = std::max( capabilities.minImageExtent.width, std::min( capabilities.maxImageExtent.width, actualExtent.width ) );
    actualExtent.height = std::max( capabilities.minImageExtent.height, std::min( capabilities.maxImageExtent.height, actualExtent.height ) );

    swapChainExtent_ = actualExtent;
  }

  void createSurface()
  {
    if( glfwCreateWindowSurface( vulkanInstance_, window_, nullptr, &surface_ ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to create window surface!" };
  }

  void createLogicalDevice()
  {
    std::vector< VkDeviceQueueCreateInfo > allQueueCreateInfo;

    float queuePriorities[] = { 1.0f };

    for( std::uint32_t queueFamilyIndex : requiredQueueFamilyIndices_.toSet() )
      allQueueCreateInfo.emplace_back( VkDeviceQueueCreateInfo
                                       {
                                         .sType{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO },
                                         .queueFamilyIndex{ queueFamilyIndex },
                                         .queueCount{ 1 },
                                         .pQueuePriorities{ queuePriorities }
                                       } );

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
    setupRequiredQueueFamiliesForDevice( device );
    setupSwapChainSupportForDevice( device );

    bool extensionsSupported = isDeviceSupportingRequiredExtensions( device );

    return requiredQueueFamilyIndices_.isComplete() && extensionsSupported && swapChainSupportDetails_.isComplete();
  }

  void setupSwapChainSupportForDevice( VkPhysicalDevice device )
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
    {
      if( std::find_if( std::cbegin( availableExtensions ),
                        std::cend( availableExtensions ),
                        [ &extension ]( const VkExtensionProperties &extensionProperties )
      {
        return extension.compare( extensionProperties.extensionName ) == 0;
      } ) == std::cend( availableExtensions ) )
        return false;
    }

    return true;
  }

  void setupRequiredQueueFamiliesForDevice( VkPhysicalDevice device )
  {
    std::uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, nullptr );

    std::vector< VkQueueFamilyProperties > queueFamilies{ queueFamilyCount };
    vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, queueFamilies.data() );

    std::uint32_t queueFamilyIndex = 0;
    for( const auto &queueFamily : queueFamilies )
    {
      if( queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT )
        requiredQueueFamilyIndices_.graphicsQueueFamilyIndex = queueFamilyIndex;

      VkBool32 hasPresentationSupport = false;
      vkGetPhysicalDeviceSurfaceSupportKHR( device, queueFamilyIndex, surface_, &hasPresentationSupport );

      if( hasPresentationSupport )
        requiredQueueFamilyIndices_.presentationQueueFamilyIndex = queueFamilyIndex;

      if( requiredQueueFamilyIndices_.isComplete() )
        break;

      queueFamilyIndex++;
    }
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

    extensions.assign( rawGlfwExtensions, rawGlfwExtensions + glfwExtensionCount );
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

  void drawFrame()
  {
    vkWaitForFences( logicalDevice_, 1, &inFlightFences_[ currentFrame_ ], VK_TRUE, std::numeric_limits< std::uint64_t >::max() );

    std::uint32_t imageIndex;
    vkAcquireNextImageKHR( logicalDevice_,
                           swapChain_,
                           std::numeric_limits< std::uint64_t >::max(),
                           imageAvailableSemaphore_[ currentFrame_ ],
                           VK_NULL_HANDLE,
                           &imageIndex );

    // Check if a previous frame is using this image (i.e. there is its fence to wait on)
    if( inFlightImageFences_[ imageIndex ] != VK_NULL_HANDLE )
      vkWaitForFences( logicalDevice_, 1, &inFlightImageFences_[ imageIndex ], VK_TRUE, std::numeric_limits< std::uint64_t >::max() );

    // Mark the image as now being in use by this frame
    inFlightImageFences_[ imageIndex ] = inFlightFences_[ currentFrame_ ];

    vkResetFences( logicalDevice_, 1, &inFlightFences_[ currentFrame_ ] );

    VkSemaphore waitSemaphores[] = { imageAvailableSemaphore_[ currentFrame_ ] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { renderFinishedSemaphore_[ currentFrame_ ] };
    VkSubmitInfo submitInfo
    {
      .sType{ VK_STRUCTURE_TYPE_SUBMIT_INFO },
      .waitSemaphoreCount{ sizeof( waitSemaphores ) / sizeof( VkSemaphore ) },
      .pWaitSemaphores{ waitSemaphores },
      .pWaitDstStageMask{ waitStages },
      .commandBufferCount{ 1 },
      .pCommandBuffers{ &commandBuffers_[ imageIndex ] },
      .signalSemaphoreCount{ sizeof( signalSemaphores ) / sizeof( VkSemaphore ) },
      .pSignalSemaphores{ signalSemaphores }
    };

    if( vkQueueSubmit( graphicsQueue_, 1, &submitInfo, inFlightFences_[ currentFrame_ ] ) != VK_SUCCESS )
      throw std::runtime_error{ "Error failed to submit draw command buffer!" };

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

    vkQueuePresentKHR( presentationQueue_, &presentInfo );

    currentFrame_ = ( currentFrame_ + 1 ) % maxFrameInFlight_;
  }

  void cleanup()
  {
    for( std::uint8_t i = 0; i < maxFrameInFlight_; ++i )
    {
      vkDestroySemaphore( logicalDevice_, renderFinishedSemaphore_[ i ], nullptr );
      vkDestroySemaphore( logicalDevice_, imageAvailableSemaphore_[ i ], nullptr );
      vkDestroyFence( logicalDevice_, inFlightFences_[ i ], nullptr );
    }

    vkDestroyCommandPool( logicalDevice_, commandPool_, nullptr );

    for( auto &&framebuffer : swapChainFramebuffers_ )
      vkDestroyFramebuffer( logicalDevice_, framebuffer, nullptr );

    for( auto &&graphicsPipeline : graphicsPipelines_ )
      vkDestroyPipeline( logicalDevice_, graphicsPipeline, nullptr );

    vkDestroyPipelineLayout( logicalDevice_, pipelineLayout_, nullptr );
    vkDestroyRenderPass( logicalDevice_, renderPass_, nullptr );

    for( auto &&imageView : swapChainImageViews_ )
      vkDestroyImageView( logicalDevice_, imageView, nullptr );

    vkDestroySwapchainKHR( logicalDevice_, swapChain_, nullptr );
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

private:
  struct RequiredQueueFamilyIndices
  {
    std::optional< std::uint32_t > graphicsQueueFamilyIndex;
    std::optional< std::uint32_t > presentationQueueFamilyIndex;

    constexpr bool isComplete() const noexcept
    {
      return graphicsQueueFamilyIndex.has_value() && presentationQueueFamilyIndex.has_value();
    }

    std::set< std::uint32_t > toSet() const noexcept
    {
      std::set< std::uint32_t > set;

      if( isComplete() )
        set.insert( { graphicsQueueFamilyIndex.value(), presentationQueueFamilyIndex.value() } );

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

private:
  std::filesystem::path applicationPath_;
  GLFWwindow *window_;
  VkInstance vulkanInstance_;
  VkDebugUtilsMessengerEXT debugMessenger_;
  VkSurfaceKHR surface_;
  VkPhysicalDevice physicalDevice_;
  VkDevice logicalDevice_;
  RequiredQueueFamilyIndices requiredQueueFamilyIndices_;
  VkQueue graphicsQueue_;
  VkQueue presentationQueue_;
  SwapChainSupportDetails swapChainSupportDetails_;
  VkSwapchainKHR swapChain_;
  VkSurfaceFormatKHR swapChainSurfaceFormat_;
  VkExtent2D swapChainExtent_;
  std::vector< VkImage > swapChainImages_;
  std::vector< VkImageView > swapChainImageViews_;
  VkRenderPass renderPass_;
  VkPipelineLayout pipelineLayout_;
  std::vector< VkPipeline > graphicsPipelines_;
  std::vector< VkFramebuffer > swapChainFramebuffers_;
  VkCommandPool commandPool_;
  std::vector< VkCommandBuffer > commandBuffers_;
  std::vector< VkSemaphore > imageAvailableSemaphore_;
  std::vector< VkSemaphore > renderFinishedSemaphore_;
  std::uint8_t currentFrame_{ 0 };
  std::vector< VkFence > inFlightFences_;
  std::vector< VkFence > inFlightImageFences_;
  std::uint8_t maxFrameInFlight_{ 0 };

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
};

int main( int argc, char *argv[] )
{
  // assuming program path is specified in argv[ 0 ], that might not necessary be the case, fragile assumption but working in that context
  HelloTriangleApplication app{ argv[ 0 ] };

  try
  {
    app.run();
  }
  catch( const std::exception & e )
  {
    std::cerr << e.what() << std::endl;

    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}