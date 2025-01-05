import hailo
# import pyhailort
import numpy as np
import pickle
from pathlib import Path

import gi

gi.require_version('Gst', '1.0')

from gi.repository import Gst

parent_folder = Path(__file__).parent.resolve(
    strict=True)
raw_output_path = parent_folder / "raw_output"

raw_output = pickle.loads(raw_output_path.read_bytes())
layer_names = ["A", "B", "C"]
frame_id = 1

def run(buffer: Gst.Buffer, roi: hailo.HailoROI):
    global frame_id
    global layer_names
    global raw_output
    if frame_id < len(raw_output):
        # print(len(raw_output[frame_id]))
        raw_tensor1 = (raw_output[frame_id][0].squeeze() / 0.00392156).astype("uint8")
        raw_tensor2 = (raw_output[frame_id][1].squeeze() / 0.00392156).astype("uint8")
        raw_tensor3 = (raw_output[frame_id][2].squeeze() / 0.00392156).astype("uint8")
        tensor1 = hailo.HailoTensor(raw_tensor1, layer_names[0], 
                            raw_tensor1.shape[0], raw_tensor1.shape[1], raw_tensor1.shape[2],
                            0.0, 0.00392156)
        tensor2 = hailo.HailoTensor(raw_tensor2, layer_names[1], 
                            raw_tensor2.shape[0], raw_tensor2.shape[1], raw_tensor2.shape[2],
                            0.0, 0.00392156)
        tensor3 = hailo.HailoTensor(raw_tensor3, layer_names[2], 
                            raw_tensor3.shape[0], raw_tensor3.shape[1], raw_tensor3.shape[2],
                            0.0, 0.00392156)

        roi.add_tensor(tensor1)
        roi.add_tensor(tensor2)
        roi.add_tensor(tensor3)
    frame_id +=1
    return Gst.FlowReturn.OK
