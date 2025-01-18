#include "openxr.h"
#include "instance.h"


static XrBool32 XR_DebugUtilsMessengerCallback(XrDebugUtilsMessageSeverityFlagsEXT messageSeverity, XrDebugUtilsMessageTypeFlagsEXT messageType, const XrDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData) {
    //Log::print("[OpenXR Debug Utils] Function {}: {}", callbackData->functionName, callbackData->message);
    return XR_FALSE;
}

OpenXR::OpenXR() {
    uint32_t xrExtensionCount = 0;
    xrEnumerateInstanceExtensionProperties(NULL, 0, &xrExtensionCount, NULL);
    std::vector<XrExtensionProperties> instanceExtensions;
    instanceExtensions.resize(xrExtensionCount, { XR_TYPE_EXTENSION_PROPERTIES, NULL });
    checkXRResult(xrEnumerateInstanceExtensionProperties(NULL, xrExtensionCount, &xrExtensionCount, instanceExtensions.data()), "Couldn't enumerate OpenXR extensions!");

    // Create instance with required extensions
    bool d3d12Supported = false;
    bool depthSupported = false;
    bool timeConvSupported = false;
    bool debugUtilsSupported = false;
    for (XrExtensionProperties& extensionProperties : instanceExtensions) {
        Log::print("Found available OpenXR extension: {}", extensionProperties.extensionName);
        if (strcmp(extensionProperties.extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME) == 0) {
            d3d12Supported = true;
        }
        if (strcmp(extensionProperties.extensionName, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME) == 0) {
            depthSupported = true;
        }
        else if (strcmp(extensionProperties.extensionName, XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME) == 0) {
            timeConvSupported = true;
        }
        else if (strcmp(extensionProperties.extensionName, XR_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
#if defined(_DEBUG)
            debugUtilsSupported = true;
#endif
        }
    }

    if (!d3d12Supported) {
        Log::print("OpenXR runtime doesn't support D3D12 (XR_KHR_D3D12_ENABLE)!");
        throw std::runtime_error("Current OpenXR runtime doesn't support Direct3D 12 (XR_KHR_D3D12_ENABLE). See the Github page's troubleshooting section for a solution!");
    }
    if (!depthSupported) {
        Log::print("OpenXR runtime doesn't support depth composition layers (XR_KHR_COMPOSITION_LAYER_DEPTH)!");
        throw std::runtime_error("Current OpenXR runtime doesn't support depth composition layers (XR_KHR_COMPOSITION_LAYER_DEPTH). See the Github page's troubleshooting section for a solution!");
    }
    if (!timeConvSupported) {
        Log::print("OpenXR runtime doesn't support converting time from/to XrTime (XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME)!");
        throw std::runtime_error("Current OpenXR runtime doesn't support converting time from/to XrTime (XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME). See the Github page's troubleshooting section for a solution!");
    }
    if (!debugUtilsSupported) {
        Log::print("OpenXR runtime doesn't support debug utils (XR_EXT_DEBUG_UTILS)! Errors/debug information will no longer be able to be shown!");
    }

    std::vector<const char*> enabledExtensions = { XR_KHR_D3D12_ENABLE_EXTENSION_NAME, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME, XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME };
    if (debugUtilsSupported) enabledExtensions.emplace_back(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);

    XrInstanceCreateInfo xrInstanceCreateInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
    xrInstanceCreateInfo.createFlags = 0;
    xrInstanceCreateInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
    xrInstanceCreateInfo.enabledExtensionNames = enabledExtensions.data();
    xrInstanceCreateInfo.enabledApiLayerCount = 0;
    xrInstanceCreateInfo.enabledApiLayerNames = NULL;
    xrInstanceCreateInfo.applicationInfo = { "BetterVR", 1, "Cemu", 1, XR_CURRENT_API_VERSION };
    checkXRResult(xrCreateInstance(&xrInstanceCreateInfo, &m_instance), "Failed to initialize the OpenXR instance!");

    // Load extension pointers for this XrInstance
    xrGetInstanceProcAddr(m_instance, "xrGetD3D12GraphicsRequirementsKHR", (PFN_xrVoidFunction*)&func_xrGetD3D12GraphicsRequirementsKHR);
    if (timeConvSupported) {
        xrGetInstanceProcAddr(m_instance, "xrConvertTimeToWin32PerformanceCounterKHR", (PFN_xrVoidFunction*)&func_xrConvertTimeToWin32PerformanceCounterKHR);
        xrGetInstanceProcAddr(m_instance, "xrConvertWin32PerformanceCounterToTimeKHR", (PFN_xrVoidFunction*)&func_xrConvertWin32PerformanceCounterToTimeKHR);
    }
    if (debugUtilsSupported) {
        xrGetInstanceProcAddr(m_instance, "xrCreateDebugUtilsMessengerEXT", (PFN_xrVoidFunction*)&func_xrCreateDebugUtilsMessengerEXT);
        xrGetInstanceProcAddr(m_instance, "xrDestroyDebugUtilsMessengerEXT", (PFN_xrVoidFunction*)&func_xrDestroyDebugUtilsMessengerEXT);
    }

    // Create debug utils messenger
    if (debugUtilsSupported) {
        XrDebugUtilsMessengerCreateInfoEXT utilsMessengerCreateInfo = { XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        utilsMessengerCreateInfo.messageSeverities = XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        utilsMessengerCreateInfo.messageTypes = XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
        utilsMessengerCreateInfo.userCallback = &XR_DebugUtilsMessengerCallback;
        func_xrCreateDebugUtilsMessengerEXT(m_instance, &utilsMessengerCreateInfo, &m_debugMessengerHandle);
    }

    // Get system information
    XrSystemGetInfo xrSystemGetInfo = { XR_TYPE_SYSTEM_GET_INFO };
    xrSystemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    checkXRResult(xrGetSystem(m_instance, &xrSystemGetInfo, &m_systemId), "No (available) head mounted display found!");

    XrSystemProperties xrSystemProperties = { XR_TYPE_SYSTEM_PROPERTIES };
    checkXRResult(xrGetSystemProperties(m_instance, m_systemId, &xrSystemProperties), "Couldn't get system properties of the given VR headset!");
    m_capabilities.supportsOrientational = xrSystemProperties.trackingProperties.orientationTracking;
    m_capabilities.supportsPositional = xrSystemProperties.trackingProperties.positionTracking;

    XrInstanceProperties properties = { XR_TYPE_INSTANCE_PROPERTIES };
    checkXRResult(xrGetInstanceProperties(m_instance, &properties), "Failed to get runtime details using xrGetInstanceProperties!");

    XrViewConfigurationProperties stereoViewConfiguration = { XR_TYPE_VIEW_CONFIGURATION_PROPERTIES };
    checkXRResult(xrGetViewConfigurationProperties(m_instance, m_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, &stereoViewConfiguration), "There's no VR headset available that allows stereo rendering!");
    m_capabilities.supportsMutatableFOV = stereoViewConfiguration.fovMutable;

    XrGraphicsRequirementsD3D12KHR graphicsRequirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR };
    checkXRResult(func_xrGetD3D12GraphicsRequirementsKHR(m_instance, m_systemId, &graphicsRequirements), "Couldn't get D3D12 requirements for the given VR headset!");
    m_capabilities.adapter = graphicsRequirements.adapterLuid;
    m_capabilities.minFeatureLevel = graphicsRequirements.minFeatureLevel;

    // Print configuration used, mostly for debugging purposes
    Log::print("Acquired system to be used:");
    Log::print(" - System Name: {}", xrSystemProperties.systemName);
    Log::print(" - Runtime Name: {}", properties.runtimeName);
    Log::print(" - Runtime Version: {}.{}.{}", XR_VERSION_MAJOR(properties.runtimeVersion), XR_VERSION_MINOR(properties.runtimeVersion), XR_VERSION_PATCH(properties.runtimeVersion));
    Log::print(" - Supports Mutable FOV: {}", m_capabilities.supportsMutatableFOV ? "Yes" : "No");
    Log::print(" - Supports Orientation Tracking: {}", xrSystemProperties.trackingProperties.orientationTracking ? "Yes" : "No");
    Log::print(" - Supports Positional Tracking: {}", xrSystemProperties.trackingProperties.positionTracking ? "Yes" : "No");
    Log::print(" - Supports D3D12 feature level {} or higher", graphicsRequirements.minFeatureLevel);
}

OpenXR::~OpenXR() {
    this->m_renderer.reset();

    if (m_headSpace != XR_NULL_HANDLE) {
        xrDestroySpace(m_headSpace);
    }

    if (m_stageSpace != XR_NULL_HANDLE) {
        xrDestroySpace(m_stageSpace);
    }

    if (m_session != XR_NULL_HANDLE) {
        xrDestroySession(m_session);
    }

    if (m_debugMessengerHandle != XR_NULL_HANDLE) {
        func_xrDestroyDebugUtilsMessengerEXT(m_debugMessengerHandle);
    }

    if (m_instance != XR_NULL_HANDLE) {
        xrDestroyInstance(m_instance);
    }
}

std::array<XrViewConfigurationView, 2> OpenXR::GetViewConfigurations() {
    uint32_t eyeViewsConfigurationCount = 0;
    checkXRResult(xrEnumerateViewConfigurationViews(m_instance, m_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &eyeViewsConfigurationCount, nullptr), "Can't get number of individual views for stereo view available");
    checkAssert(eyeViewsConfigurationCount == 2, std::format("Expected 2 views for the stereo configuration but got {} which is unsupported!", eyeViewsConfigurationCount).c_str());

    std::array<XrViewConfigurationView, 2> xrViewConf = { XrViewConfigurationView{ XR_TYPE_VIEW_CONFIGURATION_VIEW }, XrViewConfigurationView{ XR_TYPE_VIEW_CONFIGURATION_VIEW } };
    checkXRResult(xrEnumerateViewConfigurationViews(m_instance, m_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, eyeViewsConfigurationCount, &eyeViewsConfigurationCount, xrViewConf.data()), "Can't get individual views for stereo view available!");

    Log::print("Swapchain configuration to be used:");
    Log::print(" - [Left] Max View Resolution: w={}, h={} with {} samples", xrViewConf[0].maxImageRectWidth, xrViewConf[0].maxImageRectHeight, xrViewConf[0].maxSwapchainSampleCount);
    Log::print(" - [Right] Max View Resolution: w={}, h={}  with {} samples", xrViewConf[1].maxImageRectWidth, xrViewConf[1].maxImageRectHeight, xrViewConf[1].maxSwapchainSampleCount);
    Log::print(" - [Left] Recommended View Resolution: w={}, h={}  with {} samples", xrViewConf[0].recommendedImageRectWidth, xrViewConf[0].recommendedImageRectHeight, xrViewConf[0].recommendedSwapchainSampleCount);
    Log::print(" - [Right] Recommended View Resolution: w={}, h={}  with {} samples", xrViewConf[1].recommendedImageRectWidth, xrViewConf[1].recommendedImageRectHeight, xrViewConf[0].recommendedSwapchainSampleCount);
    return xrViewConf;
}

void OpenXR::CreateSession(const XrGraphicsBindingD3D12KHR& d3d12Binding) {
    Log::print("Creating the OpenXR session...");

    XrSessionCreateInfo sessionCreateInfo = { XR_TYPE_SESSION_CREATE_INFO };
    sessionCreateInfo.systemId = m_systemId;
    sessionCreateInfo.next = &d3d12Binding;
    sessionCreateInfo.createFlags = 0;
    checkXRResult(xrCreateSession(m_instance, &sessionCreateInfo, &m_session), "Failed to create Vulkan-based OpenXR session!");

    Log::print("Creating the OpenXR spaces...");
    XrReferenceSpaceCreateInfo stageSpaceCreateInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    stageSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    stageSpaceCreateInfo.poseInReferenceSpace = s_xrIdentityPose;
    checkXRResult(xrCreateReferenceSpace(m_session, &stageSpaceCreateInfo, &m_stageSpace), "Failed to create reference space for stage!");

    XrReferenceSpaceCreateInfo headSpaceCreateInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    headSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    headSpaceCreateInfo.poseInReferenceSpace = s_xrIdentityPose;
    checkXRResult(xrCreateReferenceSpace(m_session, &headSpaceCreateInfo, &m_headSpace), "Failed to create reference space for head!");
}

void OpenXR::CreateActions() {
    Log::print("Creating the OpenXR actions...");

    XrActionSetCreateInfo actionSetInfo = { XR_TYPE_ACTION_SET_CREATE_INFO };
    strcpy_s(actionSetInfo.actionSetName, "gameplay");
    strcpy_s(actionSetInfo.localizedActionSetName, "Gameplay");
    actionSetInfo.priority = 0;
    checkXRResult(xrCreateActionSet(m_instance, &actionSetInfo, &m_gameplayActionSet), "Failed to create controller bindings!");

    m_handPaths = { GetXRPath("/user/hand/left"), GetXRPath("/user/hand/right") };

    {
        XrActionCreateInfo grabActionInfo = { XR_TYPE_ACTION_CREATE_INFO };
        grabActionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        strcpy_s(grabActionInfo.actionName, "grab");
        strcpy_s(grabActionInfo.localizedActionName, "Grab");
        grabActionInfo.countSubactionPaths = (uint32_t)m_handPaths.size();
        grabActionInfo.subactionPaths = m_handPaths.data();
        checkXRResult(xrCreateAction(m_gameplayActionSet, &grabActionInfo, &m_grabAction), "Failed to create grab action!");

        XrActionCreateInfo selectActionInfo = { XR_TYPE_ACTION_CREATE_INFO };
        selectActionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        strcpy_s(selectActionInfo.actionName, "select");
        strcpy_s(selectActionInfo.localizedActionName, "Select");
        selectActionInfo.countSubactionPaths = (uint32_t)m_handPaths.size();
        selectActionInfo.subactionPaths = m_handPaths.data();
        checkXRResult(xrCreateAction(m_gameplayActionSet, &selectActionInfo, &m_selectAction), "Failed to create select action!");

        XrActionCreateInfo cancelActionInfo = { XR_TYPE_ACTION_CREATE_INFO };
        cancelActionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        strcpy_s(cancelActionInfo.actionName, "cancel");
        strcpy_s(cancelActionInfo.localizedActionName, "Cancel");
        cancelActionInfo.countSubactionPaths = (uint32_t)m_handPaths.size();
        cancelActionInfo.subactionPaths = m_handPaths.data();
        checkXRResult(xrCreateAction(m_gameplayActionSet, &cancelActionInfo, &m_cancelAction), "Failed to create cancel action!");

        XrActionCreateInfo jumpActionInfo = { XR_TYPE_ACTION_CREATE_INFO };
        jumpActionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        strcpy_s(jumpActionInfo.actionName, "jump");
        strcpy_s(jumpActionInfo.localizedActionName, "Jump");
        jumpActionInfo.countSubactionPaths = (uint32_t)m_handPaths.size();
        jumpActionInfo.subactionPaths = m_handPaths.data();
        checkXRResult(xrCreateAction(m_gameplayActionSet, &jumpActionInfo, &m_jumpAction), "Failed to create jump action!");

        XrActionCreateInfo mapActionInfo = { XR_TYPE_ACTION_CREATE_INFO };
        mapActionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        strcpy_s(mapActionInfo.actionName, "map");
        strcpy_s(mapActionInfo.localizedActionName, "Map");
        mapActionInfo.countSubactionPaths = (uint32_t)m_handPaths.size();
        mapActionInfo.subactionPaths = m_handPaths.data();
        checkXRResult(xrCreateAction(m_gameplayActionSet, &mapActionInfo, &m_mapAction), "Failed to create map action!");

        XrActionCreateInfo menuActionInfo = { XR_TYPE_ACTION_CREATE_INFO };
        menuActionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        strcpy_s(menuActionInfo.actionName, "menu");
        strcpy_s(menuActionInfo.localizedActionName, "Menu");
        menuActionInfo.countSubactionPaths = (uint32_t)m_handPaths.size();
        menuActionInfo.subactionPaths = m_handPaths.data();
        checkXRResult(xrCreateAction(m_gameplayActionSet, &menuActionInfo, &m_menuAction), "Failed to create menu action!");

        XrActionCreateInfo moveActionInfo = { XR_TYPE_ACTION_CREATE_INFO };
        moveActionInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
        strcpy_s(moveActionInfo.actionName, "move");
        strcpy_s(moveActionInfo.localizedActionName, "Move");
        moveActionInfo.countSubactionPaths = (uint32_t)m_handPaths.size();
        moveActionInfo.subactionPaths = m_handPaths.data();
        checkXRResult(xrCreateAction(m_gameplayActionSet, &moveActionInfo, &m_moveAction), "Failed to create move action!");

        XrActionCreateInfo cameraActionInfo = { XR_TYPE_ACTION_CREATE_INFO };
        cameraActionInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
        strcpy_s(cameraActionInfo.actionName, "camera");
        strcpy_s(cameraActionInfo.localizedActionName, "Camera");
        cameraActionInfo.countSubactionPaths = (uint32_t)m_handPaths.size();
        cameraActionInfo.subactionPaths = m_handPaths.data();
        checkXRResult(xrCreateAction(m_gameplayActionSet, &cameraActionInfo, &m_cameraAction), "Failed to create camera action!");

        XrActionCreateInfo poseActionInfo = { XR_TYPE_ACTION_CREATE_INFO };
        poseActionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
        strcpy_s(poseActionInfo.actionName, "pose");
        strcpy_s(poseActionInfo.localizedActionName, "Pose");
        poseActionInfo.countSubactionPaths = (uint32_t)m_handPaths.size();
        poseActionInfo.subactionPaths = m_handPaths.data();
        checkXRResult(xrCreateAction(m_gameplayActionSet, &poseActionInfo, &m_poseAction), "Failed to create hand pose action!");
    }

    {
        std::array<XrActionSuggestedBinding, 6> suggestedBindings = {
            XrActionSuggestedBinding{ .action = m_selectAction, .binding = GetXRPath("/user/hand/right/input/menu/click") },
            XrActionSuggestedBinding{ .action = m_selectAction, .binding = GetXRPath("/user/hand/left/input/menu/click") },
            XrActionSuggestedBinding{ .action = m_grabAction, .binding = GetXRPath("/user/hand/right/input/select/click") },
            XrActionSuggestedBinding{ .action = m_grabAction, .binding = GetXRPath("/user/hand/left/input/select/click") },
            XrActionSuggestedBinding{ .action = m_poseAction, .binding = GetXRPath("/user/hand/right/input/grip/pose") },
            XrActionSuggestedBinding{ .action = m_poseAction, .binding = GetXRPath("/user/hand/left/input/grip/pose") }
        };
        XrInteractionProfileSuggestedBinding suggestedBindingsInfo = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        suggestedBindingsInfo.interactionProfile = GetXRPath("/interaction_profiles/khr/simple_controller");
        suggestedBindingsInfo.countSuggestedBindings = (uint32_t)suggestedBindings.size();
        suggestedBindingsInfo.suggestedBindings = suggestedBindings.data();
        checkXRResult(xrSuggestInteractionProfileBindings(m_instance, &suggestedBindingsInfo), "Failed to suggest simple controller profile bindings!");
    }

    {
        std::array<XrActionSuggestedBinding, 12> suggestedBindings = {
            XrActionSuggestedBinding{ .action = m_selectAction, .binding = GetXRPath("/user/hand/left/input/trigger/value") },
            XrActionSuggestedBinding{ .action = m_selectAction, .binding = GetXRPath("/user/hand/right/input/trigger/value") },
            XrActionSuggestedBinding{ .action = m_grabAction, .binding = GetXRPath("/user/hand/left/input/squeeze/value") },
            XrActionSuggestedBinding{ .action = m_grabAction, .binding = GetXRPath("/user/hand/right/input/squeeze/value") },

            XrActionSuggestedBinding{ .action = m_jumpAction, .binding = GetXRPath("/user/hand/right/input/a/click") },
            XrActionSuggestedBinding{ .action = m_cancelAction, .binding = GetXRPath("/user/hand/right/input/b/click") },
            XrActionSuggestedBinding{ .action = m_mapAction, .binding = GetXRPath("/user/hand/left/input/menu/click") },
            XrActionSuggestedBinding{ .action = m_menuAction, .binding = GetXRPath("/user/hand/left/input/y/click") },

            XrActionSuggestedBinding{ .action = m_moveAction, .binding = GetXRPath("/user/hand/left/input/thumbstick") },
            XrActionSuggestedBinding{ .action = m_cameraAction, .binding = GetXRPath("/user/hand/right/input/thumbstick") },
            XrActionSuggestedBinding{ .action = m_poseAction, .binding = GetXRPath("/user/hand/left/input/grip/pose") },
            XrActionSuggestedBinding{ .action = m_poseAction, .binding = GetXRPath("/user/hand/right/input/grip/pose") },
        };
        XrInteractionProfileSuggestedBinding suggestedBindingsInfo = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        suggestedBindingsInfo.interactionProfile = GetXRPath("/interaction_profiles/oculus/touch_controller");
        suggestedBindingsInfo.countSuggestedBindings = (uint32_t)suggestedBindings.size();
        suggestedBindingsInfo.suggestedBindings = suggestedBindings.data();
        checkXRResult(xrSuggestInteractionProfileBindings(m_instance, &suggestedBindingsInfo), "Failed to suggest touch controller profile bindings!");
    }

    {
        std::array<XrActionSuggestedBinding, 12> suggestedBindings = {
            XrActionSuggestedBinding{ .action = m_selectAction, .binding = GetXRPath("/user/hand/left/input/trigger/value") },
            XrActionSuggestedBinding{ .action = m_selectAction, .binding = GetXRPath("/user/hand/right/input/trigger/value") },
            XrActionSuggestedBinding{ .action = m_grabAction, .binding = GetXRPath("/user/hand/left/input/squeeze/click") },
            XrActionSuggestedBinding{ .action = m_grabAction, .binding = GetXRPath("/user/hand/right/input/squeeze/click") },

            XrActionSuggestedBinding{ .action = m_jumpAction, .binding = GetXRPath("/user/hand/right/input/trackpad/click") },
            XrActionSuggestedBinding{ .action = m_cancelAction, .binding = GetXRPath("/user/hand/right/input/thumbstick/click") },
            XrActionSuggestedBinding{ .action = m_mapAction, .binding = GetXRPath("/user/hand/left/input/menu/click") },
            XrActionSuggestedBinding{ .action = m_menuAction, .binding = GetXRPath("/user/hand/right/input/menu/click") },

            XrActionSuggestedBinding{ .action = m_moveAction, .binding = GetXRPath("/user/hand/left/input/thumbstick") },
            XrActionSuggestedBinding{ .action = m_cameraAction, .binding = GetXRPath("/user/hand/right/input/thumbstick") },
            XrActionSuggestedBinding{ .action = m_poseAction, .binding = GetXRPath("/user/hand/left/input/grip/pose") },
            XrActionSuggestedBinding{ .action = m_poseAction, .binding = GetXRPath("/user/hand/right/input/grip/pose") },
        };
        XrInteractionProfileSuggestedBinding suggestedBindingsInfo = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        suggestedBindingsInfo.interactionProfile = GetXRPath("/interaction_profiles/microsoft/motion_controller");
        suggestedBindingsInfo.countSuggestedBindings = (uint32_t)suggestedBindings.size();
        suggestedBindingsInfo.suggestedBindings = suggestedBindings.data();
        checkXRResult(xrSuggestInteractionProfileBindings(m_instance, &suggestedBindingsInfo), "Failed to suggest microsoft motion controller profile bindings!");
    }

    XrSessionActionSetsAttachInfo attachInfo = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    std::array<XrActionSet, 1> actionSets = { m_gameplayActionSet };
    attachInfo.countActionSets = (uint32_t)actionSets.size();
    attachInfo.actionSets = actionSets.data();
    checkXRResult(xrAttachSessionActionSets(m_session, &attachInfo), "Failed to attach action sets to session!");

    for (EyeSide side : { EyeSide::LEFT, EyeSide::RIGHT }) {
        XrActionSpaceCreateInfo createInfo = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
        createInfo.action = m_poseAction;
        createInfo.subactionPath = m_handPaths[side];
        createInfo.poseInActionSpace = s_xrIdentityPose;
        checkXRResult(xrCreateActionSpace(m_session, &createInfo, &m_input.controllers[side].poseSpace), "Failed to create action space for hand pose!");
    }
}

void OpenXR::UpdateActions(XrTime predictedFrameTime) {
    XrActionsSyncInfo syncInfo = { XR_TYPE_ACTIONS_SYNC_INFO };
    std::array<XrActiveActionSet, 1> activeActionSet = { XrActiveActionSet{ m_gameplayActionSet, XR_NULL_PATH } };
    syncInfo.countActiveActionSets = (uint32_t)activeActionSet.size();
    syncInfo.activeActionSets = activeActionSet.data();
    checkXRResult(xrSyncActions(m_session, &syncInfo), "Failed to sync actions!");

    for (EyeSide side : { EyeSide::LEFT, EyeSide::RIGHT }) {
        XrActionStateGetInfo getGrabInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
        getGrabInfo.action = m_grabAction;
        getGrabInfo.subactionPath = m_handPaths[side];
        m_input.controllers[side].grab = { XR_TYPE_ACTION_STATE_BOOLEAN };
        checkXRResult(xrGetActionStateBoolean(m_session, &getGrabInfo, &m_input.controllers[side].grab), "Failed to get grab action value!");

        XrActionStateGetInfo getSelectInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
        getSelectInfo.action = m_selectAction;
        getSelectInfo.subactionPath = m_handPaths[side];
        m_input.controllers[side].select = { XR_TYPE_ACTION_STATE_BOOLEAN };
        checkXRResult(xrGetActionStateBoolean(m_session, &getSelectInfo, &m_input.controllers[side].select), "Failed to get select action value!");

        XrActionStateGetInfo getPoseInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
        getPoseInfo.action = m_poseAction;
        getPoseInfo.subactionPath = m_handPaths[side];
        m_input.controllers[side].pose = { XR_TYPE_ACTION_STATE_POSE };
        checkXRResult(xrGetActionStatePose(m_session, &getPoseInfo, &m_input.controllers[side].pose), "Failed to get pose action value!");

        if (m_input.controllers[side].pose.isActive) {
            XrSpaceLocation spaceLocation = { XR_TYPE_SPACE_LOCATION };
            checkXRResult(xrLocateSpace(m_input.controllers[side].poseSpace, m_stageSpace, m_frameTimes[side], &spaceLocation), "Failed to get location from controllers!");
            if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 && (spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
                m_input.controllers[side].poseLocation = spaceLocation;
            }
        }
    }

    XrActionStateGetInfo getMoveInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
    getMoveInfo.action = m_moveAction;
    getMoveInfo.subactionPath = XR_NULL_PATH;
    m_input.move = { XR_TYPE_ACTION_STATE_VECTOR2F };
    checkXRResult(xrGetActionStateVector2f(m_session, &getMoveInfo, &m_input.move), "Failed to get move action value!");

    XrActionStateGetInfo getCameraInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
    getCameraInfo.action = m_cameraAction;
    getCameraInfo.subactionPath = XR_NULL_PATH;
    m_input.camera = { XR_TYPE_ACTION_STATE_VECTOR2F };
    checkXRResult(xrGetActionStateVector2f(m_session, &getCameraInfo, &m_input.camera), "Failed to get camera action value!");

    XrActionStateGetInfo getCancelInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
    getCancelInfo.action = m_cancelAction;
    getCancelInfo.subactionPath = XR_NULL_PATH;
    m_input.cancel = { XR_TYPE_ACTION_STATE_BOOLEAN };
    checkXRResult(xrGetActionStateBoolean(m_session, &getCancelInfo, &m_input.cancel), "Failed to get cancel action value!");

    XrActionStateGetInfo getJumpInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
    getJumpInfo.action = m_jumpAction;
    getJumpInfo.subactionPath = XR_NULL_PATH;
    m_input.jump = { XR_TYPE_ACTION_STATE_BOOLEAN };
    checkXRResult(xrGetActionStateBoolean(m_session, &getJumpInfo, &m_input.jump), "Failed to get jump action value!");

    XrActionStateGetInfo getMapInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
    getMapInfo.action = m_mapAction;
    getMapInfo.subactionPath = XR_NULL_PATH;
    m_input.map = { XR_TYPE_ACTION_STATE_BOOLEAN };
    checkXRResult(xrGetActionStateBoolean(m_session, &getMapInfo, &m_input.map), "Failed to get map action value!");

    XrActionStateGetInfo getMenuInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
    getMenuInfo.action = m_menuAction;
    getMenuInfo.subactionPath = XR_NULL_PATH;
    m_input.menu = { XR_TYPE_ACTION_STATE_BOOLEAN };
    checkXRResult(xrGetActionStateBoolean(m_session, &getMenuInfo, &m_input.menu), "Failed to get menu action value!");
}

bool firstInit = true;
void OpenXR::UpdateTime(EyeSide side, XrTime predictedDisplayTime) {
    if (firstInit) {
        firstInit = false;
        m_frameTimes[EyeSide::LEFT] = predictedDisplayTime;
        m_frameTimes[EyeSide::RIGHT] = predictedDisplayTime;
        return;
    }
    m_frameTimes[side] = predictedDisplayTime;
}

std::optional<XrSpaceLocation> OpenXR::UpdateSpaces(XrTime predictedDisplayTime) {
    XrSpaceLocation spaceLocation = { XR_TYPE_SPACE_LOCATION };
    if (XrResult result = xrLocateSpace(m_headSpace, m_stageSpace, predictedDisplayTime, &spaceLocation); XR_SUCCEEDED(result)) {
        if (result != XR_ERROR_TIME_INVALID) {
            checkXRResult(result, "Failed to get space location!");
        }
        checkXRResult(result, "Failed to get space location!");
    }
    if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0)
        return std::nullopt;

    return spaceLocation;
}

void OpenXR::ProcessEvents() {
    auto processSessionStateChangedEvent = [this](XrEventDataSessionStateChanged* stateChangedEvent) {
        switch (stateChangedEvent->state) {
            case XR_SESSION_STATE_IDLE:
                Log::print("OpenXR has indicated that the session is idle!");
                break;
            case XR_SESSION_STATE_READY: {
                Log::print("OpenXR has indicated that the session is ready!");
                if (m_renderer) {
                    Log::print("OpenXR has indicated that the session is ready, but we already have a renderer!");
                }
                else {
                    m_renderer = std::make_unique<RND_Renderer>(m_session);
                }
                break;
            }
            case XR_SESSION_STATE_SYNCHRONIZED:
                Log::print("OpenXR has indicated that the session is synchronized!");
                break;
            case XR_SESSION_STATE_FOCUSED:
                Log::print("OpenXR has indicated that the session is focused!");
                break;
            case XR_SESSION_STATE_VISIBLE:
                Log::print("OpenXR has indicated that the session should be visible!");
                break;
            case XR_SESSION_STATE_STOPPING:
                Log::print("OpenXR has indicated that the session should be ended!");
                //this->m_renderer.reset();
                break;
            case XR_SESSION_STATE_EXITING:
                Log::print("OpenXR has indicated that the session should be destroyed!");
                // an exception is thrown here instead of using exit() to allow Cemu to ideally gracefully shutdown
                //throw std::runtime_error("BetterVR mod has been requested to exit by OpenXR!");
                break;
            case XR_SESSION_STATE_LOSS_PENDING:
                Log::print("OpenXR has indicated that the session is going to be lost!");
                // todo: implement being able to continuously check if xrGetSystem returns and then reinitialize the session
                break;
            default:
                Log::print("OpenXR has indicated that an unknown session state has occurred!");
                break;
        }
    };

    XrEventDataBuffer eventData = { XR_TYPE_EVENT_DATA_BUFFER };
    XrResult result = xrPollEvent(m_instance, &eventData);

    while (result == XR_SUCCESS) {
        switch (eventData.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
                processSessionStateChangedEvent((XrEventDataSessionStateChanged*)&eventData);
                break;
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                Log::print("OpenXR has indicated that the instance is going to be lost!");
                break;
            case XR_TYPE_EVENT_DATA_EVENTS_LOST:
                Log::print("OpenXR has indicated that events are being lost!");
                break;
            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                Log::print("OpenXR has indicated that the interaction profile has changed!");
                break;
            default:
                Log::print("OpenXR has indicated that an unknown event with type {} has occurred!", std::to_underlying(eventData.type));
                break;
        }

        eventData = { XR_TYPE_EVENT_DATA_BUFFER };
        result = xrPollEvent(m_instance, &eventData);
    }
}
