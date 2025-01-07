#include "resources.hpp"
#include <iostream>
#include <filesystem>

#define VD_NETWORK_PATH "/usr/lib/medialib/denoise_config/"
#define VD_L_NETWORK_FILE "vd_l_imx678.hef"
#define VD_M_NETWORK_FILE "vd_m_imx678.hef"
#define VD_S_NETWORK_FILE "vd_s_imx678.hef"
#define VD_L_NETWORK_HEF VD_NETWORK_PATH VD_L_NETWORK_FILE
#define VD_M_NETWORK_HEF VD_NETWORK_PATH VD_M_NETWORK_FILE
#define VD_S_NETWORK_HEF VD_NETWORK_PATH VD_S_NETWORK_FILE

inline std::string get_denoise_network_path(std::string network)
{
    if (network == "Small")
    {
        return VD_S_NETWORK_HEF;
    }
    else if (network == "Medium")
    {
        return VD_M_NETWORK_HEF;
    }
    else if (network == "Large")
    {
        return VD_L_NETWORK_HEF;
    }
    else
    {
        throw std::invalid_argument("Invalid denoise network size " + network);
    }
}

inline std::string get_denoise_network_from_path(std::string net_path)
{
    std::string filename = std::filesystem::path(net_path).filename().string();
    if (filename == VD_L_NETWORK_FILE)
    {
        return "Large";
    }
    else if (filename == VD_M_NETWORK_FILE)
    {
        return "Medium";
    }
    else if (filename == VD_S_NETWORK_FILE)
    {
        return "Small";
    }
    else
    {
        throw std::invalid_argument("Invalid denoise network path " + net_path);
    }
}

webserver::resources::AiResource::AiResource(std::shared_ptr<webserver::resources::ConfigResource> configs) : Resource()
{

    m_denoise_config = configs->get_denoise_default_config();

    m_default_config = R"(
    {
        "detection": {
            "enabled": true
        },
        "denoise": {
            "enabled": false,
            "network": "Large",
            "loopback-count": 1
        }
    })";

    m_config = nlohmann::json::parse(m_default_config);
    m_denoise_config["enabled"] = m_config["denoise"]["enabled"];
    m_config["denoise"]["network"] = get_denoise_network_from_path(m_denoise_config["network"]["network_path"]);
}

std::vector<webserver::resources::AiResource::AiApplications> webserver::resources::AiResource::get_enabled_applications()
{
    std::vector<webserver::resources::AiResource::AiApplications> enabled_applications;
    for (const auto &[key, value] : m_config.items())
    {
        if (value["enabled"])
        {
            if (key == "detection")
                enabled_applications.push_back(AiApplications::AI_APPLICATION_DETECTION);
            else if (key == "denoise")
                enabled_applications.push_back(AiApplications::AI_APPLICATION_DENOISE);
        }
    }
    return enabled_applications;
}

std::shared_ptr<webserver::resources::AiResource::AiResourceState> webserver::resources::AiResource::parse_state(std::vector<webserver::resources::AiResource::AiApplications> current_enabled, std::vector<webserver::resources::AiResource::AiApplications> prev_enabled)
{
    auto state = std::make_shared<AiResourceState>();
    for (const auto &app : current_enabled)
    {
        if (std::find(prev_enabled.begin(), prev_enabled.end(), app) == prev_enabled.end())
        {
            state->enabled.push_back(app);
        }
    }
    for (const auto &app : prev_enabled)
    {
        if (std::find(current_enabled.begin(), current_enabled.end(), app) == current_enabled.end())
        {
            state->disabled.push_back(app);
        }
    }
    return state;
}

void webserver::resources::AiResource::http_patch(nlohmann::json body)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto prev_enabled_apps = this->get_enabled_applications();
    m_config.merge_patch(body);
    auto current_enabled_apps = this->get_enabled_applications();


    m_denoise_config["enabled"] = m_config["denoise"]["enabled"];
    m_denoise_config["network"]["network_path"] = get_denoise_network_path(m_config["denoise"]["network"]);
    m_denoise_config["loopback-count"] = m_config["denoise"]["loopback-count"];
    WEBSERVER_LOG_INFO("AI: finished patching AI resource, calling on_resource_change");

    on_resource_change(this->parse_state(this->get_enabled_applications(), prev_enabled_apps));
}

void webserver::resources::AiResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->Get("/ai", std::function<nlohmann::json()>([this]()
                                                    { return this->m_config; }));

    srv->Patch("/ai", [this](const nlohmann::json &req)
               {
                http_patch(req);
                return this->m_config; });
}

nlohmann::json webserver::resources::AiResource::get_ai_config(AiApplications app)
{
    if (app == AiApplications::AI_APPLICATION_DENOISE)
        return m_denoise_config;
    return "";
}
