#!/usr/bin/env python3
"""Generate tests/data/test_infer_addmul.onnx — a 4-op float graph
(X + 1.5) * 3.0 + 0.5 that makes cv::dnn::Net::forward() return a header onto
an INTERNAL output blob (unlike the pass-through Identity fixture, whose
forward hands back the caller's own buffer). Used by the R18-B shared-session
detachment race regression (tests/test_r14_B_runtime_providers.cpp).

Pure-python protobuf writer: no onnx/torch/protobuf dependency (the CI hosts
and this dev host have none), the file layout is byte-stable, and the graph is
small enough to verify by hand.
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
            out += bytes([b7])
            return out


def tag(field: int, wire: int) -> bytes:
    return varint((field << 3) | wire)


def f_varint(field: int, n: int) -> bytes:
    return tag(field, 0) + varint(n)


def f_str(field: int, s: str) -> bytes:
    b = s.encode()
    return tag(field, 2) + varint(len(b)) + b


def f_msg(field: int, body: bytes) -> bytes:
    return tag(field, 2) + varint(len(body)) + body


def f_packed_int64(field: int, vals) -> bytes:
    body = b"".join(varint(v) for v in vals)
    return tag(field, 2) + varint(len(body)) + body


def f_packed_float(field: int, vals) -> bytes:
    body = b"".join(struct.pack("<f", v) for v in vals)
    return tag(field, 2) + varint(len(body)) + body


def string_list(field: int, names):
    return b"".join(f_str(field, n) for n in names)


def node(op: str, inputs, outputs: list, name: str) -> bytes:
    body = string_list(1, inputs) + string_list(2, outputs) + f_str(3, name) + f_str(4, op)
    return f_msg(1, body)


def scalar_init(name: str, value: float) -> bytes:
    # TensorProto{ dims=[1], data_type=1(FLOAT), name, float_data=[v] }
    body = f_packed_int64(1, [1]) + f_varint(2, 1) + f_packed_float(4, [value]) + f_str(8, name)
    return f_msg(5, body)


def value_info(name: str, dims: list) -> bytes:
    # ValueInfoProto{ name, type=TypeProto{tensor_type{elem_type=FLOAT, shape{dim[]}}}}
    shape = b"".join(f_msg(1, f_varint(1, d)) for d in dims)  # Dimension.dim_value
    tensor = f_varint(1, 1) + f_msg(2, shape)  # elem_type FLOAT=1, shape
    typ = f_msg(1, tensor)
    return f_msg(11, f_str(1, name) + f_msg(2, typ))  # GraphProto.input


def value_info_out(name: str, dims: list) -> bytes:
    shape = b"".join(f_msg(1, f_varint(1, d)) for d in dims)
    tensor = f_varint(1, 1) + f_msg(2, shape)
    typ = f_msg(1, tensor)
    return f_msg(12, f_str(1, name) + f_msg(2, typ))  # GraphProto.output


def main() -> int:
    side = 512
    dims = [1, 1, side, side]
    graph_name = "addmul"
    nodes = (
        node("Add", ["X", "C1"], ["T1"], "add1")
        + node("Mul", ["T1", "D1"], ["T2"], "mul1")
        + node("Add", ["T2", "C2"], ["T3"], "add2")
        + node("Mul", ["T3", "D2"], ["Y"], "mul2")
    )
    inits = scalar_init("C1", 1.5) + scalar_init("D1", 3.0) + scalar_init("C2", 0.5) + scalar_init("D2", 1.0)
    graph = (
        nodes
        + f_str(2, graph_name)
        + inits
        + value_info("X", dims)
        + value_info_out("Y", dims)
    )
    opset = f_str(1, "") + f_varint(2, 17)  # OperatorSetIdProto{domain="", version=17}
    model = f_varint(1, 8) + f_msg(8, opset) + f_str(2, "r18b-gen") + f_msg(7, graph)
    out = pathlib.Path(__file__).with_name("test_infer_addmul.onnx")
    out.write_bytes(model)
    print(f"wrote {out} ({len(model)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
