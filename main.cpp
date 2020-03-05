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

static constexpr int WINDOW_WIDTH = 800;
static constexpr int WINDOW_HEIGHT = 600;

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

    window_ = glfwCreateWindow( WINDOW_WIDTH, WINDOW_HEIGHT, "Vulkan", nullptr, nullptr );

    if( window_ == nullptr )
      throw std::runtime_error{ "Error when creating GLFW window" };
  }

  void initVulkan()
  {
    checkValidationSupport();
    createInstance();
    setupDebugMessenger();
    pickPhysicalDevice();
    createLogicalDevice();
  }

  void createLogicalDevice()
  {
    RequiredQueueFamilyIndices indices = findRequiredQueueFamilies( vkPhysicalDevice_ );

    VkDeviceQueueCreateInfo queueCreateInfo
    {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = indices.graphicsQueueFamilyIndex.value(),
      .queueCount = 1
    };

    float queuePriority[] = { 1.0f };
    queueCreateInfo.pQueuePriorities = queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo deviceCreateInfo
    {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queueCreateInfo,
      .enabledExtensionCount = 0,
      .pEnabledFeatures = &deviceFeatures
    };

    if( IsVulkanValidationEnabled() )
    {
      deviceCreateInfo.enabledLayerCount = static_cast< std::uint32_t >( VulkanValidationLayers.size() );
      deviceCreateInfo.ppEnabledLayerNames = VulkanValidationLayers.data();
    }
    else
      deviceCreateInfo.enabledLayerCount = 0;

    if( vkCreateDevice( vkPhysicalDevice_, &deviceCreateInfo, nullptr, &vkLogicalDevice_ ) != VK_SUCCESS )
      throw std::runtime_error( "Error failed to create logical device!" );

    vkGetDeviceQueue( vkLogicalDevice_, indices.graphicsQueueFamilyIndex.value(), 0, &vkGraphicsQueue_ );
  }

  void pickPhysicalDevice()
  {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices( vkInstance_, &deviceCount, nullptr );

    if( deviceCount == 0 )
      throw std::runtime_error{ "Error when attempting to find GPUs with Vulkan support!" };

    std::vector< VkPhysicalDevice > devices{ deviceCount };
    vkEnumeratePhysicalDevices( vkInstance_, &deviceCount, devices.data() );

    for( const auto &device : devices )
      if( isDeviceSuitable( device ) )
      {
        vkPhysicalDevice_ = device;
        break;
      }

    if( vkPhysicalDevice_ == VK_NULL_HANDLE )
      throw std::runtime_error( "Error failed to find a suitable GPU!" );
  }

  bool isDeviceSuitable( VkPhysicalDevice device )
  {
    RequiredQueueFamilyIndices requiredIndices = findRequiredQueueFamilies( device );

    return requiredIndices.isComplete();
  }

  struct RequiredQueueFamilyIndices
  {
    std::optional< std::uint32_t > graphicsQueueFamilyIndex;

    constexpr bool isComplete()
    {
      return graphicsQueueFamilyIndex.has_value();
    }
  };

  RequiredQueueFamilyIndices findRequiredQueueFamilies( VkPhysicalDevice device )
  {
    RequiredQueueFamilyIndices indices;

    std::uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, nullptr );

    std::vector< VkQueueFamilyProperties > queueFamilies{ queueFamilyCount };
    vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, queueFamilies.data() );

    std::uint32_t queueFamilyIndex = 0;
    for( const auto &queueFamily : queueFamilies )
    {
      if( queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT )
        indices.graphicsQueueFamilyIndex = queueFamilyIndex;

      queueFamilyIndex++;
    }

    return indices;
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

    if( createDebugUtilsMessengerEXT( vkInstance_, &createInfos, nullptr, &vkDebugMessenger_ ) != VK_SUCCESS )
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

    std::for_each( std::cbegin( VulkanValidationExtensions ),
                   std::cend( VulkanValidationExtensions ),
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
      std::for_each( std::cbegin( VulkanValidationLayers ),
                     std::cend( VulkanValidationLayers ),
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

    if( vkCreateInstance( &instanceInfo, nullptr, &vkInstance_ ) != VK_SUCCESS )
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

    std::for_each( std::cbegin( VulkanValidationLayers ),
                   std::cend( VulkanValidationLayers ),
                   [ &instanceLayers ]( std::string_view validationLayer )
    {
      auto it = std::find_if( std::cbegin( instanceLayers ),
                              std::cend( instanceLayers ),
                              [ &validationLayer ]( const VkLayerProperties &layerProperties )
      {
        return validationLayer.compare( layerProperties.layerName ) == 0;
      } );

      if( it == std::cend( instanceLayers ) )
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

    std::for_each( std::cbegin( VulkanValidationExtensions ),
                   std::cend( VulkanValidationExtensions ),
                   [ &instanceExtensions ]( std::string_view validationExtension )
    {
      auto it = std::find_if( std::cbegin( instanceExtensions ),
                              std::cend( instanceExtensions ),
                              [ &validationExtension ]( const VkExtensionProperties &extensionProperties )
      {
        return validationExtension.compare( extensionProperties.extensionName ) == 0;
      } );

      if( it == std::cend( instanceExtensions ) )
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
    vkDestroyDevice( vkLogicalDevice_, nullptr );
    destroyDebugUtilsMessengerEXT( vkInstance_, vkDebugMessenger_, nullptr );
    vkDestroyInstance( vkInstance_, nullptr );
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
  GLFWwindow *window_;
  VkInstance vkInstance_;
  VkDebugUtilsMessengerEXT vkDebugMessenger_;
  VkPhysicalDevice vkPhysicalDevice_ = VK_NULL_HANDLE;
  VkDevice vkLogicalDevice_;
  VkQueue vkGraphicsQueue_;

private:
  inline static const std::vector< const char * > VulkanValidationLayers
  {
    "VK_LAYER_KHRONOS_validation",
    "VK_LAYER_LUNARG_standard_validation"
  };

  inline static const std::vector< const char * > VulkanValidationExtensions
  {
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME
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