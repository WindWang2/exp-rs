#!/usr/bin/env python3
"""Generate tests/data/test_infer_two_input_add.onnx — a TWO-INPUT float
graph Y = A + B with symbolic spatial dims (b,1,h,w), for the R29-T01 real
multi-input CPU lane: the multi-input engine binds two named feeds, packs
batched halo windows, and cv::dnn::Net::forward() computes the sum through
the OpenCV DNN ONNX importer. Known answer: out[p] == a[p] + b[p].

Pure-python protobuf writer (no onnx/torch/protobuf dependency — same
discipline as gen_test_infer_addmul.py); byte-stable output.
"""
import struct
import sys
import pathlib


def varint(n: int) -> bytes:
    out = b""
    while True:
        b7 = n & 0x7F
        n >>= 7
        if n:
            out += bytes([b7 | 0x80])
        else:
            return out + bytes([b7])


def tag(field: int, wire: int) -> bytes:
    return varint((field << 3) | wire)


def f_varint(field: int, n: int) -> bytes:
    return tag(field, 0) + varint(n)


def f_str(field: int, s: str) -> bytes:
    b = s.encode()
    return tag(field, 2) + varint(len(b)) + b


def f_msg(field: int, body: bytes) -> bytes:
    return tag(field, 2) + varint(len(body)) + body


def string_list(field: int, names):
    return b"".join(f_str(field, n) for n in names)


def node(op: str, inputs, outputs: list, name: str) -> bytes:
    body = string_list(1, inputs) + string_list(2, outputs) + f_str(3, name) + f_str(4, op)
    return f_msg(1, body)


def dim_value(d: int) -> bytes:
    return f_msg(1, f_varint(1, d))  # Dimension.dim_value


def dim_param(p: str) -> bytes:
    return f_msg(1, f_str(2, p))  # Dimension.dim_param (symbolic)


def value_info(name: str, dims, out: bool = False) -> bytes:
    # TypeProto{tensor_type{elem_type=FLOAT=1, shape{dim[]}}}
    shape = b"".join(d for d in dims)
    tensor = f_varint(1, 1) + f_msg(2, shape)
    typ = f_msg(1, tensor)
    return f_msg(12 if out else 11, f_str(1, name) + f_msg(2, typ))


def main() -> int:
    # Symbolic batch/height/width; channel fixed at 1 (the engine feeds
    # one band per feed in this lane's fixture scenarios).
    a_dims = [dim_param("b"), dim_value(1), dim_param("h"), dim_param("w")]
    b_dims = [dim_param("b2"), dim_value(1), dim_param("h2"), dim_param("w2")]
    y_dims = [dim_param("b3"), dim_value(1), dim_param("h3"), dim_param("w3")]
    graph = (
        node("Add", ["A", "B"], ["Y"], "sum")
        + f_str(2, "two_input_add")
        + value_info("A", a_dims)
        + value_info("B", b_dims)
        + value_info("Y", y_dims, out=True)
    )
    opset = f_str(1, "") + f_varint(2, 17)
    model = f_varint(1, 8) + f_msg(8, opset) + f_str(2, "r29t01-gen") + f_msg(7, graph)
    out_path = pathlib.Path(__file__).with_name("test_infer_two_input_add.onnx")
    out_path.write_bytes(model)
    print(f"wrote {out_path} ({len(model)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
