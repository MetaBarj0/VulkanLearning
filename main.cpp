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

class HelloTriangleApplication
{
public:
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
      throw std::runtime_error{ "Error when intialize GLFW" };

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
  }

  void createImageViews()
  {
    swapChainImageViews_.resize( swapChainImages_.size() );

    for( std::size_t i = 0; i < swapChainImages_.size(); ++i )
    {
      VkImageViewCreateInfo createInfo
      {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = swapChainImages_[ i ],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = swapChainSurfaceFormat_.format,
        .components
        {
          .r = VK_COMPONENT_SWIZZLE_IDENTITY,
          .g = VK_COMPONENT_SWIZZLE_IDENTITY,
          .b = VK_COMPONENT_SWIZZLE_IDENTITY,
          .a = VK_COMPONENT_SWIZZLE_IDENTITY
        },
        .subresourceRange
        {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel = 0,
          .levelCount = 1,
          .baseArrayLayer = 0,
          .layerCount = 1
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

    VkSwapchainCreateInfoKHR createInfo
    {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = surface_,
      .minImageCount = imageCount,
      .imageFormat = swapChainSurfaceFormat_.format,
      .imageColorSpace = swapChainSurfaceFormat_.colorSpace,
      .imageExtent = swapChainExtent_,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .preTransform = swapChainSupportDetails_.surfaceCapabilities_.currentTransform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = presentMode,
      .clipped = VK_TRUE,
      .oldSwapchain = VK_NULL_HANDLE
    };

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
                                         .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                         .queueFamilyIndex = queueFamilyIndex,
                                         .queueCount = 1,
                                         .pQueuePriorities = queuePriorities
                                       } );

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo deviceCreateInfo
    {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = static_cast< std::uint32_t >( allQueueCreateInfo.size() ),
      .pQueueCreateInfos = allQueueCreateInfo.data(),
      .enabledExtensionCount = static_cast< std::uint32_t >( vulkanProductionExtensions_.size() ),
      .ppEnabledExtensionNames = vulkanProductionExtensions_.data(),
      .pEnabledFeatures = &deviceFeatures
    };

    if( IsVulkanValidationEnabled() )
    {
      deviceCreateInfo.enabledLayerCount = static_cast< std::uint32_t >( vulkanValidationLayers_.size() );
      deviceCreateInfo.ppEnabledLayerNames = vulkanValidationLayers_.data();
    }
    else
      deviceCreateInfo.enabledLayerCount = 0;

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
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = debugCallback,
      .pUserData = nullptr
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
    if( !IsVulkanValidationEnabled() )
      return;

    std::for_each( std::cbegin( vulkanValidationExtensions_ ),
                   std::cend( vulkanValidationExtensions_ ),
                   [ &extensions ]( const char *const &extensionName ) mutable { extensions.push_back( extensionName ); } );
  }

  auto getEnabledExtensions()
  {
    std::vector< const char *> enabledExtensions;

    appendProductionExtensionsIn( enabledExtensions );
    appendValidationExtensionsIn( enabledExtensions );

    return enabledExtensions;
  }

  auto getEnabledLayers()
  {
    std::vector< const char * > enabledLayers;

    if( IsVulkanValidationEnabled() )
    {
      std::for_each( std::cbegin( vulkanValidationLayers_ ),
                     std::cend( vulkanValidationLayers_ ),
                     [ &enabledLayers ]( const char *const &layerName ) mutable { enabledLayers.push_back( layerName ); } );
    }

    return enabledLayers;
  }

  void createInstance()
  {
    VkApplicationInfo appInfo
    {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "Hello Triangle",
      .applicationVersion = VK_MAKE_VERSION( 1, 0, 0 ),
      .pEngineName = "No Engine",
      .engineVersion = VK_MAKE_VERSION( 0, 0, 0 ),
      .apiVersion = VK_API_VERSION_1_2
    };

    auto enabledExtensions = getEnabledExtensions();
    auto enabledLayers = getEnabledLayers();

    VkInstanceCreateInfo instanceInfo
    {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &appInfo,
      .enabledLayerCount = static_cast< std::uint32_t >( enabledLayers.size() ),
      .ppEnabledLayerNames = enabledLayers.data(),
      .enabledExtensionCount = static_cast< std::uint32_t >( enabledExtensions.size() ),
      .ppEnabledExtensionNames = enabledExtensions.data()
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

    std::vector<VkLayerProperties> instanceLayers{ availableLayerCount };

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
      glfwPollEvents();
  }

  void cleanup()
  {
    for( auto imageView : swapChainImageViews_ )
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
#ifndef NDEBUG
      true;
#else
      false;
#endif // !NDEBUG
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

private:
  inline static constexpr int windowWidth_ = 800;
  inline static constexpr int windowHeight_ = 600;

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

int main()
{
  HelloTriangleApplication app;

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