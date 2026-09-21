"""Bounded camera recording only. Does not open the wheel serial port."""
import argparse, datetime, hashlib, json, pathlib, sys, time, urllib.parse

parser = argparse.ArgumentParser()
parser.add_argument("--seconds", type=float, default=5.0)
parser.add_argument("--name", default="live_check")
parser.add_argument("--runtime-dir", type=pathlib.Path)
parser.add_argument("--output-dir", type=pathlib.Path)
args = parser.parse_args()
if not 1 <= args.seconds <= 60 or not args.name.replace("_", "").isalnum():
    raise SystemExit("Use 1-60 seconds and an alphanumeric recording name.")
root = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(args.runtime_dir or root / "camera_runtime"))
import av
av.logging.set_level(av.logging.PANIC)
dest = args.output_dir or root / "camera_evidence"
dest.mkdir(parents=True, exist_ok=True)
video_path = dest / (args.name + ".mkv")
if video_path.exists():
    raise SystemExit("Recording name already exists.")
metadata = {"start_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "requested_seconds": args.seconds, "camera": "192.168.0.182",
            "av_version": av.__version__, "status": "starting"}
source = output = None
try:
    password = pathlib.Path(r"C:\Users\Mill\Desktop\pass.txt").read_text(encoding="utf-8-sig").strip()
    url = "rtsp://admin:" + urllib.parse.quote(password, safe="") + "@192.168.0.182:554/cam/realmonitor?channel=1&subtype=0"
    source = av.open(url, options={"rtsp_transport": "tcp"}, timeout=(5.0, 3.0))
    del url, password
    streams = [s for s in source.streams if s.type in ("video", "audio")]
    metadata["streams"] = []
    output = av.open(str(video_path), mode="w")
    mapping = {}
    for s in streams:
        mapping[s.index] = output.add_stream_from_template(s)
        entry = {"index": s.index, "type": s.type, "codec": s.codec_context.name}
        if s.type == "video":
            entry.update(width=s.codec_context.width, height=s.codec_context.height,
                         average_rate=str(s.average_rate))
        else:
            entry.update(sample_rate=s.codec_context.sample_rate)
        metadata["streams"].append(entry)
    started = time.monotonic()
    metadata["input_opened_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    ready_at = None
    key_packet_seen = False
    decoded_frames = 0
    last_video_time = None
    last_video_at = None
    heartbeat_at = 0.0
    counts = {str(s.index): 0 for s in streams}
    timing = []
    byte_count = 0
    for packet in source.demux(streams):
        now = time.monotonic()
        if ready_at is None and now - started > 6:
            raise TimeoutError("No decodable keyframe during warm-up.")
        if ready_at is not None and now - ready_at > args.seconds:
            break
        if last_video_at is not None and now - last_video_at > 2:
            raise TimeoutError("No advancing decoded video for two seconds.")
        if byte_count > 64000000:
            raise RuntimeError("Recording byte ceiling reached.")
        if packet.dts is None:
            continue
        idx = packet.stream.index
        if packet.stream.type == "video":
            key_packet_seen = key_packet_seen or packet.is_keyframe
            if key_packet_seen:
                frames = packet.decode()
                first_key = next((frame for frame in frames if frame.key_frame), None)
                for frame in frames:
                    if frame.pts is not None:
                        frame_time = frame.pts * frame.time_base
                        if last_video_time is None or frame_time > last_video_time:
                            last_video_time = frame_time
                            last_video_at = time.monotonic()
                            decoded_frames += 1
                if frames and last_video_at is not None and now - heartbeat_at >= .2:
                    live = {"host_monotonic_s": last_video_at,
                            "frame_pts_seconds": float(last_video_time),
                            "decoded_frames": decoded_frames}
                    temporary = dest / (args.name + "_live.tmp")
                    temporary.write_text(json.dumps(live), encoding="utf8")
                    temporary.replace(dest / (args.name + "_live.json"))
                    heartbeat_at = now
                if ready_at is None and first_key is not None:
                    ready_at = time.monotonic()
                    ready = {"event": "CAMERA_READY",
                             "host_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                             "host_monotonic_s": ready_at, "frame_pts": first_key.pts,
                             "frame_time_base": str(first_key.time_base),
                             "warmup_seconds": ready_at - started}
                    metadata["ready"] = ready
                    temporary = dest / (args.name + "_ready.tmp")
                    temporary.write_text(json.dumps(ready, indent=2), encoding="utf8")
                    temporary.replace(dest / (args.name + "_ready.json"))
                    print(json.dumps(ready), flush=True)
        timing.append({"elapsed_s": time.monotonic() - started, "stream": idx,
                       "pts": packet.pts, "dts": packet.dts, "time_base": str(packet.time_base),
                       "bytes": packet.size, "keyframe": packet.is_keyframe})
        counts[str(idx)] += 1
        byte_count += packet.size
        packet.stream = mapping[idx]
        output.mux(packet)
    if ready_at is None:
        raise RuntimeError("Stream ended before a keyframe decoded.")
    metadata.update(status="captured", elapsed_seconds=time.monotonic()-started,
                    post_ready_seconds=time.monotonic()-ready_at,
                    packets=counts, packet_bytes=byte_count)
    (dest / (args.name + "_packets.json")).write_text(json.dumps(timing, indent=2), encoding="utf8")
except Exception as exc:
    metadata.update(status="error", error_type=type(exc).__name__)
finally:
    for handle in (output, source):
        if handle is not None:
            try:
                handle.close()
            except Exception as exc:
                metadata.update(status="error", close_error_type=type(exc).__name__)
    if video_path.exists():
        metadata["bytes"] = video_path.stat().st_size
        metadata["sha256"] = hashlib.sha256(video_path.read_bytes()).hexdigest()
    metadata["end_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (dest / (args.name + ".json")).write_text(json.dumps(metadata, indent=2), encoding="utf8")
    print(json.dumps(metadata))
raise SystemExit(0 if metadata.get("status") == "captured" else 1)
