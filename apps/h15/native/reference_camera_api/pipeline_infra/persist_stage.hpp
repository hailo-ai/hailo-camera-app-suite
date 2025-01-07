#pragma once

// Infra includes
#include "stage.hpp"
#include "buffer.hpp"
#include "queue.hpp"

// Tappas includes
#include "hailo_objects.hpp"
#include "hailo_common.hpp"

class PersistStage : public ConnectedStage
{
private:
    std::vector<HailoDetectionPtr> m_detections;
    size_t m_expiration_threshold;
    size_t m_count = 0;
public:
    PersistStage(std::string name, size_t expiration=5, size_t queue_size=5, bool leaky=false, bool print_fps=false) : 
        ConnectedStage(name, queue_size, leaky, print_fps), m_expiration_threshold(expiration) {}

    AppStatus process(BufferPtr data)
    {
        std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
        HailoROIPtr hailo_roi = data->get_roi();

        std::vector<HailoDetectionPtr> incoming_detections = hailo_common::get_hailo_detections(hailo_roi);
        if (incoming_detections.size() > 0)
        {
            m_detections = incoming_detections;
        }
        else if (m_detections.size() > 0)
        {
            hailo_common::add_detection_pointers(hailo_roi, m_detections);
            ++m_count;
            if (m_count >= m_expiration_threshold)
            {
                m_detections.clear();
                m_count = 0;
            }
        }
        
        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        if (m_print_fps)
        {
            std::cout << "Persist time = " << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() << "[microseconds]" << std::endl;
        }
        data->add_time_stamp(m_stage_name);
        set_duration(data);
        send_to_subscribers(data);

        return AppStatus::SUCCESS;
    }
};