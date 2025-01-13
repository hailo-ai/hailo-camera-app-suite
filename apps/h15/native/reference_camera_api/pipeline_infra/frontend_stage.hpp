#pragma once

// General includes
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

// Media-Library includes
#include "media_library/frontend.hpp"

// Tappas includes
#include "hailo_common.hpp"

// Infra includes
#include "media_library/media_library_types.hpp"
#include "stage.hpp"
#include "buffer.hpp"

class FrontendStage : public ConnectedStage
{
private:
    MediaLibraryFrontendPtr m_frontend;
    std::map<output_stream_id_t, std::vector<ConnectedStagePtr>> m_stream_subscribers;
    std::mutex m_running_mutex;
    std::condition_variable m_running_cv;
    
public:
    FrontendStage(std::string name, size_t queue_size=1, bool leaky=false, bool print_fps=false) : 
        ConnectedStage(name, queue_size, leaky, print_fps)
    {
        m_frontend = nullptr;
        m_stream_subscribers.clear();
    }

    AppStatus create(std::string config_string)
    {
        tl::expected<MediaLibraryFrontendPtr, media_library_return> frontend_expected = MediaLibraryFrontend::create();
        if (!frontend_expected.has_value())
        {
            std::cerr << "Failed to create frontend" << std::endl;
            return AppStatus::CONFIGURATION_ERROR;
        }
        m_frontend = frontend_expected.value();
        if (m_frontend->set_config(config_string) != MEDIA_LIBRARY_SUCCESS)
        {
            std::cerr << "Failed to create frontend" << std::endl;
            return AppStatus::CONFIGURATION_ERROR;
        }
        return subscribe_output_streams();
    }

    // Note subscription is done by stream id as forntend has multiple output streams
    void subscribe_to_stream(output_stream_id_t stream_id, ConnectedStagePtr subscriber)
    {
        m_stream_subscribers[stream_id].push_back(subscriber);
        subscriber->add_queue(stream_id);
    }

    AppStatus subscribe_output_streams()
    {
        if (m_frontend == nullptr)
        {
            std::cerr << "Frontend " << m_stage_name << " not configured. Call configure()" << std::endl;
            return AppStatus::UNINITIALIZED;
        }
        // Get frontend output streams
        auto streams = m_frontend->get_outputs_streams();
        // Subscribe to frontend
        FrontendCallbacksMap fe_callbacks;
        if (!streams.has_value())
        {
            std::cout << "Failed to get stream ids" << std::endl;
            throw std::runtime_error("Failed to get stream ids");
        }
        for (auto s : streams.value())
        {
            std::cout << "subscribing to frontend for '" << s.id << "'" << std::endl;
            fe_callbacks[s.id] = [s, this](HailoMediaLibraryBufferPtr buffer, size_t size)
            {
                BufferPtr wrapped_buffer = std::make_shared<Buffer>(buffer);
                for (auto &subscriber : m_stream_subscribers[s.id])
                {
                    subscriber->push(wrapped_buffer, s.id);
                }
            };
        }
        m_frontend->subscribe(fe_callbacks);
        return AppStatus::SUCCESS;
    }

    virtual AppStatus stop() override
    {
        set_end_of_stream(true);
        m_running_cv.notify_one();
        m_thread.join();
        return AppStatus::SUCCESS;
    }

    AppStatus init() override
    {
        if (m_frontend == nullptr)
        {
            std::cerr << "Frontend " << m_stage_name << " not configured. Call configure()" << std::endl;
            return AppStatus::UNINITIALIZED;
        }
        m_frontend->start();
        return AppStatus::SUCCESS;
    }

    AppStatus deinit() override
    {
        m_frontend->stop();
        return AppStatus::SUCCESS;
    }

    AppStatus configure(std::string config_string)
    {
        if (m_frontend == nullptr)
        {
            return create(config_string);
        }
        m_frontend->stop();
        m_frontend = nullptr;
        return create(config_string);
    }

    void loop() override
    {
        init();
        std::unique_lock<std::mutex> lock(m_running_mutex);
        m_running_cv.wait(lock, [this] { return m_end_of_stream; });
        deinit();
    }

    tl::expected<std::vector<frontend_output_stream_t>, media_library_return> get_outputs_streams()
    {
        return m_frontend->get_outputs_streams();
    }
};
