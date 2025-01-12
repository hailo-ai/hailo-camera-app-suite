#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <mutex>
#include <shared_mutex>
#include <gst/gst.h>
#include "media_library/v4l2_ctrl.hpp"
#include "common/httplib/httplib_utils.hpp"
#include "common/isp/common.hpp"
#include "common/logger_macros.hpp"
#include "rtc/rtc.hpp"
#include "osd.hpp"

#ifndef MEDIALIB_LOCAL_SERVER
#include "media_library/gyro_device.hpp"
#include "media_library/privacy_mask_types.hpp"
#include "media_library/privacy_mask.hpp"
using namespace privacy_mask_types;
#else
struct vertex
{
    uint x, y;

    vertex(uint x, uint y) : x(x), y(y) {}
};
struct polygon
{
    std::string id;
    std::vector<vertex> vertices;
};
class PrivacyMaskBlender
{
public:
    void add_privacy_mask(polygon mask) {}
    void remove_privacy_mask(std::string id) {}
};
#endif
using namespace webserver::common;

namespace webserver
{
    namespace resources
    {
        enum ResourceType
        {
            RESOURCE_WEBPAGE,
            RESOURCE_CONFIG_MANAGER,
            RESOURCE_FRONTEND,
            RESOURCE_ENCODER,
            RESOURCE_ENCODER_RESET,
            RESOURCE_OSD,
            RESOURCE_AI,
            RESOURCE_ISP,
            RESOURCE_PRIVACY_MASK,
            RESOURCE_WEBRTC,
            STREAM_CONFIG,
            RESTART_STREAM,
        };

        enum ResourceBehaviorType
        {
            RESOURCE_BEHAVIOR_CONFIG,
            RESOURCE_BEHAVIOR_FUNCTIONAL
        };

        NLOHMANN_JSON_SERIALIZE_ENUM(ResourceType, {{RESOURCE_WEBPAGE, "webpage"},
                                                    {RESOURCE_FRONTEND, "frontend"},
                                                    {RESOURCE_ENCODER, "encoder"},
                                                    {RESOURCE_ENCODER_RESET, "encoder_reset"},
                                                    {RESOURCE_OSD, "osd"},
                                                    {RESOURCE_AI, "ai"},
                                                    {RESOURCE_ISP, "isp"},
                                                    {RESOURCE_PRIVACY_MASK, "privacy_mask"},
                                                    {RESOURCE_CONFIG_MANAGER, "config"},
                                                    {RESOURCE_WEBRTC, "webrtc"},
                                                    {STREAM_CONFIG, "stream_config"},
                                                    {RESTART_STREAM, "restart_stream"}})

        NLOHMANN_JSON_SERIALIZE_ENUM(ResourceBehaviorType, {
                                                               {RESOURCE_BEHAVIOR_CONFIG, "config"},
                                                               {RESOURCE_BEHAVIOR_FUNCTIONAL, "functional"},
                                                           })
        class ResourceState
        {
        public:
            virtual ~ResourceState() {}
        };

#define VALUE_RESOURCE_NAME_FRONTEND_FREEZE "freeze"

        template <typename T>
        class ValueResourceState : public ResourceState
        {
        public:
            std::string name;
            T value;
            ValueResourceState(std::string name, T value) : name(name), value(value) {}
        };

        class ResourceStateChangeNotification
        {
        public:
            ResourceType resource_type;
            std::shared_ptr<ResourceState> resource_state;
        };

        class ConfigResourceState : public ResourceState
        {
        public:
            std::string config;
            ConfigResourceState(std::string config) : config(config) {}
        };

        class StreamConfigResourceState : public ResourceState
        {
            public:
                enum rotation_t
                {
                    ROTATION_0 = 0,
                    ROTATION_90 = 90,
                    ROTATION_180 = 180,
                    ROTATION_270 = 270
                };
                struct Resolution
                {
                    uint32_t width;
                    uint32_t height;
                    uint32_t framerate;
                    bool framerate_changed;
                    bool stream_size_changed;
                };

                rotation_t rotation;
                bool rotate_enabled;
                std::vector<Resolution> resolutions;

                StreamConfigResourceState() = default;
                StreamConfigResourceState(std::vector<Resolution> resolutions, std::string rotation, bool rotate_enabled) : rotate_enabled(rotate_enabled),resolutions(resolutions)
                {
                    // this->resolutions = resolutions;
                    if (rotation == "ROTATION_ANGLE_0")
                    {
                        this->rotation = ROTATION_0;
                    }
                    else if (rotation == "ROTATION_ANGLE_90")
                    {
                        this->rotation = ROTATION_90;
                    }
                    else if (rotation == "ROTATION_ANGLE_180")
                    {
                        this->rotation = ROTATION_180;
                    }
                    else if (rotation == "ROTATION_ANGLE_270")
                    {
                        this->rotation = ROTATION_270;
                    }
                    else
                        throw std::invalid_argument("Invalid rotation angle " + rotation);
                }
        };


        typedef std::function<void(ResourceStateChangeNotification)> ResourceChangeCallback;

        class Resource
        {
        protected:
            std::string m_default_config;
            nlohmann::json m_config;
            std::vector<ResourceChangeCallback> m_callbacks;

        public:
            Resource() = default;
            virtual ~Resource() = default;
            virtual std::string name() = 0;
            virtual ResourceType get_type() = 0;
            virtual void http_register(std::shared_ptr<HTTPServer> srv) = 0;

            virtual std::string to_string()
            {
                return m_config.dump();
            }

            virtual nlohmann::json get()
            {
                return m_config;
            }

            virtual ResourceBehaviorType get_behavior_type()
            {
                return RESOURCE_BEHAVIOR_CONFIG; // default
            }

            virtual void on_resource_change(std::shared_ptr<ResourceState> state)
            {
                for (auto &callback : m_callbacks)
                {
                    callback({get_type(), state});
                }
            }

            virtual void on_resource_change(ResourceType type, std::shared_ptr<ResourceState> state)
            {
                for (auto &callback : m_callbacks)
                {
                    callback({type, state});
                }
            }

            void subscribe_callback(ResourceChangeCallback callback)
            {
                m_callbacks.push_back(callback);
            }
        };

        class ConfigResource : public Resource
        {
        private:
            nlohmann::json m_frontend_default_config;
            nlohmann::json m_encoder_osd_default_config;

        public:
            ConfigResource();
            ~ConfigResource() = default;
            std::string name() override { return "config"; }
            ResourceType get_type() override { return RESOURCE_CONFIG_MANAGER; }
            void http_register(std::shared_ptr<HTTPServer> srv) override {}
            nlohmann::json get_frontend_default_config();
            nlohmann::json get_encoder_default_config();
            nlohmann::json get_osd_default_config();
            nlohmann::json get_hdr_default_config();
            nlohmann::json get_denoise_default_config();
        };

        class WebpageResource : public Resource
        {
        public:
            WebpageResource() = default;
            void http_register(std::shared_ptr<HTTPServer> srv) override;
            std::string name() override { return "webpage"; }
            ResourceType get_type() override { return RESOURCE_WEBPAGE; }
        };

        class AiResource : public Resource
        {
        public:
            enum AiApplications
            {
                AI_APPLICATION_DETECTION,
                AI_APPLICATION_DENOISE,
            };

            class AiResourceState : public ResourceState
            {
            public:
                std::vector<AiApplications> enabled;
                std::vector<AiApplications> disabled;
            };

            AiResource(std::shared_ptr<webserver::resources::ConfigResource> configs);
            void http_register(std::shared_ptr<HTTPServer> srv) override;
            std::string name() override { return "ai"; }
            ResourceType get_type() override { return RESOURCE_AI; }
            nlohmann::json get_ai_config(AiApplications app);
            std::vector<AiApplications> get_enabled_applications();
            void set_detection_enabled(bool enabled);

        private:
            void http_patch(nlohmann::json body);
            std::shared_ptr<AiResourceState> parse_state(std::vector<AiApplications> current_enabled, std::vector<AiApplications> prev_enabled);
            nlohmann::json m_denoise_config;
            std::mutex m_mutex;
        };

        class OsdResource : public Resource
        {
        public:
            template <typename T>
            class OsdResourceConfig
            {
                static_assert(std::is_base_of<osd::Overlay, T>::value, 
                  "T must be derived from BaseTextOverlay");
                private:
                    bool enabled;
                    T overlay;
                public:
                    OsdResourceConfig(bool enabled, nlohmann::json params) : enabled(enabled){
                        osd::from_json(params, overlay);
                    }
                    T get_overlay() { return overlay; }
                    bool get_enabled() { return enabled; }
            };

            class OsdResourceState : public ResourceState
            {
                public:
                    std::vector<std::string> overlays_to_delete;
                    std::vector<OsdResourceConfig<osd::TextOverlay>> text_overlays;
                    std::vector<OsdResourceConfig<osd::ImageOverlay>> image_overlays;
                    std::vector<OsdResourceConfig<osd::DateTimeOverlay>> datetime_overlays;
                    std::vector<OsdResourceConfig<osd::AutoFocusOverlay>> autofocus_overlays;
                    OsdResourceState(nlohmann::json config, std::vector<std::string> overlays_ids);
            };
            OsdResource();
            void http_register(std::shared_ptr<HTTPServer> srv) override;
            std::string name() override { return "osd"; }
            ResourceType get_type() override { return RESOURCE_OSD; }
            nlohmann::json get_encoder_osd_config();
            std::vector<std::string> get_overlays_to_delete(nlohmann::json previouse_config, nlohmann::json new_config);
            nlohmann::json map_paths(nlohmann::json config);
            nlohmann::json unmap_paths(nlohmann::json config);

        };

        class IspResource : public Resource
        {
        private:
            std::mutex m_mutex;
            std::unique_ptr<isp_utils::ctrl::v4l2Control> m_v4l2;
            std::shared_ptr<AiResource> m_ai_resource;
            stream_isp_params_t m_baseline_stream_params;
            int16_t m_baseline_wdr_params;
            backlight_filter_t m_baseline_backlight_params;
            nlohmann::json m_hdr_config;
            auto_exposure_t get_auto_exposure();
            nlohmann::json set_auto_exposure(const nlohmann::json &req);
            bool set_auto_exposure(auto_exposure_t &ae);
            void on_ai_state_change(std::shared_ptr<AiResource::AiResourceState> state);
            void set_tuning_profile(webserver::common::tuning_profile_t);

        public:
            class IspResourceState : public ResourceState
            {
            public:
                bool isp_3aconfig_updated;
                IspResourceState(bool isp_3aconfig_updated) : isp_3aconfig_updated(isp_3aconfig_updated) {}
            };

            nlohmann::json get_hdr_config() { return m_hdr_config; }
            IspResource(std::shared_ptr<AiResource> ai_res, std::shared_ptr<webserver::resources::ConfigResource> configs, std::shared_ptr<OsdResource> osd_res);
            void http_register(std::shared_ptr<HTTPServer> srv) override;
            std::string name() override { return "isp"; }
            ResourceType get_type() override { return RESOURCE_ISP; }
            ResourceBehaviorType get_behavior_type() override { return RESOURCE_BEHAVIOR_FUNCTIONAL; }
            void init(bool set_auto_wb = true);
        };

        class FrontendResource : public Resource
        {
        public:
            struct Resolution
            {
                uint32_t width;
                uint32_t height;
                uint32_t framerate;
            };  
            struct frontend_config_t
            {
                bool freeze;
                bool freeze_state_changed;
                bool rotate_enabled;
                std::string rotate;
                std::vector<Resolution> resolutions;
            };

            class FrontendResourceState : public ResourceState
            {
            public:
                std::string config;
                frontend_config_t control;
                FrontendResourceState(std::string config, frontend_config_t control) : config(config), control(control) {}
            };

            FrontendResource(std::shared_ptr<webserver::resources::AiResource> ai_res, std::shared_ptr<webserver::resources::IspResource> isp_res, std::shared_ptr<webserver::resources::ConfigResource> configs);
            void http_register(std::shared_ptr<HTTPServer> srv) override;
            std::string name() override { return "frontend"; }
            ResourceType get_type() override { return RESOURCE_FRONTEND; }
            nlohmann::json get_frontend_config();

        private:
            void update_frontend_config();
            std::shared_ptr<webserver::resources::AiResource> m_ai_resource;
            std::shared_ptr<webserver::resources::IspResource> m_isp_resource;
            frontend_config_t m_frontend_config;
        };

        class EncoderResource : public Resource
        {
        public:
            enum bitrate_control_t
            {
                VBR,
                CVBR,
            };

            struct encoder_control_t
            {
                int bitrate;
                bitrate_control_t rc_mode;
                uint32_t width;
                uint32_t height;
                uint32_t framerate;

                bitrate_control_t stringToEnum(const std::string& str);
                std::string enumToString(bitrate_control_t bitrate) const;
            };

            class EncoderResourceState : public ResourceState
            {
            public:
                encoder_control_t control;
                EncoderResourceState(encoder_control_t control) : control(control) {}
            };

            EncoderResource(std::shared_ptr<webserver::resources::ConfigResource> configs, std::shared_ptr<webserver::resources::FrontendResource> frontend_res);
            void http_register(std::shared_ptr<HTTPServer> srv) override;
            std::string name() override { return "encoder"; }
            ResourceType get_type() override { return RESOURCE_ENCODER; }
            void apply_config(GstElement *encoder_element);

        private:
            encoder_control_t m_encoder_control;
            void set_encoder_control(encoder_control_t &encoder_control);
        };

        NLOHMANN_JSON_SERIALIZE_ENUM(EncoderResource::bitrate_control_t, {{webserver::resources::EncoderResource::VBR, "VBR"},
                                                                          {webserver::resources::EncoderResource::CVBR, "CVBR"}})
        void to_json(nlohmann::json &j, const EncoderResource::encoder_control_t &b);
        void from_json(const nlohmann::json &j, EncoderResource::encoder_control_t &b);


        class PrivacyMaskResource : public Resource
        {
        public:
            class PrivacyMaskResourceState : public ResourceState
            {
            public:
                std::vector<std::string> changed_to_enabled;
                std::vector<std::string> changed_to_disabled;
                std::vector<std::string> polygon_to_update;
                std::vector<std::string> polygon_to_delete;
            };

            void renable_masks();
        private:
            std::map<std::string, polygon> m_privacy_masks;
            StreamConfigResourceState::rotation_t m_rotation;
            std::shared_ptr<PrivacyMaskResourceState> parse_state(std::vector<std::string> current_enabled, std::vector<std::string> prev_enabled, nlohmann::json diff);
            std::shared_ptr<PrivacyMaskResourceState> update_all_vertices_state();
            std::vector<std::string> get_enabled_masks();
            void parse_polygon(nlohmann::json j);
            std::shared_ptr<webserver::resources::PrivacyMaskResource::PrivacyMaskResourceState> delete_masks_from_config(nlohmann::json config);
            vertex point_rotation(vertex &point, uint32_t width, uint32_t height, StreamConfigResourceState::rotation_t from_rotation, StreamConfigResourceState::rotation_t to_rotation);

        public:
            PrivacyMaskResource(std::shared_ptr<webserver::resources::FrontendResource> frontend_res);
            void http_register(std::shared_ptr<HTTPServer> srv) override;
            std::string name() override { return "privacy_mask"; }
            ResourceType get_type() override { return RESOURCE_PRIVACY_MASK; }
            ResourceBehaviorType get_behavior_type() override { return RESOURCE_BEHAVIOR_CONFIG; }
            std::map<std::string, polygon> get_privacy_masks() { return m_privacy_masks; }
        };

        class WebRtcResource : public Resource
        {
        private:
            struct WebrtcSession
            {
                std::shared_ptr<rtc::PeerConnection> peer_connection;
                std::shared_ptr<rtc::Track> track;
                rtc::PeerConnection::State state;
                rtc::PeerConnection::GatheringState gathering_state;
                rtc::SSRC ssrc;
                std::string codec;
                nlohmann::json ICE_offer;
            };
            std::vector<std::shared_ptr<WebrtcSession>> m_sessions;
            std::shared_mutex m_session_mutex;
            const std::map<std::string, int> codec_payload_type_map = {
                {"H264", 96},
                {"H265", 98},
            };
            std::shared_ptr<WebrtcSession> create_media_sender();

        public:
            WebRtcResource();
            void send_rtp_packet(GstSample *sample);
            void remove_inactive_sessions();
            void close_all_connections();
            void http_register(std::shared_ptr<HTTPServer> srv) override;
            std::string name() override { return "webrtc"; }
            ResourceType get_type() override { return RESOURCE_WEBRTC; }
        };

    }
}

typedef std::shared_ptr<webserver::resources::Resource> WebserverResource;
