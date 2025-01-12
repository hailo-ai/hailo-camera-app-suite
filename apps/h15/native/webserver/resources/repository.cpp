#include "resources.hpp"
#include "repository.hpp"

WebserverResourceRepository webserver::resources::ResourceRepository::create()
{
    std::vector<WebserverResource> resources_vec{};
    auto config_resource = std::make_shared<webserver::resources::ConfigResource>();
    auto osd_resource = std::make_shared<webserver::resources::OsdResource>();
    auto ai_resource = std::make_shared<webserver::resources::AiResource>(config_resource);
    auto isp_resource = std::make_shared<webserver::resources::IspResource>(ai_resource, config_resource, osd_resource);
    // auto isp_resource = std::make_shared<webserver::resources::IspResource>(ai_resource, config_resource);
    auto frontend_resource = std::make_shared<webserver::resources::FrontendResource>(ai_resource, isp_resource, config_resource);
    resources_vec.push_back(config_resource);
    resources_vec.push_back(ai_resource);
    resources_vec.push_back(isp_resource);
    resources_vec.push_back(frontend_resource);
    resources_vec.push_back(osd_resource);
    resources_vec.push_back(std::make_shared<webserver::resources::EncoderResource>(config_resource, frontend_resource));
    resources_vec.push_back(std::make_shared<webserver::resources::PrivacyMaskResource>(frontend_resource));
    resources_vec.push_back(std::make_shared<webserver::resources::WebpageResource>());
    resources_vec.push_back(std::make_shared<webserver::resources::WebRtcResource>());

    return std::make_shared<webserver::resources::ResourceRepository>(resources_vec);
}

webserver::resources::ResourceRepository::ResourceRepository(std::vector<WebserverResource> resources)
{
    for (const auto &resource : resources)
    {
        m_resources[resource->get_type()] = resource;
    }
}

std::map<webserver::resources::ResourceBehaviorType, std::vector<webserver::resources::ResourceType>> webserver::resources::ResourceRepository::get_all_types()
{
    std::map<webserver::resources::ResourceBehaviorType, std::vector<webserver::resources::ResourceType>> m = {{webserver::resources::RESOURCE_BEHAVIOR_CONFIG, {}}, {webserver::resources::RESOURCE_BEHAVIOR_FUNCTIONAL, {}}};
    for (const auto &[res_type, res] : m_resources)
    {
        m[res->get_behavior_type()].push_back(res_type);
    }
    return m;
}
