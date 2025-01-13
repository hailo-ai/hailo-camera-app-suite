from itertools import count
import pickle
from pathlib import Path

import hailo

# Importing VideoFrame before importing GST is must
from gsthailo import VideoFrame
from gi.repository import Gst


dest_file = Path('data.pickled')
counter = count()

if not dest_file.is_file():
    raise RuntimeError(f'{dest_file} not found, please run record.sh before running inject.sh')

data_loaded = pickle.loads(dest_file.read_bytes())
hailo_rois = [pickle.loads(hailo_object) for hailo_object in data_loaded]


def run(video_frame: VideoFrame):
    loaded_roi = hailo_rois[next(counter)]

    for hailo_obj in loaded_roi.get_objects():
        video_frame.roi.add_object(hailo_obj)

    return Gst.FlowReturn.OK
