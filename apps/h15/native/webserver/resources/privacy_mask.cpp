#include "resources.hpp"
#include <iostream>

using namespace webserver::resources;

PrivacyMaskResource::PrivacyMaskResource() : Resource()
{
    m_privacy_masks = {};
    m_config = nlohmann::json::parse("{}");
}

std::shared_ptr<PrivacyMaskResource::PrivacyMaskResourceState> PrivacyMaskResource::parse_state(std::vector<std::string> current_enabled, std::vector<std::string> prev_enabled, nlohmann::json diff)
{
    auto state = std::make_shared<PrivacyMaskResource::PrivacyMaskResourceState>();
    for (auto &mask : m_privacy_masks)
    {
        if (std::find(current_enabled.begin(), current_enabled.end(), mask.first) != current_enabled.end() &&
            std::find(prev_enabled.begin(), prev_enabled.end(), mask.first) == prev_enabled.end())
        {
            state->changed_to_enabled.push_back(mask.first);
        }
        if (std::find(current_enabled.begin(), current_enabled.end(), mask.first) == current_enabled.end() &&
            std::find(prev_enabled.begin(), prev_enabled.end(), mask.first) != prev_enabled.end())
        {
            state->changed_to_disabled.push_back(mask.first);
        }
        if (std::find(current_enabled.begin(), current_enabled.end(), mask.first) != current_enabled.end() &&
            std::find(prev_enabled.begin(), prev_enabled.end(), mask.first) != prev_enabled.end())
        {
            //update all the polyagons that are still on the screen
            state->polygon_to_update.push_back(mask.first);
        }
    }
    for (auto &entry : diff)
    {
        if (entry["op"] == "remove")
        {
            state->polygon_to_delete.push_back(entry["path"].get<std::string>());
        }
    }
    return state;
}

std::vector<std::string> PrivacyMaskResource::get_enabled_masks()
{
    std::vector<std::string> enabled;
    for (const auto& mask : m_config["masks"]) {
        if(mask["status"].get<bool>() && m_config["global_enable"].get<bool>()){
            enabled.push_back(mask["id"].get<std::string>());
        }
    }
    return enabled;
}

void PrivacyMaskResource::parse_polygon(nlohmann::json j){
    try
    {
        for (const auto& mask : j["masks"]) {
            polygon poly;
            poly.id = mask["id"].get<std::string>();
            if (mask["Polygon"].empty()){
                WEBSERVER_LOG_WARNING("Got polygon without points in privacy mask, skipping");
                continue;
            }
            for (const auto& point : mask["Polygon"]) {
                poly.vertices.emplace_back(point["x"].get<uint>(), point["y"].get<uint>());
            }

            m_privacy_masks[poly.id] = poly;
        }
    }
    catch (const std::exception& e)
    {
        WEBSERVER_LOG_ERROR("Failed to parse json body for privacy mask, no change have been made");
    }
}

std::shared_ptr<PrivacyMaskResource::PrivacyMaskResourceState> PrivacyMaskResource::delete_masks_from_config(nlohmann::json config){
    std::shared_ptr<PrivacyMaskResource::PrivacyMaskResourceState> state = std::make_shared<PrivacyMaskResource::PrivacyMaskResourceState>();
    for (const auto& mask_to_del : config["masks"]) {
        std::string id = mask_to_del["id"].get<std::string>();
        if (m_config.contains("masks")) {
            state->polygon_to_delete.push_back(id);
            m_config["masks"].erase(std::remove_if(m_config["masks"].begin(), m_config["masks"].end(), [id](const nlohmann::json& mask){
                return mask["id"].get<std::string>() == id;
            }), m_config["masks"].end());
        }
    }
    return state;
}

void PrivacyMaskResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->Get("/privacy_mask", std::function<nlohmann::json()>([this]()
                                                              { return this->m_config; }));

    srv->Patch("/privacy_mask", [this](const nlohmann::json &partial_config)
            {
                auto prev_enabled = this->get_enabled_masks();
                nlohmann::json diff = nlohmann::json::diff(m_config, partial_config);
                m_config.merge_patch(partial_config);
                this->parse_polygon(m_config);
                on_resource_change(this->parse_state(this->get_enabled_masks(), prev_enabled, diff));
                return this->m_config; });

    srv->Put("/privacy_mask", [this](const nlohmann::json &config)
             {
                auto prev_enabled = this->get_enabled_masks();
                auto partial_config = nlohmann::json::diff(m_config, config);
                m_config = m_config.patch(partial_config);
                this->parse_polygon(m_config);
                on_resource_change(this->parse_state(this->get_enabled_masks(), prev_enabled, partial_config));
                return this->m_config; });

    srv->Delete("/privacy_mask", [this](const nlohmann::json &config){
        auto state = delete_masks_from_config(config);
        on_resource_change(state);
        return this->m_config;
    });
}