#define GLFW_INCLUDE_VULKAN //Turn on glfw vulkan support
#include <GLFW\glfw3.h> //glfw header

#include <iostream> 
#include <vector>
#include <stdexcept>
#include <functional>
#include <set>
#include <algorithm>
#include <fstream>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE //use normalized coordinates for depth
#include <glm/glm.hpp> //linear algebra library
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION //include stb function definitions
#include <stb_image.h>

const int WIDTH = 800; //window initial width
const int HEIGHT = 600; //window initial height

VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugReportCallbackEXT *pCallback) {
	//wrapper function to call vkCreateDebugReportCallbackEXT since it's not loaded by default
	auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"); //get a pointer to the function

	if (func != nullptr) { //check that we succeded
		return func(instance, pCreateInfo, pAllocator, pCallback); //call the function and pass through the parameters
	} else {
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}
}

void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks *pAllocator) {
	//wrapper function to call vkDestroyDebugReportCallbackEXT since it's not loaded by default
	auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"); //get a pointer to the function

	if (func != nullptr) { //check that we succeded
		func(instance, callback, pAllocator); //call the function and pass through the parameters
	}
}

static std::vector<char> readFile(const std::string &filename) {
	//load a file into memory as binary data - open at the end
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) //if we failed
		throw std::runtime_error("Failed to open file " + filename);

	size_t fileSize = (size_t)file.tellg(); //get our current location in the file - at the end so this is our filesize
	std::vector<char> buffer(fileSize); //buffer for the file

	file.seekg(0); //go to the beggining
	file.read(buffer.data(), fileSize); //read the entire file into the buffer

	file.close(); //close the file
	
#ifndef NDEBUG
	std::cout << "File size of " << fileSize << std::endl; //debug check file size
#endif // !NDEBUG

	

	return buffer;
}

const std::vector<const char*> validationLayers = {

		"VK_LAYER_LUNARG_standard_validation" //use the standard lunarG validation layers

};

const std::vector<const char *> deviceExtentions = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME //enable SwapChain extentions
};

#ifndef NDEBUG
const bool enableValidationLayers = true; //enable validation layers iff we're in debug mode
#else
const bool enableValidationLayers = false;
#endif // !NDEBUG


class TriangleBasicsApp {

public:
	void run() {
		initWindow(); //call glfw setup functions to open window
		initVulkan(); //something like 900 lines of data entry, a function call every so often to mix things up
		mainLoop(); //program only does 4 things right now, occasionally blow stuff up to reset it
		cleanup(); //blow everything up in the right order
	}

private:

	struct UniformBufferObject { //shader dlobal object
		glm::mat4 model; //model matrix
		glm::mat4 view; //view matrix
		glm::mat4 proj; //projection matrix
	};

	struct Vertex { //shader vertex information
		glm::vec3 pos;  //position vetor x, y, z for now
		glm::vec3 color; //color vector, RBG, alpha hardcoded to 1 in shader for now
		glm::vec2 tex;
		//data is interleaved in memory i.e <[pos][color][tex]><[pos][color][tex]>...
		//                                  ^-----stride-----^

		static VkVertexInputBindingDescription getBindingDescription() { //generate struct describing the binding properties
			VkVertexInputBindingDescription bindingDes = {};
			bindingDes.binding = 0; //binding shader will look for the data buffer at
			bindingDes.stride = sizeof(Vertex); //current size of the struct
			bindingDes.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; //advance data entry every vertex, rather than every instance

			return bindingDes; //return the struct
		}

		static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() { //generate an array of structs describing our vertex struct
			std::array<VkVertexInputAttributeDescription, 3> attDes = {};

			attDes[0].binding = 0; //binding, must match appropriate VkVertexInputBindingDescription
			attDes[0].location = 0; //location specified in shader for i-th data member - 0:0
			attDes[0].format = VK_FORMAT_R32G32B32_SFLOAT; //specify data vector size using color flags - two 32 bit signed floats
			attDes[0].offset = offsetof(Vertex, pos); //offset to find pos elements <^[pos][color][tex]><^[pos][color][tex]>...

			attDes[1].binding = 0; //binding, must match appropriate VkVertexInputBindingDescription
			attDes[1].location = 1; //location specified in shader for i-th data member - 0:1
			attDes[1].format = VK_FORMAT_R32G32B32_SFLOAT; //specify data vector size using color flags - three 32 bit signed floats
			attDes[1].offset = offsetof(Vertex, color); //offset to find color elements <[pos]^[color][tex]><[pos]^[color][tex]>...

			attDes[2].binding = 0; //binding, must match appropriate VkVertexInputBindingDescription
			attDes[2].location = 2; //location specified in shader for i-th data member - 0:2 
			attDes[2].format = VK_FORMAT_R32G32_SFLOAT; //specify data vector size using color flags - two 32 bit signed floats
			attDes[2].offset = offsetof(Vertex, tex); //offset to find color elements <[pos][color]^[tex]><[pos][color]^[tex]>...

			return attDes; //return the struct
		}
	}; 

	struct QueueFamilyIndices { //struct to hold current device indexes for queue families being used
		int graphicsFamily = -1; //graphics family index - draw related operations - implies memory transfer operations support
		int presentFamily = -1;  //present family index - operations related to presenting images to swapchain/framebuffers - ideally the same as the graphics family

		bool isComplete() { //return true if all needed queues have been found
			return graphicsFamily >= 0 && presentFamily >= 0;
		}

	};

	struct SwapChainSupportDetails { //struct to hold the current SwapChain support details of our physical device
		VkSurfaceCapabilitiesKHR capabilities; //capability list struct
		std::vector<VkSurfaceFormatKHR> formats; //supported formats
		std::vector<VkPresentModeKHR> presentModes; //supported present modes
	};

	GLFWwindow *window; //created by glfw with vulkan instead of OpenGL - platform agnostic code

	VkInstance instance; //Vulkan instance - explicitly created - destroy last
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE; //physical device to use - retrieved from instance - implicitly destroyed shares lifecycle with instance

	VkDevice device; //logical device created from the physical device - explicitly created from physical device - destroy only after everything created from it

	VkQueue graphicsQueue; //the graphics queue - explicitly created from the device - destroy before device
	VkQueue presentQueue; //the present queue - explicitly created from the device - destroy before device

	VkDebugReportCallbackEXT callback; //debug message callback function to output to console - explicitly created from instance - destroy before instance

	VkSurfaceKHR surface; //surface connecting the instance to the window using glfw helper functions for platform agnosticism - explicitly created from instance and window - destroy before instance

	VkFormat swapChainImageFormat; //the chosen image format for the SwapChain described as a pixel format and a color space - member of the SwapChain - no cleanup required
	VkExtent2D swapChainExtent; //extent of the SwapChain in height and width will either be the window actual or the max allowed by the surface - member of the SwapChain - no cleanup required
	VkSwapchainKHR swapChain; //object to hold series of images to present to the surface - created explicitly from the device - destroy before device
	std::vector<VkImage> swapChainImages; //vector of images held in the swapchain - created during swapchain creation - no cleanup required

	std::vector<VkImageView> swapChainImageViews; //vector of views related to swapchain images - created from SwapChain images - delete before the swapchain

	VkRenderPass renderPass;
	VkDescriptorSetLayout desSetLayout;
	VkPipelineLayout pipelineLayout;
	VkPipeline graphicsPipeline;

	std::vector<VkFramebuffer> swapChainFramebuffers;

	VkBuffer vertexBuffer;
	VkDeviceMemory vertexBufferMemory;

	VkBuffer indexBuffer;
	VkDeviceMemory indexBufferMemory;

	VkBuffer uniformBuffer;
	VkDeviceMemory uniformBufferMemory;

	VkImage texImage; //image object to hold texture texels - explicitly created on the device - destroy before the device
	VkDeviceMemory texImageMem; //device memory to hold our image object - explicitly created on the device - free after the destruction of the related buffer
	VkImageView texImgView; //image view for our texture - created from texture image - delete before the image

	VkImage depthImage; //image object to hold depth attachment image one needed per running draw op- explicitly created on the device - destroy before the device
	VkDeviceMemory depthImageMem; //device memory to hold our depth image object - explicitly created on the device - free after the destruction of the related buffer
	VkImageView depthImgView; //image view for our depth - created from texture image - delete before the image

	VkSampler texSampler; //sampler to take our texel data and turn it into proper fragment data - explicitly created on the device - destroy before the device

	VkCommandPool commandPool;
	VkDescriptorPool desPool;
	VkDescriptorSet desSet;

	std::vector<VkCommandBuffer> commandBuffers;

	VkSemaphore imageAvailableSemaphore;
	VkSemaphore renderFinishedSemaphore;

	QueueFamilyIndices indicies;

	const std::vector<Vertex> vertices = {
		{ { -0.5f, -0.5f, 0.0f },{ 1.0f, 0.0f, 0.0f },{ 1.0f, 0.0f } },
		{ { 0.5f, -0.5f, 0.0f },{ 0.0f, 1.0f, 0.0f },{ 0.0f, 0.0f } },
		{ { 0.5f, 0.5f, 0.0f },{ 0.0f, 0.0f, 1.0f },{ 0.0f, 1.0f } },
		{ { -0.5f, 0.5f, 0.0f },{ 1.0f, 1.0f, 1.0f },{ 1.0f, 1.0f } },
		
		{ { -0.5f, -0.5f, -0.5f },{ 1.0f, 0.0f, 0.0f },{ 1.0f, 0.0f } },
		{ { 0.5f, -0.5f, -0.5f },{ 0.0f, 1.0f, 0.0f },{ 0.0f, 0.0f } },
		{ { 0.5f, 0.5f, -0.5f },{ 0.0f, 0.0f, 1.0f },{ 0.0f, 1.0f } },
		{ { -0.5f, 0.5f, -0.5f },{ 1.0f, 1.0f, 1.0f },{ 1.0f, 1.0f } }
	};

	const std::vector<uint16_t> vIndicies = {
		0, 1, 2, 2, 3, 0,
		4, 5, 6, 6, 7, 4
	};


	void initWindow() {

		if (!glfwInit())
			throw std::runtime_error("Failed to init GLFW");


		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);

		glfwSetWindowUserPointer(window, this);
		glfwSetWindowSizeCallback(window, TriangleBasicsApp::onWindowResize);

	}

	static void onWindowResize(GLFWwindow *window, int width, int height) {
		if (width == 0 || height == 0) return;
		TriangleBasicsApp *app = reinterpret_cast<TriangleBasicsApp *>(glfwGetWindowUserPointer(window));
		app->recreateSwapchain();


	}


	void initVulkan() {
		createInstance();

		setupCallback();

		createSurface();

		pickPhysicalDevice();
		createLogicalDevice();
		createSwapChain();
		createImageViews();
		createRenderPass();
		createDescriptorSetLayout();
		createGraphicsPipeline();
		createFrameBuffer();

		createCommandPool();

		createDepthResources();

		createTextureImage(); //load texture image into device memory
		createTextureImageView(); //create a view for our texture
		createTexureSampler();

		createVertexBuffer();
		
		createIndexBuffer();
		
		createUniformBuffer();
		createDescriptorPool();
		createDescriptorSet();

		createCommandBuffers();

		createSemaphores();
	}

	void createInstance() {

		if (enableValidationLayers && !checkValidationLayerSupport())
			throw std::runtime_error("Validation layers requested but not found");

		VkApplicationInfo appInfo = {};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Triangle Basics";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "No Engine";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_0;

		uint32_t extentionCount = 0;

		vkEnumerateInstanceExtensionProperties(nullptr, &extentionCount, nullptr);


		std::vector<VkExtensionProperties> extentions(extentionCount);

		vkEnumerateInstanceExtensionProperties(nullptr, &extentionCount, extentions.data());

		
#ifndef NDEBUG
		std::cout << "Avalible extentions" << std::endl;
		for (const auto &extention : extentions)
			std::cout << "\t" << extention.extensionName << std::endl;
#endif // !NDEBUG

		


		VkInstanceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

		auto extention = getRequiredExtentions();

		createInfo.enabledExtensionCount = static_cast<uint32_t>(extention.size());
		createInfo.ppEnabledExtensionNames = extention.data();

		if (enableValidationLayers) {
			createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
			createInfo.ppEnabledLayerNames = validationLayers.data();
		} else {
			createInfo.enabledLayerCount = 0;
			createInfo.ppEnabledLayerNames = nullptr;
		}

		if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
			throw std::runtime_error("Failed to create instance!");

	}

	bool checkValidationLayerSupport() {

		uint32_t layerCount = 0;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
		std::vector<VkLayerProperties> availibleLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availibleLayers.data());

		for (const char *layerName : validationLayers) {
			bool found = false;

			for (const auto &layerProperties : availibleLayers) {
				if (strcmp(layerName, layerProperties.layerName) == 0) {
					found = true;
					break;
				}
			}

			if (!found)
				return false;
		}

		return true;

	}

	std::vector<const char*> getRequiredExtentions() {
		uint32_t glfwExtentionCount = 0;
		const char **glfwExtentions;

		glfwExtentions = glfwGetRequiredInstanceExtensions(&glfwExtentionCount);

		std::vector<const char*> extentions(glfwExtentions, glfwExtentions + glfwExtentionCount);

		if (enableValidationLayers)
			extentions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);


		return extentions;
	}


	void setupCallback() {
		if (!enableValidationLayers) return;

		VkDebugReportCallbackCreateInfoEXT createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
		createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
		createInfo.pfnCallback = debugCallback;

		if (CreateDebugReportCallbackEXT(instance, &createInfo, nullptr, &callback) != VK_SUCCESS)
			throw std::runtime_error("Failed to set up debug callback");
	}

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugReportFlagsEXT flags,
		VkDebugReportObjectTypeEXT objType,
		uint64_t obj,
		size_t location,
		int32_t code,
		const char *layerPrefix,
		const char *msg,
		void *userData) {

		std::cerr << "Validation layer: " << msg << std::endl;

		return VK_FALSE;
	}


	void createSurface() {
		if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
			throw std::runtime_error("ERROR: Surface creation failed");
	}


	void pickPhysicalDevice() {

		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

		if (deviceCount == 0)
			throw std::runtime_error("Failed to find GPU that supports Vulkan");

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

		for (const auto &device : devices) {
			if (isDeviceSuitable(device)) {
				physicalDevice = device;
				break;
			}
		}

		if (physicalDevice == VK_NULL_HANDLE)
			throw std::runtime_error("Failed to find a suitable GPU");

	}

	bool isDeviceSuitable(VkPhysicalDevice device) {
		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(device, &deviceProperties);

		VkPhysicalDeviceFeatures deviceFeatures;
		vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

		if (!(deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))
			return false;


		bool extentionsSupport = checkDeviceExtentionSupport(device);

		indicies = findQueueFamilies(device);

		bool swapChainSufficient = false;
		if (extentionsSupport) {
			SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
			swapChainSufficient = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
		}

		return indicies.isComplete() && swapChainSufficient && deviceFeatures.samplerAnisotropy;
	}

	bool checkDeviceExtentionSupport(VkPhysicalDevice device) {
		uint32_t extentionCount;
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extentionCount, nullptr);

		std::vector<VkExtensionProperties> avalibleExtentions(extentionCount);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extentionCount, avalibleExtentions.data());

		std::set<std::string> reqExtentions(deviceExtentions.begin(), deviceExtentions.end());

		for (const auto &extention : avalibleExtentions)
			reqExtentions.erase(extention.extensionName);

		return reqExtentions.empty();
	}

	QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
		QueueFamilyIndices indicies;

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

		int i = 0;
		for (const auto &queueFamily : queueFamilies) {
			if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
				indicies.graphicsFamily = i;

			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

			if (queueFamily.queueCount > 0 && presentSupport)
				indicies.presentFamily = i;

			if (indicies.isComplete())
				break;

			i++;
		}

		return indicies;
	}


	void createLogicalDevice() {

		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		std::set<int> uniqueFamilies = { indicies.graphicsFamily, indicies.presentFamily };

		float queuePriority = 1.0f;
		for (int queueFamily : uniqueFamilies) {
			VkDeviceQueueCreateInfo queueCreateInfo = {};
			queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfo.queueFamilyIndex = queueFamily;
			queueCreateInfo.queueCount = 1;
			queueCreateInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(queueCreateInfo);
		}




		VkPhysicalDeviceFeatures deviceFeatures = {};
		deviceFeatures.samplerAnisotropy = VK_TRUE;

		VkDeviceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

		createInfo.pQueueCreateInfos = queueCreateInfos.data();
		createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());

		createInfo.pEnabledFeatures = &deviceFeatures;

		createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtentions.size());
		createInfo.ppEnabledExtensionNames = deviceExtentions.data();

		if (enableValidationLayers) {
			createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
			createInfo.ppEnabledLayerNames = validationLayers.data();
		} else {
			createInfo.enabledLayerCount = 0;
		}

		if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS)
			throw std::runtime_error("Failed to create logical device");

		vkGetDeviceQueue(device, indicies.graphicsFamily, 0, &graphicsQueue);
		vkGetDeviceQueue(device, indicies.presentFamily, 0, &presentQueue);
	}


	void createSwapChain() {
		SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

		VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
		VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
		VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

		uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;

		if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount)
			imageCount = swapChainSupport.capabilities.maxImageCount;

		VkSwapchainCreateInfoKHR createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		uint32_t queueFamilyIndicies[] = { (uint32_t)indicies.graphicsFamily, (uint32_t)indicies.presentFamily };

		if (indicies.graphicsFamily != indicies.presentFamily) {
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndicies;
		} else {
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			createInfo.queueFamilyIndexCount = 0;
			createInfo.pQueueFamilyIndices = nullptr;
		}

		createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS)
			throw std::runtime_error("Failed to create swapchain.");

		vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
		swapChainImages.resize(imageCount);
		vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

		swapChainImageFormat = surfaceFormat.format;
		swapChainExtent = extent;
	}

	SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) {
		SwapChainSupportDetails details;

		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

		uint32_t formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

		if (formatCount != 0) {
			details.formats.resize(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
		}

		uint32_t presentModeCount;
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

		if (presentModeCount != 0) {
			details.presentModes.resize(presentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
		}

		return details;
	}

	VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats) {
		if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED)
			return{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };

		for (const auto &availableFormat : availableFormats)
			if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
				return availableFormat;

		return availableFormats[0];
	}

	VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes) {
		VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;

		for (const auto &availablePresentMode : availablePresentModes) {
			if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
				return availablePresentMode;
			} else if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
				bestMode = availablePresentMode;
			}
		}
		return bestMode;
	}

	VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) {
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
			return capabilities.currentExtent;
		} else {
			int width, height;
			glfwGetWindowSize(window, &width, &height);

			VkExtent2D actualExtent = { width, height };

			actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
			actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

			return actualExtent;
		}

	}


	void createImageViews() {
		swapChainImageViews.resize(swapChainImages.size());

		for (size_t i = 0; i < swapChainImages.size(); i++) {
			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = 1;
			subresourceRange.baseArrayLayer = 0;
			subresourceRange.layerCount = 1;

			ovgfCreateImageView(swapChainImages[i], VK_IMAGE_VIEW_TYPE_2D, swapChainImageFormat, { VK_COMPONENT_SWIZZLE_IDENTITY }, subresourceRange, &swapChainImageViews[i]);

		}
	}

	void createTextureImageView() {
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.baseArrayLayer = 0;
		subresourceRange.layerCount = 1;

		ovgfCreateImageView(texImage, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, { VK_COMPONENT_SWIZZLE_IDENTITY }, subresourceRange, &texImgView);
	}

	void ovgfCreateImageView(VkImage image, VkImageViewType viewType, VkFormat format, VkComponentMapping componentStettings, VkImageSubresourceRange range, VkImageView *view) {
		VkImageViewCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createInfo.image = image;
		createInfo.viewType = viewType;
		createInfo.format = format;
		createInfo.components.r = componentStettings.r;
		createInfo.components.b = componentStettings.b;
		createInfo.components.g = componentStettings.g;
		createInfo.components.a = componentStettings.a;
		createInfo.subresourceRange.aspectMask = range.aspectMask;
		createInfo.subresourceRange.baseMipLevel = range.baseMipLevel;
		createInfo.subresourceRange.levelCount = range.levelCount;
		createInfo.subresourceRange.baseArrayLayer = range.baseArrayLayer;
		createInfo.subresourceRange.layerCount = range.layerCount;

		if (vkCreateImageView(device, &createInfo, nullptr, view) != VK_SUCCESS)
			throw std::runtime_error("Failed to create image views");
	}
	
	void createTexureSampler() {
		VkSamplerCreateInfo samplerInfo = {};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;

		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

		samplerInfo.anisotropyEnable = VK_TRUE;
		samplerInfo.maxAnisotropy = 16;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;

		if (vkCreateSampler(device, &samplerInfo, nullptr, &texSampler) != VK_SUCCESS)
			throw std::runtime_error("Failed to create image views");
	}


	void createRenderPass() {
		VkAttachmentDescription colorAttachment = {};
		colorAttachment.format = swapChainImageFormat;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorAttachmentRef = {};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;

		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;

		VkSubpassDependency dependency = {};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;

		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;

		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS)
			throw std::runtime_error("Failed to create render pass!");



	}

	void createDescriptorSetLayout() {

		VkDescriptorSetLayoutBinding uboLB = {};
		uboLB.binding = 0;
		uboLB.descriptorCount = 1;
		uboLB.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboLB.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		uboLB.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutBinding samplerLB = {};
		samplerLB.binding = 1;
		samplerLB.descriptorCount = 1;
		samplerLB.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerLB.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		samplerLB.pImmutableSamplers = nullptr;

		std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLB, samplerLB };

		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &desSetLayout) != VK_SUCCESS)
			throw std::runtime_error("Failed to create descriptor set layouts.");


	}


	void createGraphicsPipeline() {
		auto vertShaderCode = readFile("shaders/vert.spv");
		auto fragShaderCode = readFile("shaders/frag.spv");

		VkShaderModule vertShaderModule;
		VkShaderModule fragShaderModule;

		vertShaderModule = createShaderModule(vertShaderCode);
		fragShaderModule = createShaderModule(fragShaderCode);

		VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
		vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = vertShaderModule;
		vertShaderStageInfo.pName = "main";

		VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
		fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = fragShaderModule;
		fragShaderStageInfo.pName = "main";

		VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

		VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		auto bindDes = Vertex::getBindingDescription();
		auto attDes = Vertex::getAttributeDescriptions();

		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &bindDes;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attDes.size());
		vertexInputInfo.pVertexAttributeDescriptions = attDes.data();

		VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;

		viewport.width = (float)swapChainExtent.width;
		viewport.height = (float)swapChainExtent.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor = {};
		scissor.offset = { 0, 0 };
		scissor.extent = swapChainExtent;

		VkPipelineViewportStateCreateInfo viewportState = {};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.scissorCount = 1;
		viewportState.pScissors = &scissor;
		viewportState.viewportCount = 1;
		viewportState.pViewports = &viewport;

		VkPipelineRasterizationStateCreateInfo rasterizer = {};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.0f;
		rasterizer.depthBiasClamp = 0.0f;
		rasterizer.depthBiasSlopeFactor = 0.0f;

		VkPipelineMultisampleStateCreateInfo multisampling = {};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampling.minSampleShading = 1.0f;
		multisampling.pSampleMask = nullptr;
		multisampling.alphaToCoverageEnable = VK_FALSE;
		multisampling.alphaToOneEnable = VK_FALSE;

		VkPipelineColorBlendAttachmentState cBAS = {};
		cBAS.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		cBAS.blendEnable = VK_FALSE;
		cBAS.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
		cBAS.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
		cBAS.colorBlendOp = VK_BLEND_OP_ADD; // Optional
		cBAS.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
		cBAS.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
		cBAS.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

		VkPipelineColorBlendStateCreateInfo colorBlend = {};
		colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlend.logicOpEnable = VK_FALSE;
		colorBlend.logicOp = VK_LOGIC_OP_COPY;
		colorBlend.attachmentCount = 1;
		colorBlend.pAttachments = &cBAS;
		colorBlend.blendConstants[0] = 0.0f; // Optional
		colorBlend.blendConstants[1] = 0.0f; // Optional
		colorBlend.blendConstants[2] = 0.0f; // Optional
		colorBlend.blendConstants[3] = 0.0f; // Optional

		/* Dynamic State Config

		VkDynamicState dynamicStates[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_LINE_WIDTH
		};

		VkPipelineDynamicStateCreateInfo dynamicState = {};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = 2;
		dynamicState.pDynamicStates = dynamicStates;*/

		VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &desSetLayout;
		pipelineLayoutInfo.pushConstantRangeCount = 0;
		pipelineLayoutInfo.pPushConstantRanges = 0;

		if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
				throw std::runtime_error("Failed to create pipeline layout.");

		VkGraphicsPipelineCreateInfo pipelineInfo = {};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = nullptr;
		pipelineInfo.pColorBlendState = &colorBlend;
		pipelineInfo.pDynamicState = nullptr;
		pipelineInfo.layout = pipelineLayout;
		pipelineInfo.renderPass = renderPass;
		pipelineInfo.subpass = 0;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
		pipelineInfo.basePipelineIndex = -1; // Optional

		if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS)
			throw std::runtime_error("Failed to create graphics pipeline.");


		vkDestroyShaderModule(device, fragShaderModule, nullptr);
		vkDestroyShaderModule(device, vertShaderModule, nullptr);
	}

	VkShaderModule createShaderModule(const std::vector<char> &code) {
		VkShaderModuleCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

		VkShaderModule shaderModule;
		if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
			throw std::runtime_error("Failed to create shader module");
		return shaderModule;
	}


	void createFrameBuffer() {
		swapChainFramebuffers.resize(swapChainImageViews.size());

		for (size_t i = 0; i < swapChainImageViews.size(); i++) {
			VkImageView attachments[] = {
				swapChainImageViews[i]
			};

			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = renderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = swapChainExtent.width;
			framebufferInfo.height = swapChainExtent.height;
			framebufferInfo.layers = 1;

			if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS)
				throw std::runtime_error("Failed to create framebuffer " + i);
		}

	}


	void createCommandPool() {
		VkCommandPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = indicies.graphicsFamily;
		poolInfo.flags = 0;

		if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
			throw std::runtime_error("Failed to create command pool.");
	}


	void createDepthResources() {
		VkFormat depthFormat = findSupportedFormat(
			{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT }, 
			VK_IMAGE_TILING_OPTIMAL, 
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

		createImage(swapChainExtent.width, swapChainExtent.height, 
			depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			depthImage, depthImageMem);

		ovgfCreateimageView(depthImage, )
	}

	bool hasStencilComponent(VkFormat format) {
		return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
	}

	VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
		for (VkFormat format : candidates) {
			VkFormatProperties props;
			vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

			if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features){
				return format;
			} else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
				return format;
			}

			throw std::runtime_error("Failed to find supported format!");
		}
	}

	void createTextureImage() {
		int texWidth, texHeight, texChannels; //vars to hold image data
		stbi_uc *pixels = stbi_load("textures/texture.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha); //load the pixel data and force alpha channel even if missing

		VkDeviceSize imageSize = texWidth * texHeight * 4; //pixel count * number of bytes

		if (!pixels) //we have no image if this goes off
			throw std::runtime_error("Failed to load texture image");

		VkBuffer stageBuff; //staging buffer for the pixels
		VkDeviceMemory stageBuffMem; //buffer device memory

		//create buffer on the divice with a transfer source memory layout, the host visible and coherent flags, and the buffer/memory to fill
		createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stageBuff, stageBuffMem);

		void *data; //void pointer for the trasfer
		vkMapMemory(device, stageBuffMem, 0, imageSize, 0, &data); //map the buffer memory to our void pointer
		memcpy(data, pixels, static_cast<size_t>(imageSize)); //copy the pixel data to the device
		vkUnmapMemory(device, stageBuffMem); //unmap the memory

		stbi_image_free(pixels); //free the host image memory

		createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texImage, texImageMem);

		trasitionImageLayout(texImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL); //transition from undef to transfer dest optimal

		copyBufferToImage(stageBuff, texImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight)); //preform the copy

		trasitionImageLayout(texImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); //change from tranfer layout to shader read layout

		vkDestroyBuffer(device, stageBuff, nullptr); //destroy the staging buffer
		vkFreeMemory(device, stageBuffMem, nullptr); //free the buffers memory

	}

	void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
		VkImageCreateInfo imageInfo = {};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D; //texel coordinate system
		imageInfo.extent.width = width; //width of the image
		imageInfo.extent.height = height; //height of the image
		imageInfo.extent.depth = 1; //no depth on a 2d image but still has one "row"
		imageInfo.mipLevels = 1; //not an array of textures
		imageInfo.arrayLayers = 1; //only one image layer
		imageInfo.tiling = tiling; //using a staging buffer so we don't need texel access
		imageInfo.format = format; //texel format
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; //we don't need to preserve any initial texel data
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; //the image is going to recieve data from our staging buffer and we need access to the image from the shader
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; //the graphics queue implicitly supports memory transfers so only one queue will use this image
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; //No multisampling
		imageInfo.flags = 0; //No flags for now - optional

		if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) //crash if we don't create an image
			throw std::runtime_error("Failed to create image");

		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, image, &memReqs); //query our texture image for its memory requirements

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReqs.size; //set the ammount of memory to allocate to the ammount we need
		allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); //find memory with the properties we need, ensure it's device local

		if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) //crash if we dont get our memory
			throw std::runtime_error("Failed to allocate image memory");

		vkBindImageMemory(device, image, imageMemory, 0); //bind our image to the allocated memory
	}


	void createVertexBuffer() {
		VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

		createStagedBuffer(bufferSize, vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0, vertexBuffer, vertexBufferMemory);
	}

	void createIndexBuffer() {
		VkDeviceSize bufferSize = sizeof(vIndicies[0]) * vIndicies.size();

		createStagedBuffer(bufferSize, vIndicies, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 0, indexBuffer, indexBufferMemory);

	}

	template<typename T, typename A>
	void createStagedBuffer(VkDeviceSize bufferSize, std::vector<T, A> in, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &buffer, VkDeviceMemory &bufferMemory) {

		VkBuffer stagingBuffer;
		VkDeviceMemory stagingBufferMemory;
		createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

		void *data;
		vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
		memcpy(data, in.data(), (size_t)bufferSize);
		vkUnmapMemory(device, stagingBufferMemory);

		createBuffer(bufferSize, VK_IMAGE_USAGE_TRANSFER_DST_BIT | usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer, bufferMemory);

		copyBuffer(stagingBuffer, buffer, bufferSize);

		vkDestroyBuffer(device, stagingBuffer, nullptr);
		vkFreeMemory(device, stagingBufferMemory, nullptr);
	}

	void createUniformBuffer() {
		VkDeviceSize bufferSize = sizeof(UniformBufferObject);
		createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformBuffer, uniformBufferMemory);
	}

	void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &buffer, VkDeviceMemory &bufferMemory) {
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
			throw std::runtime_error("Failed to create vertex buffer.");

		VkMemoryRequirements memReq;
		vkGetBufferMemoryRequirements(device, buffer, &memReq);

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);

		if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS)
			throw std::runtime_error("Failed to allocate vertex buffer memory.");

		vkBindBufferMemory(device, buffer, bufferMemory, 0);
	}

	uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
		VkPhysicalDeviceMemoryProperties memProp;
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProp);

		for (uint32_t i = 0; i < memProp.memoryTypeCount; i++)
			if (typeFilter & (1 << i) && (memProp.memoryTypes[i].propertyFlags & properties) == properties)
				return i;

		throw std::runtime_error("Failed to find suitable memory type.");
	}

	void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
		VkCommandBuffer commandBuffer = beginSingleTimeCommands();

		VkBufferCopy copyRegion = {};
		copyRegion.srcOffset = 0; //Offset if data interlaced
		copyRegion.dstOffset = 0; //Offset of destinaion if interlaced
		copyRegion.size = size; //size of the memory region to copy
		vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion); //record the copy command

		endSingleTimeCommands(commandBuffer);
	}

	void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
		VkCommandBuffer commandBuffer = beginSingleTimeCommands();

		VkBufferImageCopy region = {};
		region.bufferOffset = 0; //start at the beggining of the buffer
		region.bufferRowLength = 0; //no row padding - tightly packed
		region.bufferImageHeight = 0; //no height padding - tightly packed

		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; //the aspect of the buffer to copy
		region.imageSubresource.mipLevel = 0; //no mipmapping
		region.imageSubresource.layerCount = 1; //only one shall pass

		region.imageOffset = { 0, 0, 0 }; //start indexes
		region.imageExtent = { width, height, 1 }; //end indexes

		vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region); //copy the region of the specified buffer to the specified image

		endSingleTimeCommands(commandBuffer);
	}

	VkCommandBuffer beginSingleTimeCommands() {
		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; //primary or secondary buffer - right now only using primary command buffers
		allocInfo.commandBufferCount = 1; //only one buffer at a time
		allocInfo.commandPool = commandPool; //the command pool to allocate from - only the one command pool for now

		VkCommandBuffer commandBuffer;
		vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer); //allocate the command buffer

		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; //signal that this buffer is one and done

		vkBeginCommandBuffer(commandBuffer, &beginInfo); //begin recording the buffer

		return commandBuffer;
	}

	void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
		vkEndCommandBuffer(commandBuffer); //stop recording the buffer

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1; //only one at a time for now
		submitInfo.pCommandBuffers = &commandBuffer; //the buffer

		vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE); //submit the buffer to the queue
		vkQueueWaitIdle(graphicsQueue); //wait for it to finish

		vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer); //free the buffer
	}

	void trasitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
		VkCommandBuffer commandBuffer = beginSingleTimeCommands();

		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = oldLayout; //specify the old layout
		barrier.newLayout = newLayout; //layout to trasfer to
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; //stayding in queue
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; //ibid
		barrier.image = image; //set the image who is having their layout transitioned
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; //the aspect of the image to transition
		barrier.subresourceRange.baseMipLevel = 0; //no mipmapping
		barrier.subresourceRange.levelCount = 1; //one level
		barrier.subresourceRange.baseArrayLayer = 0; //not using an array
		barrier.subresourceRange.layerCount = 1; //no stereoscopic images or anything

		VkPipelineStageFlags srcStage;
		VkPipelineStageFlags dstStage;
		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			barrier.srcAccessMask = 0; //Wait on nothing
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; //make the transfer stage wait on this barrier

			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; //wait on nothing
			dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT; //hold up the tranfer phase
		} else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; //Wait on the transfer phase
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; //make the fragment shader wait on this barrier

			srcStage = VK_ACCESS_TRANSFER_WRITE_BIT; //wait on the tranfer phase
			dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; //hold up the fragment shader
		} else {
			throw std::runtime_error("Currently unsupported layout transition");
		}


		vkCmdPipelineBarrier(commandBuffer,	srcStage, dstStage,	0, 0, nullptr, 0, nullptr,1, &barrier);

		endSingleTimeCommands(commandBuffer);
	}


	void createDescriptorPool() {
		std::array<VkDescriptorPoolSize, 2> poolSizes = {};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = 1;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = 1;

		VkDescriptorPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = 1;

		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &desPool) != VK_SUCCESS)
			throw std::runtime_error("Failed to create descriptor pool");

	}

	void createDescriptorSet() {
		VkDescriptorSetLayout layouts[] = { desSetLayout };
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = desPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = layouts;

		if (vkAllocateDescriptorSets(device, &allocInfo, &desSet) != VK_SUCCESS)
			throw std::runtime_error("Failed to allocate descriptor set");

		VkDescriptorBufferInfo bufferInfo = {};
		bufferInfo.buffer = uniformBuffer;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(UniformBufferObject);

		VkDescriptorImageInfo imageInfo = {};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = texImgView;
		imageInfo.sampler = texSampler;

		std::array<VkWriteDescriptorSet, 2> desWrites = {};
		desWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desWrites[0].dstSet = desSet;
		desWrites[0].dstBinding = 0;
		desWrites[0].dstArrayElement = 0;
		desWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		desWrites[0].descriptorCount = 1;
		desWrites[0].pBufferInfo = &bufferInfo;
		desWrites[0].pImageInfo = nullptr;
		desWrites[0].pTexelBufferView = nullptr;

		desWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desWrites[1].dstSet = desSet;
		desWrites[1].dstBinding = 1;
		desWrites[1].dstArrayElement = 0;
		desWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;;
		desWrites[1].descriptorCount = 1;
		desWrites[1].pBufferInfo = nullptr;
		desWrites[1].pImageInfo = &imageInfo;
		desWrites[1].pTexelBufferView = nullptr;

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(desWrites.size()), desWrites.data(), 0, nullptr);


	}

	void createCommandBuffers() {
		commandBuffers.resize(swapChainFramebuffers.size());

		VkCommandBufferAllocateInfo cmbAlloc = {};
		cmbAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmbAlloc.commandPool = commandPool;
		cmbAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmbAlloc.commandBufferCount = (uint32_t)commandBuffers.size();

		if (vkAllocateCommandBuffers(device, &cmbAlloc, commandBuffers.data()) != VK_SUCCESS)
			throw std::runtime_error("Failed to allocate command buffers.");

		for (size_t i = 0; i < commandBuffers.size(); i++) {
			VkCommandBufferBeginInfo beginInfo = {};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
			beginInfo.pInheritanceInfo = nullptr;

			vkBeginCommandBuffer(commandBuffers[i], &beginInfo);

			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = renderPass;
			renderPassInfo.framebuffer = swapChainFramebuffers[i];
			renderPassInfo.renderArea.extent = swapChainExtent;
			renderPassInfo.renderArea.offset = { 0, 0 };

			VkClearValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
			renderPassInfo.clearValueCount = 1;
			renderPassInfo.pClearValues = &clearColor;

			vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

			VkBuffer vertexBuffers[] = { vertexBuffer };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);

			vkCmdBindIndexBuffer(commandBuffers[i], indexBuffer, 0, VK_INDEX_TYPE_UINT16);

			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &desSet, 0, nullptr);

			vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(vIndicies.size()), 1, 0, 0, 0);

			vkCmdEndRenderPass(commandBuffers[i]);

			if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS)
				throw std::runtime_error("Failed to record command buffer " + i);

		}



	}


	void createSemaphores() {
		VkSemaphoreCreateInfo semaphoreInfo = {};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS || vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS)
			throw std::runtime_error("Failed to create semaphores.");
	}


	void mainLoop() {

		auto times = std::chrono::high_resolution_clock::now();
		uint32_t frames = 0;

		while (!glfwWindowShouldClose(window)) {

			glfwPollEvents();

			updateUniformBuffer();
			drawFrame();

			frames++;

			if (std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::high_resolution_clock::now() - times).count() >= 1) {
				std::cout << "Current FPS: " << frames << std::endl;
				frames = 0;
				times = std::chrono::high_resolution_clock::now();
			}
		}

		vkDeviceWaitIdle(device);
	}

	void drawFrame() {
		uint32_t imageIndex;
		VkResult res = vkAcquireNextImageKHR(device, swapChain, std::numeric_limits<uint32_t>::max(), imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

		if (res == VK_ERROR_OUT_OF_DATE_KHR) {
			recreateSwapchain();
			return;
		} else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
			throw std::runtime_error("Failed to acquire swapchain image");
		}

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

		VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
			throw std::runtime_error("Failed to submit draw command buffer");

		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;

		VkSwapchainKHR swapChains[] = { swapChain };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapChains;
		presentInfo.pImageIndices = &imageIndex;
		presentInfo.pResults = nullptr;

		vkQueuePresentKHR(presentQueue, &presentInfo);
		vkQueueWaitIdle(presentQueue);


	}

	void updateUniformBuffer() {
		static auto startTime = std::chrono::high_resolution_clock::now();

		auto currentTime = std::chrono::high_resolution_clock::now();
		float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

		UniformBufferObject ubo = {};
		ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.proj = glm::perspective(glm::radians(45.0f), swapChainExtent.width / (float)swapChainExtent.height, 0.1f, 10.0f);

		ubo.proj[1][1] *= -1;

		void *data;
		vkMapMemory(device, uniformBufferMemory, 0, sizeof(UniformBufferObject), 0, &data);
		memcpy(data, &ubo, sizeof(UniformBufferObject));
		vkUnmapMemory(device, uniformBufferMemory);
	}

	void cleanupSwapChain() {
		for (auto framebuffer : swapChainFramebuffers)
			vkDestroyFramebuffer(device, framebuffer, nullptr);

		vkFreeCommandBuffers(device, commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());

		vkDestroyPipeline(device, graphicsPipeline, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

		vkDestroyRenderPass(device, renderPass, nullptr);

		for (auto imageView : swapChainImageViews)
			vkDestroyImageView(device, imageView, nullptr);

		vkDestroySwapchainKHR(device, swapChain, nullptr);
	}

	void recreateSwapchain() {
		vkDeviceWaitIdle(device);

		cleanupSwapChain();

		createSwapChain();
		createImageViews();
		createRenderPass();
		createGraphicsPipeline();
		createFrameBuffer();
		createCommandBuffers();

	}


	void cleanup() {

		vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
		vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);

		cleanupSwapChain(); //destroy swapchain components

		vkDestroySampler(device, texSampler, nullptr); //destroy texture sampler

		vkDestroyImageView(device, texImgView, nullptr); //destroy texture image view

		vkDestroyImage(device, texImage, nullptr); //destroy texture image
		vkFreeMemory(device, texImageMem, nullptr); //free the device memory

		vkDestroyDescriptorSetLayout(device, desSetLayout, nullptr);

		vkDestroyDescriptorPool(device, desPool, nullptr);
		vkDestroyCommandPool(device, commandPool, nullptr);

		vkDestroyBuffer(device, indexBuffer, nullptr);
		vkFreeMemory(device, indexBufferMemory, nullptr);

		vkDestroyBuffer(device, vertexBuffer, nullptr);
		vkFreeMemory(device, vertexBufferMemory, nullptr);

		vkDestroyBuffer(device, uniformBuffer, nullptr);
		vkFreeMemory(device, uniformBufferMemory, nullptr);

		DestroyDebugReportCallbackEXT(instance, callback, nullptr);

		vkDestroySurfaceKHR(instance, surface, nullptr);

		vkDestroyInstance(instance, nullptr);

		glfwDestroyWindow(window);

		glfwTerminate();

	}
};

int main() {

	TriangleBasicsApp app;

	try {
		app.run();
	} catch (const std::runtime_error &e) {

		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}