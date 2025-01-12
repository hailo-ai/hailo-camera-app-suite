#pragma once

// General includes
#include <algorithm>

// Media-Library includes
#include "media_library/encoder.hpp"

// Tappas includes
#include "hailo_common.hpp"

// Infra includes
#include "stage.hpp"
#include "buffer.hpp"
#include "udp_module.hpp"

class UdpStage : public ConnectedStage
{
private:
    std::string m_host;
    std::string m_port;
    EncodingType m_type;
    UdpModulePtr m_udp;
    
public:
    UdpStage(std::string name, size_t queue_size=1, bool leaky=false, bool print_fps=false) : 
        ConnectedStage(name, queue_size, leaky, print_fps)
    {
    }

    AppStatus create(std::string host, std::string port, EncodingType type)
    {
        if (m_udp == nullptr)
        {
            tl::expected<UdpModulePtr, AppStatus> udp_expected = UdpModule::create(m_stage_name, host, port, type);
            if (!udp_expected.has_value())
            {
                std::cout << "Failed to create udp" << std::endl;
                return AppStatus::CONFIGURATION_ERROR;
            }
            m_udp = udp_expected.value();
            m_host = host;
            m_port = port;
            m_type = type;
        }
        return AppStatus::SUCCESS;
    }

    AppStatus init() override
    {
        if (m_udp == nullptr)
        {
            std::cerr << "Udp " << m_stage_name << " not configured. Call configure()" << std::endl;
            return AppStatus::UNINITIALIZED;
        }
        m_udp->start();
        return AppStatus::SUCCESS;
    }

    AppStatus deinit() override
    {
        m_udp->stop();
        return AppStatus::SUCCESS;
    }

    AppStatus configure(std::string host, std::string port, EncodingType type)
    {
        if (m_udp == nullptr)
        {
            return create(host, port, type);
        }
        m_udp->stop();
        m_udp = nullptr;
        return create(host, port, type);
    }

    AppStatus process(BufferPtr data)
    {
        if (m_udp == nullptr)
        {
            std::cerr << "Udp " << m_stage_name << " not configured. Call configure()" << std::endl;
            return AppStatus::UNINITIALIZED;
        }

        std::vector<MetadataPtr> metadata = data->get_metadata_of_type(MetadataType::SIZE);
        if (metadata.size() <= 0)
        {
            std::cerr << "Udp " << m_stage_name << " got buffer of unknown size, add SizeMeta" << std::endl;
            return AppStatus::PIPELINE_ERROR;
        }
        SizeMetadataPtr size_metadata = std::dynamic_pointer_cast<SizeMetadata>(metadata[0]);
        size_t size = size_metadata->get_size();
        m_udp->add_buffer(data->get_buffer(), size);

        return AppStatus::SUCCESS;
    }
};
