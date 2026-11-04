"""Reproduces the automatic-selection models Patchy ships (docs/ai-selection-models.md).

Dev tooling only: builds never run this. Every output is byte-reproducible from the pinned
upstream fp32 file it names below.

- MobileSAM and BiRefNet (release `ai-models-1`): dynamic int8 (weights int8 per channel,
  activations quantized at run time) on MatMul/Gemm only. Quantizing convolutions too was
  measured and rejected: MobileSAM's masks break and BiRefNet runs 2.3x slower on CPU.
- EfficientViT-SAM XL0 (release `ai-models-2`): float16 weights, each cast back to float32
  by a Cast node that ONNX Runtime folds at session load, so it runs in float32. int8 was
  measured and rejected for this encoder: its masks fall to IoU 0.43 against fp32 (0.84
  embedding cosine even with 32-weight blocks), while fp16 keeps IoU 0.9998. The decoder
  also gets its four masks and their predicted IoUs as outputs in place of the export's
  in-graph argmax, so Patchy picks the output the way it does for MobileSAM.

Outputs are published as assets of those GitHub releases and pinned by SHA-256 in
scripts/setup-ai-deps.* and the Flatpak manifest.

Requirements (match the pinned runtime):
    python -m venv venv
    venv/Scripts/python -m pip install onnxruntime==1.30.0 onnx==1.23.2 numpy
Usage:
    venv/Scripts/python -I scripts/ai/quantize_models.py <fp32 models folder> <output folder>
The fp32 folder holds any of the upstream files listed below under their input names
(download them and check the SHA-256); each one present is converted.
"""
import hashlib
import os
import sys

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper
from onnxruntime.quantization import QuantType, quantize_dynamic

# EfficientViT-SAM (MIT Han Lab, Apache-2.0), pinned to one Hugging Face commit.
EFFICIENTVIT_SAM = ("https://huggingface.co/mit-han-lab/efficientvit-sam/resolve/"
                    "a2f0c5929196a13ef1ffc9338d3f9e482e1e0e68/onnx/")

# Upstream fp32 sources: (input name, upstream URL, SHA-256, output name, conversion).
MODELS = [
    ("mobile_sam_image_encoder.onnx",
     "https://huggingface.co/Acly/MobileSAM/resolve/main/mobile_sam_image_encoder.onnx",
     "580f5fb648ea1062c0aabc26217aed56921985f03f0cbbd852bba81d760cc749",
     "mobile_sam_image_encoder_int8.onnx", "int8"),
    ("sam_mask_decoder_multi.onnx",
     "https://huggingface.co/Acly/MobileSAM/resolve/main/sam_mask_decoder_multi.onnx",
     "8976b90a87ba50a6a72217a5ff994f7d25ce16f2229fcc1ed259e1294c622ffe",
     "sam_mask_decoder_multi_int8.onnx", "int8"),
    ("birefnet_lite_general_1024.onnx",
     "https://github.com/ZhengPeng7/BiRefNet/releases/download/v1/BiRefNet-general-bb_swin_v1_tiny-epoch_232.onnx",
     "5600024376f572a557870a5eb0afb1e5961636bef4e1e22132025467d0f03333",
     "birefnet_lite_general_1024_int8.onnx", "int8"),
    ("xl0_encoder.onnx", EFFICIENTVIT_SAM + "xl0_encoder.onnx",
     "b39add8f7e2f8a612af867e864d962eff5ac98c8336b32feca08327a89cc8de3",
     "efficientvit_sam_xl0_encoder_fp16.onnx", "fp16"),
    ("xl0_decoder.onnx", EFFICIENTVIT_SAM + "xl0_decoder.onnx",
     "051bfae9a07c1ff180fd7c5238334fdc45eaa0227c46f29740d98662865e01f7",
     "efficientvit_sam_xl0_decoder_multi_fp16.onnx", "fp16_multimask"),
]

# The EfficientViT-SAM decoder export's tensors holding all four masks (low resolution,
# 256x256 over the padded 1024 frame) and their predicted IoUs, before its argmax.
DECODER_ALL_MASKS = "/Reshape_5_output_0"
DECODER_ALL_SCORES = "/iou_prediction_head/layers.2/Gemm_output_0"


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def strip_initializer_identities(source, destination):
    """The BiRefNet export routes MatMul weights through Identity nodes; with them in place
    the quantizer keeps each float weight alive beside its int8 copy (the file grows)."""
    model = onnx.load(source)
    graph = model.graph
    initializers = {item.name for item in graph.initializer}
    outputs = {item.name for item in graph.output}
    rename = {}
    kept = []
    for node in graph.node:
        if node.op_type == "Identity" and node.input[0] in initializers and node.output[0] not in outputs:
            rename[node.output[0]] = node.input[0]
        else:
            kept.append(node)
    for node in kept:
        for index, name in enumerate(node.input):
            while name in rename:
                name = rename[name]
            node.input[index] = name
    del graph.node[:]
    graph.node.extend(kept)
    onnx.checker.check_model(model)
    onnx.save(model, destination)
    return len(rename)


def expose_all_masks(model):
    """Makes the decoder output all four masks and their scores instead of the best one."""
    graph = model.graph
    produced = {output for node in graph.node for output in node.output}
    for name in (DECODER_ALL_MASKS, DECODER_ALL_SCORES):
        if name not in produced:
            sys.exit(f"decoder tensor {name} not found: the upstream export changed")
    needed = {DECODER_ALL_MASKS, DECODER_ALL_SCORES}
    kept = []
    for node in reversed(list(graph.node)):
        if any(output in needed for output in node.output):
            kept.append(node)
            needed.update(node.input)
    del graph.node[:]
    graph.node.extend(reversed(kept))
    used = {name for node in graph.node for name in node.input}
    initializers = [item for item in graph.initializer if item.name in used]
    del graph.initializer[:]
    graph.initializer.extend(initializers)
    # The old outputs' producers are gone, so their names are free again.
    rename = {DECODER_ALL_MASKS: "low_res_masks", DECODER_ALL_SCORES: "iou_predictions"}
    for node in graph.node:
        for index, name in enumerate(node.output):
            node.output[index] = rename.get(name, name)
        for index, name in enumerate(node.input):
            node.input[index] = rename.get(name, name)
    del graph.output[:]
    graph.output.extend([
        helper.make_tensor_value_info("low_res_masks", TensorProto.FLOAT, ["batch", 4, 256, 256]),
        helper.make_tensor_value_info("iou_predictions", TensorProto.FLOAT, ["batch", 4])])


def float16_weights(model):
    """Stores every float32 weight tensor as float16 behind a Cast back to float32."""
    graph = model.graph
    initializers, casts = [], []
    for item in graph.initializer:
        values = numpy_helper.to_array(item)
        if item.data_type != TensorProto.FLOAT or values.size < 16:
            initializers.append(item)
            continue
        initializers.append(numpy_helper.from_array(values.astype(np.float16), item.name + "_fp16"))
        casts.append(helper.make_node("Cast", [item.name + "_fp16"], [item.name], to=TensorProto.FLOAT,
                                      name=item.name + "_cast"))
    del graph.initializer[:]
    graph.initializer.extend(initializers)
    nodes = casts + list(graph.node)
    del graph.node[:]
    graph.node.extend(nodes)
    return len(casts)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    source_dir, output_dir = sys.argv[1], sys.argv[2]
    os.makedirs(output_dir, exist_ok=True)
    converted = 0
    for name, url, expected, output_name, conversion in MODELS:
        source = os.path.join(source_dir, name)
        if not os.path.exists(source):
            continue
        actual = sha256(source)
        if actual != expected:
            sys.exit(f"{name}: SHA-256 {actual} does not match the pinned upstream {expected} ({url})")
        output = os.path.join(output_dir, output_name)
        if conversion == "int8":
            staged = os.path.join(output_dir, name + ".stripped.onnx")
            removed = strip_initializer_identities(source, staged)
            quantize_dynamic(staged, output, weight_type=QuantType.QInt8, per_channel=True,
                             op_types_to_quantize=["MatMul", "Gemm"])
            os.remove(staged)
            detail = f"removed {removed} Identity nodes"
        else:
            model = onnx.load(source)
            if conversion == "fp16_multimask":
                expose_all_masks(model)
            casts = float16_weights(model)
            onnx.checker.check_model(model)
            onnx.save(model, output)
            detail = f"{casts} float16 weights"
        converted += 1
        print(f"{output_name}: {os.path.getsize(output) / 1e6:.1f} MB, {detail}, sha256 {sha256(output)}")
    if converted == 0:
        sys.exit(f"none of the upstream files listed in {sys.argv[0]} are in {source_dir}")


main()
