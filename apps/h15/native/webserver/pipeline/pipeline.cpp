#include "pipeline/pipeline.hpp"
#include "common/common.hpp"
#include "resources/resources.hpp"
#include <gst/gst.h>

using namespace webserver::pipeline;
using namespace webserver::resources;
#ifndef MEDIALIB_LOCAL_SERVER
using namespace privacy_mask_types;
#endif

#define DETECTION_HEF_PATH "/home/root/apps/webserver/resources/yolov5m_wo_spp_60p_nv12_640.hef"

std::shared_ptr<Pipeline> Pipeline::create()
{
    auto resources = ResourceRepository::create();
    return std::make_shared<Pipeline>(resources);
}

nlohmann::json create_encoder_osd_config(WebserverResourceRepository resources)
{
    auto osd_resource = std::static_pointer_cast<OsdResource>(resources->get(RESOURCE_OSD));
    nlohmann::json encoder_osd_config;
    encoder_osd_config["osd"] = osd_resource->get_current_osd_config();
    encoder_osd_config["encoding"] = resources->get(RESOURCE_ENCODER)->get();
    return encoder_osd_config;
}

std::string Pipeline::create_gst_pipeline_string()
{
    auto enabled_apps = std::static_pointer_cast<AiResource>(m_resources->get(RESOURCE_AI))->get_enabled_applications();
    auto detection_pass_through = std::find(enabled_apps.begin(), enabled_apps.end(), AiResource::AI_APPLICATION_DETECTION) != enabled_apps.end() ? "false" : "true";

    auto fe_resource = std::static_pointer_cast<FrontendResource>(m_resources->get(RESOURCE_FRONTEND));

    std::ostringstream pipeline;
    pipeline << "hailofrontendbinsrc name=frontend config-string='" << fe_resource->get_frontend_config() << "' ";
    pipeline << "hailomuxer name=mux ";
    pipeline << "frontend. ! ";
    pipeline << "queue name=q4 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "mux. ";
    pipeline << "frontend. ! ";
    pipeline << "queue name=q5 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "video/x-raw,format=NV12,width=1280,height=720 ! ";
    pipeline << "hailovideoscale use-letterbox=true ! ";
    pipeline << "video/x-raw, width=640, height=640 ! ";
    pipeline << "hailonet name=detection batch-size=4 hef-path=" << DETECTION_HEF_PATH << " pass-through=" << detection_pass_through << " nms-iou-threshold=0.45 nms-score-threshold=0.3 scheduling-algorithm=1 scheduler-threshold=4 scheduler-timeout-ms=1000 vdevice-group-id=1 vdevice-key=1 ! ";
    pipeline << "queue name=q6 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "hailofilter function-name=yolov5 config-path=/home/root/apps/detection/resources/configs/yolov5.json so-path=/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so qos=false ! ";
    pipeline << "queue name=q7 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "mux. ";
    pipeline << "mux. ! ";
    pipeline << "hailooverlay qos=false ! ";
    pipeline << "queue name=q8 leaky=downstream max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "hailoencodebin name=enc config-string='" << create_encoder_osd_config(m_resources).dump() << "' enforce-caps=false ! ";
    pipeline << "video/x-h264,framerate=30/1 ! ";
    pipeline << "queue name=q9 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "h264parse config-interval=-1 ! ";
    pipeline << "queue name=q10 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "rtph264pay config-interval=-1 ! ";
    pipeline << "application/x-rtp, media=(string)video, encoding-name=(string)H264 ! ";
    pipeline << "queue name=q11 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "appsink name=webtrc_appsink emit-signals=true max-buffers=0 ";
    std::string pipeline_str = pipeline.str();
    std::cout << "Pipeline: \n"
              << pipeline_str << std::endl;
    return pipeline_str;
}

Pipeline::Pipeline(WebserverResourceRepository resources)
    : IPipeline(resources)
{
    for (const auto &[key, val] : resources->get_all_types())
    {
        for (const auto &resource_type : val)
        {
            auto resource = resources->get(resource_type);
            if (resource == nullptr)
                continue;
            WEBSERVER_LOG_DEBUG("Pipeline: Subscribing to resource type: {}", resource_type);
            resource->subscribe_callback([this](ResourceStateChangeNotification notif)
                                         { this->callback_handle_strategy(notif); });
        }
    }
}

void Pipeline::callback_handle_strategy(ResourceStateChangeNotification notif)
{
    WEBSERVER_LOG_INFO("Pipeline: Handling resource state change notification of type: {}", notif.resource_type);
    switch (notif.resource_type)
    {
    case RESOURCE_FRONTEND:
    {
        GstElement *frontend = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
        std::shared_ptr<ValueResourceState<bool>> value_ptr = std::dynamic_pointer_cast<ValueResourceState<bool>>(notif.resource_state);
        if (value_ptr != nullptr)
        {
            WEBSERVER_LOG_DEBUG("Pipeline: Frontend freeze state changed");
            g_object_set(frontend, "freeze", value_ptr->value, NULL);
        }
        else
        {
            // Try to cast to ConfigResourceState
            std::shared_ptr<ConfigResourceState> config_ptr = std::dynamic_pointer_cast<ConfigResourceState>(notif.resource_state);
            if (config_ptr != nullptr)
            {
                WEBSERVER_LOG_DEBUG("Pipeline: Updating frontend config");
                auto fe_resource = std::static_pointer_cast<FrontendResource>(m_resources->get(RESOURCE_FRONTEND));
                std::string conf = fe_resource->get_frontend_config().dump();
                g_object_set(frontend, "config-string", conf.c_str(), NULL);
            }
        }
        gst_object_unref(frontend);
        break;
    }
    case RESOURCE_OSD:
    case RESOURCE_ENCODER:
    {
        WEBSERVER_LOG_DEBUG("Pipeline: Updating encoder-osd config");
        GstElement *encoder_element = gst_bin_get_by_name(GST_BIN(m_pipeline), "enc");
        auto enc_resource = std::static_pointer_cast<EncoderResource>(m_resources->get(RESOURCE_FRONTEND));
        enc_resource->apply_config(encoder_element);
        gst_object_unref(encoder_element);
        break;
    }
    case RESOURCE_AI:
    {
        auto state = std::static_pointer_cast<AiResource::AiResourceState>(notif.resource_state);

        if (state->enabled.empty() && state->disabled.empty())
        {
            WEBSERVER_LOG_DEBUG("Pipeline: No AI applications enabled or disabled");
            break;
        }

        GstElement *detection = gst_bin_get_by_name(GST_BIN(m_pipeline), "detection");
        if (std::find(state->disabled.begin(), state->disabled.end(), AiResource::AI_APPLICATION_DETECTION) != state->disabled.end()) // disable detection if it's in the disabled list
        {
            WEBSERVER_LOG_DEBUG("Pipeline: Disabling detection");
            g_object_set(detection, "pass-through", TRUE, NULL);
        }
        else if (std::find(state->enabled.begin(), state->enabled.end(), AiResource::AI_APPLICATION_DETECTION) != state->enabled.end()) // enable detection if it's in the enabled list
        {
            WEBSERVER_LOG_DEBUG("Pipeline: Enabling detection");
            g_object_set(detection, "pass-through", FALSE, NULL);
        }
        gst_object_unref(detection);
        break;
    }
    case RESOURCE_PRIVACY_MASK:
    {
        auto state = std::static_pointer_cast<PrivacyMaskResource::PrivacyMaskResourceState>(notif.resource_state);
        if (state->changed_to_enabled.empty() && state->changed_to_disabled.empty() && state->polygon_to_update.empty() && state->polygon_to_delete.empty())
        {
            break;
        }
        std::shared_ptr<PrivacyMaskResource> pm_resource = std::static_pointer_cast<PrivacyMaskResource>(m_resources->get(RESOURCE_PRIVACY_MASK));
        auto masks = pm_resource->get_privacy_masks();

        GstElement *frontend = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
        GValue val = G_VALUE_INIT;
        g_object_get_property(G_OBJECT(frontend), "privacy-mask", &val);
        void *value_ptr = g_value_get_pointer(&val);
        auto privacy_blender = (PrivacyMaskBlender *)value_ptr;

        for (std::string id : state->changed_to_enabled)
        {
            if (masks.find(id) != masks.end())
            {
                WEBSERVER_LOG_DEBUG("Pipeline: Adding privacy mask: {}", id);
                privacy_blender->add_privacy_mask(masks[id]);
            }
        }
        for (std::string id : state->changed_to_disabled)
        {
            if (masks.find(id) != masks.end())
            {
                WEBSERVER_LOG_DEBUG("Pipeline: Removing privacy mask: {}", id);
                privacy_blender->remove_privacy_mask(id);
            }
        }
        for (std::string &mask : state->polygon_to_update)
        {
            if (masks.find(mask) != masks.end())
            {
                WEBSERVER_LOG_DEBUG("Pipeline: Updating privacy mask: {}", mask);
                privacy_blender->set_privacy_mask(masks[mask]);
            }
        }
        for (std::string &mask : state->polygon_to_delete)
        {
            if (masks.find(mask) != masks.end())
            {
                WEBSERVER_LOG_DEBUG("Pipeline: Deleting privacy mask: {}", mask);
                privacy_blender->remove_privacy_mask(mask);
            }
        }
        break;
    }
    case RESOURCE_ISP:
    {
        auto isp_state = std::static_pointer_cast<IspResource::IspResourceState>(notif.resource_state);

        WEBSERVER_LOG_INFO("Pipeline: ISP state changed, updating frontend config and restarting frontendsrcbin");
        GstElement *frontend = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");

        // stop frontend
        WEBSERVER_LOG_DEBUG("Pipeline: Stopping frontend");
        if (GstStateChangeReturn stop_status = gst_element_set_state(frontend, GST_STATE_NULL); stop_status != GST_STATE_CHANGE_SUCCESS)
        {
            GST_ERROR_OBJECT(m_pipeline, "Failed to stop frontend");
            WEBSERVER_LOG_ERROR("Pipeline: Failed to stop frontend");
        }

        // update frontend config
        WEBSERVER_LOG_DEBUG("Pipeline: Updating frontend config");
        auto fe_resource = std::static_pointer_cast<FrontendResource>(m_resources->get(RESOURCE_FRONTEND));
        g_object_set(frontend, "config-string", fe_resource->get_frontend_config().dump().c_str(), NULL);

        // start frontend
        WEBSERVER_LOG_DEBUG("Pipeline: Starting frontend");
        GstStateChangeReturn start_status = gst_element_set_state(frontend, GST_STATE_PLAYING);
        if (start_status != GST_STATE_CHANGE_SUCCESS)
        {
            GST_ERROR_OBJECT(m_pipeline, "Failed to start frontend");
            WEBSERVER_LOG_ERROR("Pipeline: Failed to start frontend");
        }

        gst_object_unref(frontend);
        break;
    }
    default:
        break;
    }
}