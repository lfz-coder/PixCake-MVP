"""Print input/output tensor info and normalization metadata of a .tflite model."""
import sys

from mediapipe.tasks.metadata import metadata_schema_py_generated as M
from mediapipe.tasks.metadata import schema_py_generated as S

buf = open(sys.argv[1], "rb").read()
model = S.Model.GetRootAsModel(buf, 0)
sg = model.Subgraphs(0)
TYPES = {v: k for k, v in S.TensorType.__dict__.items() if not k.startswith("_")}
for kind, n, get in (("input", sg.InputsLength(), sg.Inputs), ("output", sg.OutputsLength(), sg.Outputs)):
    for i in range(n):
        t = sg.Tensors(get(i))
        print(kind, i, t.Name().decode(), t.ShapeAsNumpy().tolist(), TYPES.get(t.Type()))
print("operator codes:", sorted({model.OperatorCodes(i).BuiltinCode() for i in range(model.OperatorCodesLength())}))
custom = [model.OperatorCodes(i).CustomCode() for i in range(model.OperatorCodesLength()) if model.OperatorCodes(i).CustomCode()]
print("custom ops:", custom)

for i in range(model.MetadataLength()):
    md = model.Metadata(i)
    name = md.Name().decode()
    print("metadata buffer:", name)
    if name != "TFLITE_METADATA":
        continue
    raw = model.Buffers(md.Buffer()).DataAsNumpy().tobytes()
    meta = M.ModelMetadata.GetRootAsModelMetadata(raw, 0)
    print("name:", meta.Name(), "version:", meta.Version(), "author:", meta.Author(), "license:", meta.License())
    print("min_parser_version:", meta.MinParserVersion())
    sgm = meta.SubgraphMetadata(0)
    for j in range(sgm.InputTensorMetadataLength()):
        tm = sgm.InputTensorMetadata(j)
        print("input meta:", tm.Name(), tm.Description())
        for k in range(tm.ProcessUnitsLength()):
            pu = tm.ProcessUnits(k)
            if pu.OptionsType() == M.ProcessUnitOptions.NormalizationOptions:
                opt = M.NormalizationOptions()
                opt.Init(pu.Options().Bytes, pu.Options().Pos)
                print("  normalization mean", opt.MeanAsNumpy().tolist(), "std", opt.StdAsNumpy().tolist())
        st = tm.Stats()
        if st:
            print("  stats min", st.MinAsNumpy().tolist() if st.MinLength() else None,
                  "max", st.MaxAsNumpy().tolist() if st.MaxLength() else None)
        content = tm.Content()
        if content and content.ContentPropertiesType() == M.ContentProperties.ImageProperties:
            ip = M.ImageProperties()
            ip.Init(content.ContentProperties().Bytes, content.ContentProperties().Pos)
            print("  color space", ip.ColorSpace())
    for j in range(sgm.OutputTensorMetadataLength()):
        tm = sgm.OutputTensorMetadata(j)
        print("output meta:", tm.Name(), tm.Description())
        for k in range(tm.AssociatedFilesLength()):
            print("  file", tm.AssociatedFiles(k).Name(), tm.AssociatedFiles(k).Type())
        for k in range(tm.ProcessUnitsLength()):
            print("  process unit type", tm.ProcessUnits(k).OptionsType())
    for j in range(sgm.CustomMetadataLength()):
        cm = sgm.CustomMetadata(j)
        print("custom metadata:", cm.Name(), bytes(cm.DataAsNumpy()) if cm.DataLength() else b"")
