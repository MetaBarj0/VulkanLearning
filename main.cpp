#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>
#include <functional>
#include <cstdlib>
#include <vector>
#include <algorithm>

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
    createInstance();
    displayAvailableExtensions();
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

    VkInstanceCreateInfo instanceInfo
    {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &appInfo
    };

    uint32_t glfwExtensionCount = 0u;
    auto glfwExtensions = glfwGetRequiredInstanceExtensions( &glfwExtensionCount );

    if( glfwExtensions == nullptr )
      throw std::runtime_error{ "Error while querying glfw required vulkan extensions" };

    instanceInfo.enabledExtensionCount = glfwExtensionCount;
    instanceInfo.ppEnabledExtensionNames = glfwExtensions;

    instanceInfo.enabledLayerCount = 0u;

    if( vkCreateInstance( &instanceInfo, nullptr, &vkInstance_ ) != VK_SUCCESS )
      throw std::runtime_error{ "Error while creating vulkan instance" };
  }

  void displayAvailableExtensions()
  {
    std::uint32_t availableExtensionCount = 0u;

    if( vkEnumerateInstanceExtensionProperties( nullptr, &availableExtensionCount, nullptr ) != VK_SUCCESS )
      throw std::runtime_error{ "Error while counting vulkan instance available extensions" };

    std::vector<VkExtensionProperties> instanceExtensions{ availableExtensionCount };

    if( vkEnumerateInstanceExtensionProperties( nullptr, &availableExtensionCount, instanceExtensions.data() ) != VK_SUCCESS )
      throw std::runtime_error{ "Error while querying vulkan instance available extensions" };

    std::for_each( std::cbegin( instanceExtensions ), std::cend( instanceExtensions ), []( auto &&extension ) { std::cout << extension.extensionName << '\n'; } );
  }

  void mainLoop()
  {
    while( glfwWindowShouldClose( window_ ) != GLFW_TRUE )
      glfwPollEvents();
  }

  void cleanup()
  {
    vkDestroyInstance( vkInstance_, nullptr );
    glfwDestroyWindow( window_ );
    glfwTerminate();
  }

private:
  GLFWwindow *window_;
  VkInstance vkInstance_;
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