#include "resources.hpp"
#include <iostream>

webserver::resources::FrontendResource::FrontendResource(std::shared_ptr<webserver::resources::AiResource> ai_res, std::shared_ptr<webserver::resources::IspResource> isp_res, std::shared_ptr<webserver::resources::ConfigResource> configs) : Resource()
{
    m_config = configs->get_frontend_default_config();
    m_ai_resource = ai_res;
    m_isp_resource = isp_res;
    m_frontend_config.freeze = false;

    const auto& resolutions = m_config["output_video"]["resolutions"];
    for (const auto& res : resolutions)
    {
        m_frontend_config.resolutions.push_back({res["width"], res["height"], res["framerate"]});
    }

    update_frontend_config();
}

void webserver::resources::FrontendResource::update_frontend_config()
{
    std::string old_rotate = m_frontend_config.rotate;
    bool rotate_state_changed = m_frontend_config.rotate_enabled != m_config["rotation"]["enabled"] || (m_frontend_config.rotate_enabled && (m_frontend_config.rotate != m_config["rotation"]["angle"]));
    m_frontend_config.rotate_enabled = m_config["rotation"]["enabled"];
    m_frontend_config.rotate = m_config["rotation"]["angle"];

    std::vector<webserver::resources::FrontendResource::Resolution> old_resolutions;
    for (const auto& res : m_frontend_config.resolutions)
    {
        old_resolutions.push_back(res);
    }
    m_frontend_config.resolutions.clear();
    for (const auto& res : m_config["output_video"]["resolutions"])
    {
        m_frontend_config.resolutions.push_back({res["width"], res["height"], res["framerate"]});
    }
    bool at_least_one_stream_resolution_changed = false;
    bool at_least_one_stream_framerate_changed = false;
    std::vector<webserver::resources::StreamConfigResourceState::Resolution> resolutions;
    for (u_int32_t i = 0; i < m_frontend_config.resolutions.size(); i++)
    {
        bool framerate_changed = m_frontend_config.resolutions[i].framerate != old_resolutions[i].framerate;
        bool stream_size_changed = m_frontend_config.resolutions[i].width != old_resolutions[i].width || m_frontend_config.resolutions[i].height != old_resolutions[i].height;
        at_least_one_stream_framerate_changed = at_least_one_stream_framerate_changed || framerate_changed;
        at_least_one_stream_resolution_changed = at_least_one_stream_resolution_changed || stream_size_changed;
        resolutions.push_back({m_frontend_config.resolutions[i].width, m_frontend_config.resolutions[i].height, m_frontend_config.resolutions[i].framerate, framerate_changed, stream_size_changed});
    }
    if (at_least_one_stream_framerate_changed){
        auto state = StreamConfigResourceState(resolutions, m_frontend_config.rotate, m_frontend_config.rotate_enabled);
        on_resource_change(STREAM_CONFIG, std::make_shared<webserver::resources::StreamConfigResourceState>(state));
    }

    if (rotate_state_changed || at_least_one_stream_resolution_changed)
    {
        WEBSERVER_LOG_INFO("Frontend: Rotation state changed, updating frontend config and restarting frontendsrcbin");
        auto state = StreamConfigResourceState(resolutions, m_frontend_config.rotate, m_frontend_config.rotate_enabled);
        on_resource_change(STREAM_CONFIG,std::make_shared<webserver::resources::StreamConfigResourceState>(state));
        on_resource_change(RESTART_STREAM,std::make_shared<webserver::resources::StreamConfigResourceState>(state));
    }
}

void webserver::resources::FrontendResource::http_register(std::shared_ptr<HTTPServer> srv)
{
    srv->Get("/frontend", std::function<nlohmann::json()>([this]()
                                                          { return this->get_frontend_config(); }));

    srv->Patch("/frontend", [this](const nlohmann::json &partial_config)
               {
        m_config.merge_patch(partial_config);//check if rotate change angale or from false to true of true to false 
        update_frontend_config();
        auto result = this->m_config;
        auto state = FrontendResourceState(this->to_string(), m_frontend_config);
        on_resource_change(std::make_shared<webserver::resources::FrontendResource::FrontendResourceState>(state));
        return result; });

    srv->Put("/frontend", [this](const nlohmann::json &config)
             {
        auto partial_config = nlohmann::json::diff(m_config, config);
        m_config = m_config.patch(partial_config);
        update_frontend_config();
        auto result = this->m_config;
        on_resource_change(std::make_shared<webserver::resources::FrontendResource::FrontendResourceState>(FrontendResourceState(this->to_string(), m_frontend_config)));
        return result; });

    srv->Post("/frontend/freeze", std::function<void(const nlohmann::json &)>([this](const nlohmann::json &j_body)
                                                                              {
        m_frontend_config.freeze_state_changed = m_frontend_config.freeze != j_body["value"];
        m_frontend_config.freeze = j_body["value"];
        on_resource_change(std::make_shared<webserver::resources::FrontendResource::FrontendResourceState>(FrontendResourceState(this->to_string(), m_frontend_config)));
        }));
}

nlohmann::json webserver::resources::FrontendResource::get_frontend_config()
{
    nlohmann::json conf = m_config;
    conf["denoise"] = m_ai_resource->get_ai_config(AiResource::AiApplications::AI_APPLICATION_DENOISE);
    conf["hdr"] = m_isp_resource->get_hdr_config();
    conf["rotation"]["enabled"] = m_frontend_config.rotate_enabled;
    conf["rotation"]["angle"] = m_frontend_config.rotate;
    return conf;
}
