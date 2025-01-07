import pickle
from pathlib import Path

import hailo

# Importing VideoFrame before importing GST is must
from gsthailo import VideoFrame
from gi.repository import Gst


pickle_version = 2
dest_file = 'data.pickled'
rois_list = list()


def run(video_frame: VideoFrame):
    pickled_data = pickle.dumps(video_frame.roi, pickle_version)
    rois_list.append(pickled_data)

    return Gst.FlowReturn.OK


def finalize():
    dumped = pickle.dumps(rois_list, pickle_version)
    Path(dest_file).write_bytes(dumped)
    print("Dumped ROI's succesfully")

    return Gst.FlowReturn.OK
