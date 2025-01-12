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
result_path = re_id_path / "logits_list.npy"
json_dir_path.mkdir(exist_ok=True)

results = np.load(result_path, allow_pickle=True)
frame_id = 0
def run(buffer: Gst.Buffer, roi: hailo.HailoROI):
    global frame_id
    dets = []

    for det in hailo.get_hailo_detections(roi):
        bbox = det.get_bbox()
        json_det = [
        bbox.ymin(),
        bbox.xmin(),
        bbox.ymax() ,
        bbox.xmax(),
        det.get_label(),
        det.get_confidence()
        ]
        unique_ids = det.get_objects_typed(hailo.HAILO_UNIQUE_ID)
        for uid in unique_ids:
            if uid.get_type() == hailo.TRACKING_ID:
                json_det["id"] = uid.get_id()
                break
        dets.append(json_det)
    other = results[frame_id+1]
    dets_sholev = []
    for i in range(other['detection_boxes'].squeeze().shape[0]):
        if other['detection_boxes'].squeeze()[i].any():
            dets_sholev.append(f"{other['detection_boxes'].squeeze()[i]}, {other['detection_scores'].squeeze()[i]}, {other['detection_classes'].squeeze()[i]}") 
    print(dets)
    print(dets_sholev)

    # json_path = json_dir_path / f"frame{frame_id}.json"
    # json_path.write_text(json.dumps(dets))
    frame_id +=1
    return Gst.FlowReturn.OK
