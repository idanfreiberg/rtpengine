import json
from pathlib import Path

def encode(x):
    if isinstance(x, str):
        b = x.encode()
        return str(len(b)).encode() + b":" + b
    if isinstance(x, int):
        return b"i" + str(x).encode() + b"e"
    if isinstance(x, list):
        return b"l" + b"".join(map(encode, x)) + b"e"
    return b"d" + b"".join(encode(k) + encode(v) for k, v in sorted(x.items())) + b"e"

sdp = "v=0\r\no=- 1234567890 1 IN IP4 192.0.2.1\r\ns=bench\r\nc=IN IP4 192.0.2.1\r\nt=0 0\r\nm=audio 35000 RTP/AVP 0 8 101\r\na=rtpmap:0 PCMU/8000\r\na=rtpmap:8 PCMA/8000\r\na=rtpmap:101 telephone-event/8000\r\na=fmtp:101 0-16\r\na=rtcp:35001 IN IP4 192.0.2.1\r\na=sendrecv\r\n"
base = {"call-id": "synthetic-integer-benchmark-call-1234567890", "from-tag": "peer-1234567890"}
requests = [
    dict(base, command="offer", sdp=sdp, flags=["trust address"], direction=["external", "internal"]),
    dict(base, command="answer", sdp=sdp, **{"to-tag": "other-9876543210"}),
    dict(base, command="query"),
    dict(base, command="offer", sdp=sdp, flags=["trust address"], codec={"mask": ["PCMU", "PCMA"], "transcode": ["L16/16000"]}, **{"TOS": 184, "ptime": 20}),
]
Path("bench/payloads.h").write_text("static const char *payloads[] = {\n" + ",\n".join(json.dumps(encode(x).decode()) for x in requests) + "\n};\n")
