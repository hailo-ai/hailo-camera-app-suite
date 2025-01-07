#pragma once

// general includes
#include <queue>
#include <thread>
#include <vector>

// infra includes
#include "stage.hpp"

enum class StageType
{
    GENERAL = 0,
    SOURCE,
    SINK
};

class Pipeline
{
private:
    std::vector<StagePtr> m_stages;      // All stages, used for full queries (get and print)
    std::vector<StagePtr> m_gen_stages;  // For general type stages
    std::vector<StagePtr> m_src_stages;  // For source type stages
    std::vector<StagePtr> m_sink_stages; // For sink type stages

public:

    void add_stage(StagePtr stage, StageType type=StageType::GENERAL)
    {
        switch (type)
        {
        case StageType::SOURCE:
            m_src_stages.push_back(stage);
            break;
        case StageType::SINK:
            m_sink_stages.push_back(stage);
            break;
        default:
            m_gen_stages.push_back(stage);
        }
        m_stages.push_back(stage);
    }

    void start_pipeline()
    {
        // Start the sink stages
        for (auto &stage : m_sink_stages)
        {
            stage->start();
        }

        // Start the general stages
        for (auto &stage : m_gen_stages)
        {
            stage->start();
        }

        // Start the source stages
        for (auto &stage : m_src_stages)
        {
            stage->start();
        }
    }

    void stop_pipeline()
    {
        // Stop the source stages
        for (auto &stage : m_src_stages)
        {
            stage->stop();
        }

        // Stop the general stages
        for (auto &stage : m_gen_stages)
        {
            stage->stop();
        }

        // Stop the sink stages
        for (auto &stage : m_sink_stages)
        {
            stage->stop();
        }
    }

    StagePtr get_stage_by_name(std::string stage_name)
    {
        for (auto &stage : m_stages)
        {
            if (stage->get_name() == stage_name)
            {
                return stage;
            }
        }
        return nullptr;
    }

    void print_latency()
    {
        std::cout << "Stage latencies ";
        for (auto &stage : m_stages)
        {
            std::cout << stage->get_name() << " : " << stage->get_duration().count() << "  ";
        }
        std::cout << std::endl;
    }
};
using PipelinePtr = std::shared_ptr<Pipeline>;