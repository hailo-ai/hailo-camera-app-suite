#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <iostream>
#include <cxxopts/cxxopts.hpp>
#include "apps_common.hpp"

#define BIG_GOP (150)
#define MEDIUM_GOP (30)
#define SMALL_GOP (5)

static int counter=0;

/**
 * Encoder's probe callback
 *
 * @param[in] pad               The sinkpad of the encoder.
 * @param[in] info              Info about the probe
 * @param[in] user_data         user specified data for the probe
 * @return GST_PAD_PROBE_OK
 * @note Example only - Switches between GOP length's in 600 frame cycle:
 * 300 frames in gop 150
 * 200 frames in gop 30
 * 100 frames in gop 5
 */
static GstPadProbeReturn encoder_probe_callback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    GstElement *pipeline = GST_ELEMENT(user_data);
    GstElement *encoder_element = gst_bin_get_by_name(GST_BIN(pipeline), "enco");

    counter++;

    if (counter % 600 == 0) {
        // Changing to big GOP
        GST_INFO("Changing encoder to GOP %d", BIG_GOP);
        g_object_set(encoder_element, "intra-pic-rate", BIG_GOP, NULL);
        g_object_set(encoder_element, "gop-length", BIG_GOP, NULL);
    }
    else if (counter % 600 == 300) {
        // Changing to MEDIUM_GOP
        GST_INFO("Changing encoder to GOP %d", MEDIUM_GOP);
        g_object_set(encoder_element, "intra-pic-rate", MEDIUM_GOP, NULL);
        g_object_set(encoder_element, "gop-length", MEDIUM_GOP, NULL);
    }
    else if (counter % 600 == 500) {
        // Changing to SMALL_GOP
        GST_INFO("Changing encoder to GOP %d", SMALL_GOP);
        g_object_set(encoder_element, "intra-pic-rate", SMALL_GOP, NULL);
        g_object_set(encoder_element, "gop-length", SMALL_GOP, NULL);
    }

    gst_object_unref(encoder_element);
    return GST_PAD_PROBE_OK;
}

/**
 * Appsink's new_sample callback
 *
 * @param[in] appsink               The appsink object.
 * @param[in] callback_data         user data.
 * @return GST_FLOW_OK
 * @note Example only - only mapping the buffer to a GstMapInfo, than unmapping.
 */
static GstFlowReturn appsink_new_sample(GstAppSink * appsink, gpointer callback_data)
{
    GstSample *sample;
    GstBuffer *buffer;
    GstMapInfo mapinfo;

    sample = gst_app_sink_pull_sample(appsink);
    buffer = gst_sample_get_buffer(sample);
    gst_buffer_map(buffer, &mapinfo, GST_MAP_READ);

    GST_INFO_OBJECT(appsink, "Got Buffer from appsink: %p", mapinfo.data);
    // Do Logic

    gst_buffer_unmap(buffer,&mapinfo);
    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

/**
 * Create the gstreamer pipeline as string
 *
 * @return A string containing the gstreamer pipeline.
 * @note prints the return value to the stdout.
 */
std::string create_pipeline_string(std::string codec)
{
    std::string pipeline = "";
    std::string encoder_arguments;

    encoder_arguments = "intra-pic-rate=" + std::to_string(BIG_GOP) + " "
                        "gop-length=" + std::to_string(BIG_GOP);
                        

    pipeline = "v4l2src name=src_element num-buffers=2000 device=/dev/video0 io-mode=dmabuf ! "
               "video/x-raw,format=NV12,width=1920,height=1080, framerate=30/1 ! "
               "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
               "hailo" + codec + "enc name=enco " + encoder_arguments + " ! " + codec + "parse config-interval=-1 ! "
               "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
               "video/x-" + codec + ",framerate=30/1 ! "
               "fpsdisplaysink fps-update-interval=2000 name=display_sink text-overlay=false video-sink=\"appsink name=hailo_sink\" sync=true signal-fps-measurements=true";
                                           
    std::cout << "Pipeline:" << std::endl;
    std::cout << "gst-launch-1.0 " << pipeline << std::endl;

    return pipeline;
}

/**
 * Set the Appsink callbacks
 *
 * @param[in] pipeline        The pipeline as a GstElement.
 * @param[in] print_fps       To print FPS or not.
 * @note Sets the new_sample and propose_allocation callbacks, without callback user data (NULL).
 */
void set_callbacks(GstElement *pipeline, bool print_fps)
{
    GstAppSinkCallbacks callbacks={NULL};

    GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline), "hailo_sink");
    callbacks.new_sample = appsink_new_sample;

    gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, NULL, NULL);
    gst_object_unref(appsink);

    if (print_fps)
    {
        GstElement *display_sink = gst_bin_get_by_name(GST_BIN(pipeline), "display_sink");
        g_signal_connect(display_sink, "fps-measurements", G_CALLBACK(fps_measurements_callback), NULL);
    }
}

/**
 * Set the pipeline's probes
 *
 * @param[in] pipeline        The pipeline as a GstElement.
 * @note Sets a probe to the encoder sinkpad.
 */
void set_probes(GstElement *pipeline)
{
    // extract elements from pipeline
    GstElement *encoder = gst_bin_get_by_name(GST_BIN(pipeline), "enco");
    // extract pads from elements
    GstPad *pad_encoder = gst_element_get_static_pad(encoder, "sink");
    // set probes
    gst_pad_add_probe(pad_encoder, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)encoder_probe_callback, pipeline, NULL);
    // free resources
    gst_object_unref(encoder);    
}

int main(int argc, char *argv[])
{
    std::string src_pipeline_string;
    GstFlowReturn ret;
    add_sigint_handler();
    std::string codec;
    bool print_fps = false;

    // Parse user arguments
    cxxopts::Options options = build_arg_parser();
    auto result = options.parse(argc, argv);
    std::vector<ArgumentType> argument_handling_results = handle_arguments(result, options, codec);

    for (ArgumentType argument: argument_handling_results)
    {
        switch (argument) {
            case ArgumentType::Help:
                return 0;
            case ArgumentType::Codec:
                break;
            case ArgumentType::PrintFPS:
                print_fps = true;
                break;
            case ArgumentType::Error:
                return 1;
        }
    }

    gst_init(&argc, &argv);

    std::string pipeline_string = create_pipeline_string(codec);
    GstElement *pipeline = gst_parse_launch(pipeline_string.c_str(), NULL);
    set_callbacks(pipeline, print_fps);
    set_probes(pipeline);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    ret = wait_for_end_of_pipeline(pipeline);

    // Free resources
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    gst_deinit();

    return ret;
}
