import hailo
import numpy as np
import json
from pathlib import Path

import gi

gi.require_version('Gst', '1.0')

from gi.repository import Gst

re_id_path = Path(__file__).parent.resolve(strict=True)
npy_dir_path = re_id_path / "numpy"
npy_dir_path.mkdir(exist_ok=True)

frame_id = 0
def run(buffer: Gst.Buffer, roi: hailo.HailoROI):
    global frame_id
    for tensor in roi.get_tensors():
        tensor_name = tensor.name().split('/')[1]
        net_result = np.array(tensor, copy=False)
        npy_path = npy_dir_path / f"frame{frame_id}_{tensor_name}"
        np.save(npy_path, net_result)
    frame_id +=1
    return Gst.FlowReturn.OK
