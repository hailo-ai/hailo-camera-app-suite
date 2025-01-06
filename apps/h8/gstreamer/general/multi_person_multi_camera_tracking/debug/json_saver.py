import hailo
import numpy as np
import json
from pathlib import Path

import gi

gi.require_version('Gst', '1.0')

from gi.repository import Gst

HEIGHT=1080
WIDTH=1920

re_id_path = Path(__file__).parent.resolve(
    strict=True)
json_dir_path = re_id_path / "jsons"
json_dir_path.mkdir(exist_ok=True)

frame_id = 0

def run(buffer: Gst.Buffer, roi: hailo.HailoROI):
    global frame_id
    dets = []
    for det in hailo.get_hailo_detections(roi):
        bbox = det.get_bbox()
        json_det = {
        "xmin": bbox.xmin() * WIDTH,
        "ymin": bbox.ymin() * HEIGHT,
        "xmax":  bbox.xmax() * WIDTH,
        "ymax": bbox.ymax() * HEIGHT,
        "label": det.get_label(),
        "confidence": det.get_confidence()
        }
        unique_ids = det.get_objects_typed(hailo.HAILO_UNIQUE_ID)
        for uid in unique_ids:
            if uid.get_type() == hailo.TRACKING_ID:
                json_det["id"] = uid.get_id()
                break
        dets.append(json_det)

    json_path = json_dir_path / f"frame{frame_id}.json"
    json_path.write_text(json.dumps(dets))
    frame_id +=1
    return Gst.FlowReturn.OK
