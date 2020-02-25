#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>
#include <functional>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <string_view>

static constexpr int WINDOW_WIDTH = 800;
static constexpr int WINDOW_HEIGHT = 600;

#ifndef NDEBUG
#define VULKAN_VALIDATION_ENABLED
#endif // !NDEBUG

#ifdef VULKAN_VALIDATION_ENABLED
static const std::vector< const char * > VULKAN_VALIDATION_LAYERS
{
  "VK_LAYER_KHRONOS_validation",
  "VK_LAYER_LUNARG_standard_validation"
};

static const std::vector< const char * > VULKAN_VALIDATION_EXTENSIONS
{
  VK_EXT_DEBUG_UTILS_EXTENSION_NAME
};

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback( VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                     VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                     const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                                     void *pUserData )
{
  std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

  return VK_FALSE;
}

static VkResult CreateDebugUtilsMessengerEXT( VkInstance instance,
                                              const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
                                              const VkAllocationCallbacks *pAllocator,
                                              VkDebugUtilsMessengerEXT *pDebugMessenger )
{
  auto func = reinterpret_cast< PFN_vkCreateDebugUtilsMessengerEXT >( vkGetInstanceProcAddr( instance, "vkCreateDebugUtilsMessengerEXT" ) );

  if( func != nullptr )
    return func( instance, pCreateInfo, pAllocator, pDebugMessenger );

  return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void DestroyDebugUtilsMessengerEXT( VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks *pAllocator )
{
  auto func = reinterpret_cast< PFN_vkDestroyDebugUtilsMessengerEXT >( vkGetInstanceProcAddr( instance, "vkDestroyDebugUtilsMessengerEXT" ) );

  if( func != nullptr )
    func( instance, debugMessenger, pAllocator );
}
#endif // VULKAN_VALIDATION_ENABLED

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
  }

  void setupDebugMessenger()
  {
#ifdef VULKAN_VALIDATION_ENABLED
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

    if( CreateDebugUtilsMessengerEXT( vkInstance_, &createInfos, nullptr, &vkDebugMessenger_ ) != VK_SUCCESS )
      throw std::runtime_error{ "Error while setup the debug messenger" };
#endif // VULKAN_VALIDATION_ENABLED
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
#ifdef VULKAN_VALIDATION_ENABLED
    std::for_each( std::cbegin( VULKAN_VALIDATION_EXTENSIONS ),
                   std::cend( VULKAN_VALIDATION_EXTENSIONS ),
                   [ &extensions ]( const char *const &extensionName ) mutable { extensions.push_back( extensionName ); } );
#endif // VULKAN_VALIDATION_ENABLED
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

#ifdef VULKAN_VALIDATION_ENABLED
    std::for_each( std::cbegin( VULKAN_VALIDATION_LAYERS ),
                   std::cend( VULKAN_VALIDATION_LAYERS ),
                   [ &enabledLayers ]( const char *const &layerName ) mutable { enabledLayers.push_back( layerName ); } );
#endif // VULKAN_VALIDATION_ENABLED

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
#ifdef VULKAN_VALIDATION_ENABLED
    std::uint32_t availableLayerCount = 0u;

    if( vkEnumerateInstanceLayerProperties( &availableLayerCount, nullptr ) != VK_SUCCESS )
      throw std::runtime_error{ "Error while counting vulkan instance available layers" };

    std::vector<VkLayerProperties> instanceLayers{ availableLayerCount };

    if( vkEnumerateInstanceLayerProperties( &availableLayerCount, instanceLayers.data() ) != VK_SUCCESS )
      throw std::runtime_error{ "Error while querying vulkan instance available layers" };

    std::for_each( std::cbegin( VULKAN_VALIDATION_LAYERS ),
                   std::cend( VULKAN_VALIDATION_LAYERS ),
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
#endif // VULKAN_VALIDATION_ENABLED
  }

  void checkSupportedValidationExtensions()
  {
#ifdef VULKAN_VALIDATION_ENABLED
    std::uint32_t availableExtensionCount = 0u;

    if( vkEnumerateInstanceExtensionProperties( nullptr, &availableExtensionCount, nullptr ) != VK_SUCCESS )
      throw std::runtime_error{ "Error while counting vulkan instance available extensions" };

    std::vector<VkExtensionProperties> instanceExtensions{ availableExtensionCount };

    if( vkEnumerateInstanceExtensionProperties( nullptr, &availableExtensionCount, instanceExtensions.data() ) != VK_SUCCESS )
      throw std::runtime_error{ "Error while querying vulkan instance available extensions" };

    std::for_each( std::cbegin( VULKAN_VALIDATION_EXTENSIONS ),
                   std::cend( VULKAN_VALIDATION_EXTENSIONS ),
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
#endif // VULKAN_VALIDATION_ENABLED
  }

  void mainLoop()
  {
    while( glfwWindowShouldClose( window_ ) != GLFW_TRUE )
      glfwPollEvents();
  }

  void cleanup()
  {
#ifdef VULKAN_VALIDATION_ENABLED
    DestroyDebugUtilsMessengerEXT( vkInstance_, vkDebugMessenger_, nullptr );
#endif // VULKAN_VALIDATION_ENABLED
    vkDestroyInstance( vkInstance_, nullptr );
    glfwDestroyWindow( window_ );
    glfwTerminate();
  }

private:
  GLFWwindow *window_;
  VkInstance vkInstance_;
  VkDebugUtilsMessengerEXT vkDebugMessenger_;
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