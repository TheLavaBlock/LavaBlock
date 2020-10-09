// file      : liblava/base/instance.cpp
// copyright : Copyright (c) 2018-present, Lava Block OÜ
// license   : MIT; see accompanying LICENSE file

#include <liblava/base/instance.hpp>
#include <liblava/base/memory.hpp>

#define VK_LAYER_KHRONOS_VALIDATION_NAME "VK_LAYER_KHRONOS_validation"
#define VK_LAYER_RENDERDOC_CAPTURE_NAME "VK_LAYER_RENDERDOC_Capture"

namespace lava {

    instance::~instance() {
        destroy();
    }

    bool instance::check_debug(create_param& param) const {
        if (debug.validation) {
            if (!exists(param.layers, VK_LAYER_KHRONOS_VALIDATION_NAME))
                param.layers.push_back(VK_LAYER_KHRONOS_VALIDATION_NAME);
        }

        if (debug.render_doc) {
            if (!exists(param.layers, VK_LAYER_RENDERDOC_CAPTURE_NAME))
                param.layers.push_back(VK_LAYER_RENDERDOC_CAPTURE_NAME);
        }

        if (debug.utils) {
            if (!exists(param.extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
                param.extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        if (!check(param)) {
            log()->error("create instance param");

            for (auto& extension : param.extensions)
                log()->debug("extension: {}", extension);

            for (auto& layer : param.layers)
                log()->debug("layer: {}", layer);

            return false;
        }

        return true;
    }

    bool instance::create(create_param& param, debug_config::ref d, app_info::ref i) {
        debug = d;
        info = i;

        if (!check_debug(param))
            return false;

        ui32 app_version = VK_MAKE_VERSION(info.app_version.major,
                                           info.app_version.minor,
                                           info.app_version.patch);

        VkApplicationInfo application_info{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = info.app_name ? info.app_name : _lava_,
            .applicationVersion = app_version,
            .pEngineName = _liblava_,
        };

        application_info.engineVersion = VK_MAKE_VERSION(_internal_version.major,
                                                         _internal_version.minor,
                                                         _internal_version.patch);

        application_info.apiVersion = VK_API_VERSION_1_0;

        switch (info.req_api_version) {
        case api_version::v1_1:
            application_info.apiVersion = VK_API_VERSION_1_1;
            break;
        case api_version::v1_2:
            application_info.apiVersion = VK_API_VERSION_1_2;
            break;
        }

        VkInstanceCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &application_info,
            .enabledLayerCount = to_ui32(param.layers.size()),
            .ppEnabledLayerNames = param.layers.data(),
            .enabledExtensionCount = to_ui32(param.extensions.size()),
            .ppEnabledExtensionNames = param.extensions.data(),
        };

        auto result = vkCreateInstance(&create_info, memory::alloc(), &vk_instance);
        if (failed(result))
            return false;

        volkLoadInstance(vk_instance);

        if (!enumerate_physical_devices())
            return false;

        if (debug.utils)
            if (!create_validation_report())
                return false;

        return true;
    }

    void instance::destroy() {
        if (!vk_instance)
            return

                physical_devices.clear();

        if (debug.utils)
            destroy_validation_report();

        vkDestroyInstance(vk_instance, memory::alloc());
        vk_instance = nullptr;
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL validation_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                                              VkDebugUtilsMessageTypeFlagsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data) {
        auto message_header = fmt::format("validation: {} ({})", callback_data->pMessageIdName,
                                          callback_data->messageIdNumber);

        if (message_severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            log()->error(str(message_header));
            log()->error(callback_data->pMessage);

            assert(!"check validation error");
        } else if (message_severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            log()->warn(str(message_header));
            log()->warn(callback_data->pMessage);
        } else if (message_severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
            log()->info(str(message_header));
            log()->info(callback_data->pMessage);
        } else if (message_severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
            log()->trace(str(message_header));
            log()->trace(callback_data->pMessage);
        }

        return VK_FALSE;
    }

    bool instance::create_validation_report() {
        VkDebugUtilsMessengerCreateInfoEXT create_info{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = validation_callback,
        };

        if (debug.verbose)
            create_info.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;

        return check(vkCreateDebugUtilsMessengerEXT(vk_instance, &create_info, memory::alloc(), &debug_messenger));
    }

    void instance::destroy_validation_report() {
        if (!debug_messenger)
            return;

        vkDestroyDebugUtilsMessengerEXT(vk_instance, debug_messenger, memory::alloc());

        debug_messenger = 0;
    }

    VkLayerPropertiesList instance::enumerate_layer_properties() {
        auto layer_count = 0u;
        auto result = vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
        if (failed(result))
            return {};

        VkLayerPropertiesList list(layer_count);
        result = vkEnumerateInstanceLayerProperties(&layer_count, list.data());
        if (failed(result))
            return {};

        return list;
    }

    VkExtensionPropertiesList instance::enumerate_extension_properties(name layer_name) {
        auto property_count = 0u;
        auto result = vkEnumerateInstanceExtensionProperties(layer_name, &property_count, nullptr);
        if (failed(result))
            return {};

        VkExtensionPropertiesList list(property_count);
        result = vkEnumerateInstanceExtensionProperties(layer_name, &property_count, list.data());
        if (failed(result))
            return {};

        return list;
    }

    bool instance::enumerate_physical_devices() {
        physical_devices.clear();

        auto count = 0u;
        auto result = vkEnumeratePhysicalDevices(vk_instance, &count, nullptr);
        if (failed(result))
            return false;

        VkPhysicalDevices devices(count);
        result = vkEnumeratePhysicalDevices(vk_instance, &count, devices.data());
        if (failed(result))
            return false;

        for (auto& device : devices) {
            physical_device physical_device;
            physical_device.initialize(device);
            physical_devices.push_back(std::move(physical_device));
        }

        return true;
    }

    internal_version instance::get_version() {
        ui32 instance_version = VK_API_VERSION_1_0;

        auto enumerate_instance_version = (PFN_vkEnumerateInstanceVersion) vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion");
        if (enumerate_instance_version)
            enumerate_instance_version(&instance_version);

        internal_version version;
        version.major = VK_VERSION_MAJOR(instance_version);
        version.minor = VK_VERSION_MINOR(instance_version);
        version.patch = VK_VERSION_PATCH(VK_HEADER_VERSION);
        return version;
    }

} // namespace lava

bool lava::check(instance::create_param::ref param) {
    auto layer_properties = instance::enumerate_layer_properties();
    for (auto const& layer_name : param.layers) {
        auto itr = std::find_if(layer_properties.begin(), layer_properties.end(),
                                [&](VkLayerProperties const& extProp) {
                                    return strcmp(layer_name, extProp.layerName) == 0;
                                });

        if (itr == layer_properties.end())
            return false;
    }

    auto extensions_properties = instance::enumerate_extension_properties();
    for (auto const& ext_name : param.extensions) {
        auto itr = std::find_if(extensions_properties.begin(), extensions_properties.end(),
                                [&](VkExtensionProperties const& extProp) {
                                    return strcmp(ext_name, extProp.extensionName) == 0;
                                });

        if (itr == extensions_properties.end())
            return false;
    }

    return true;
}
