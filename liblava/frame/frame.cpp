/**
 * @file         liblava/frame/frame.cpp
 * @brief        Framework
 * @authors      Lava Block OÜ and contributors
 * @copyright    Copyright (c) 2018-present, MIT License
 */

#include <liblava/base/memory.hpp>
#include <liblava/frame/frame.hpp>

#ifndef LIBLAVA_HIDE_CONSOLE
    #define LIBLAVA_HIDE_CONSOLE (!LIBLAVA_DEBUG && _WIN32)
#endif

#if LIBLAVA_HIDE_CONSOLE
    #include <windows.h>
    #include <iostream>
#endif

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace lava {

/**
 * @brief Hide the console
 * 
 * @param program    Name of program
 */
void hide_console(name program) {
#if LIBLAVA_HIDE_CONSOLE

    auto version_str = fmt::format("{} {}", _liblava_, str(version_string()));
    std::cout << version_str.c_str() << std::endl;

    auto const dot_count = 5;
    auto const sleep_time = ms(to_i32(1. / dot_count * 1000.f));

    for (auto i = 0u; i < dot_count; ++i) {
        sleep(sleep_time);
        std::cout << ".";
    }

    FreeConsole();

#endif
}

/**
 * @brief Log command line
 * 
 * @param cmd_line    Command line parser
 */
void log_command_line(argh::parser& cmd_line) {
    if (!cmd_line.pos_args().empty()) {
        for (auto& pos_arg : cmd_line.pos_args())
            log()->info("cmd {}", str(pos_arg));
    }

    if (!cmd_line.flags().empty()) {
        for (auto& flag : cmd_line.flags())
            log()->info("cmd flag {}", str(flag));
    }

    if (!cmd_line.params().empty()) {
        for (auto& param : cmd_line.params())
            log()->info("cmd para {} = {}", str(param.first), str(param.second));
    }
}

//-----------------------------------------------------------------------------
ms now() {
    return to_ms(glfwGetTime());
}

/// Frame initialized state
static bool frame_initialized = false;

//-----------------------------------------------------------------------------
frame::frame(argh::parser cmd_line) {
    frame_config c;
    c.cmd_line = cmd_line;

    setup(c);
}

//-----------------------------------------------------------------------------
frame::frame(frame_config c) {
    setup(c);
}

//-----------------------------------------------------------------------------
frame::~frame() {
    teardown();
}

//-----------------------------------------------------------------------------
bool frame::ready() const {
    return frame_initialized;
}

/**
 * @brief Handle config
 * 
 * @param config    Frame config
 */
void handle_config(frame_config& config) {
#if LIBLAVA_DEBUG
    config.log.debug = true;
    config.debug.validation = true;
    config.debug.utils = true;
#endif

    hide_console(config.info.app_name);

    auto cmd_line = config.cmd_line;

    if (cmd_line[{ "-d", "--debug" }])
        config.debug.validation = true;

    if (cmd_line[{ "-r", "--renderdoc" }])
        config.debug.render_doc = true;

    if (cmd_line[{ "-v", "--verbose" }])
        config.debug.verbose = true;

    if (cmd_line[{ "-u", "--utils" }])
        config.debug.utils = true;

    if (auto log_level = -1; cmd_line({ "-l", "--log" }) >> log_level)
        config.log.level = log_level;

    setup_log(config.log);

    if (internal_version{} != config.info.app_version) {
        log()->info(">>> {} / {} - {} / {} - {} {}", str(version_string()),
                    str(internal_version_string()),
                    config.info.app_name,
                    str(to_string(config.info.app_version)),
                    _build_date, _build_time);
    } else {
        log()->info(">>> {} / {} - {} - {} {}", str(version_string()),
                    str(internal_version_string()),
                    config.info.app_name,
                    _build_date, _build_time);
    }

    log_command_line(cmd_line);

    if (config.log.level >= 0)
        log()->info("log {}", spdlog::level::to_string_view((spdlog::level::level_enum) config.log.level));
}

//-----------------------------------------------------------------------------
bool frame::setup(frame_config c) {
    if (frame_initialized)
        return false;

    config = c;
    handle_config(config);

    glfwSetErrorCallback([](i32 error, name description) {
        log()->error("glfw {} - {}", error, description);
    });

    log()->debug("glfw {}", glfwGetVersionString());

    if (glfwInit() != GLFW_TRUE) {
        log()->error("init glfw");
        return false;
    }

    if (glfwVulkanSupported() != GLFW_TRUE) {
        log()->error("vulkan not supported");
        return false;
    }

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    auto result = volkInitialize();
    if (failed(result)) {
        log()->error("init volk");
        return false;
    }

    log()->info("vulkan {}", str(to_string(instance::get_version())));

    auto glfw_extensions_count = 0u;
    auto glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extensions_count);
    for (auto i = 0u; i < glfw_extensions_count; ++i)
        config.param.extensions.push_back(glfw_extensions[i]);

    if (!instance::singleton().create(config.param, config.debug, config.info)) {
        log()->error("create instance");
        return false;
    }

    frame_initialized = true;

    log()->info("---");

    return true;
}

//-----------------------------------------------------------------------------
void frame::teardown() {
    if (!frame_initialized)
        return;

    manager.clear();

    instance::singleton().destroy();

    glfwTerminate();

    log()->info("<<<");

    log()->flush();
    spdlog::drop_all();

    frame_initialized = false;
}

//-----------------------------------------------------------------------------
frame::result frame::run() {
    if (running)
        return error::still_running;

    running = true;
    start_time = now();

    while (running) {
        if (!run_step())
            break;
    }

    manager.wait_idle();

    trigger_run_end();

    auto result = 0;
    if (running) {
        result = error::run_aborted;
        running = false;
    }

    start_time = {};

    return result;
}

//-----------------------------------------------------------------------------
bool frame::run_step() {
    handle_events(wait_for_events);

    if (!run_once_list.empty()) {
        for (auto& func : run_once_list)
            if (!func())
                return false;

        run_once_list.clear();
    }

    for (auto& func : run_map)
        if (!func.second())
            return false;

    return true;
}

//-----------------------------------------------------------------------------
bool frame::shut_down() {
    if (!running)
        return false;

    running = false;

    return true;
}

//-----------------------------------------------------------------------------
id frame::add_run(run_func_ref func) {
    auto id = ids::next();
    run_map.emplace(id, func);

    return id;
}

//-----------------------------------------------------------------------------
id frame::add_run_end(run_end_func_ref func) {
    auto id = ids::next();
    run_end_map.emplace(id, func);

    return id;
}

//-----------------------------------------------------------------------------
bool frame::remove(id::ref id) {
    auto result = false;

    if (run_map.count(id)) {
        run_map.erase(id);
        result = true;
    } else if (run_end_map.count(id)) {
        run_end_map.erase(id);
        result = true;
    }

    if (result)
        ids::free(id);

    return result;
}

//-----------------------------------------------------------------------------
void frame::trigger_run_end() {
    for (auto& func : reverse(run_end_map))
        func.second();
}

//-----------------------------------------------------------------------------
void handle_events(bool wait) {
    if (wait)
        glfwWaitEvents();
    else
        glfwPollEvents();
}

//-----------------------------------------------------------------------------
void handle_events(ms timeout) {
    glfwWaitEventsTimeout(to_sec(timeout));
}

//-----------------------------------------------------------------------------
void handle_events(seconds timeout) {
    glfwWaitEventsTimeout(to_r64(timeout.count()));
}

//-----------------------------------------------------------------------------
void post_empty_event() {
    glfwPostEmptyEvent();
}

} // namespace lava
