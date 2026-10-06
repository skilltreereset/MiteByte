"""Check the actual linked firmware USB descriptors. Requires pyelftools."""
import pathlib
import struct
import sys
from elftools.elf.elffile import ELFFile

ROOT = pathlib.Path(__file__).resolve().parents[1]
path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / ".pio/build/mitebyte/firmware.elf"
with path.open("rb") as stream:
    elf = ELFFile(stream)
    symbols = list(elf.get_section_by_name(".symtab").iter_symbols())

    def data(name):
        symbol = next(s for s in symbols if s.name.endswith(name))
        section = elf.get_section(symbol["st_shndx"])
        start = symbol["st_value"] - section["sh_addr"]
        return section.data()[start:start + symbol["st_size"]]

    for name in ("tud_descriptor_configuration_cb", "tud_descriptor_device_cb", "tud_descriptor_string_cb",
                 "usbd_app_driver_get_cb", "flea_rndis_recv_cb", "flea_rndis_xmit_cb", "flea_rndis_filter_cb",
                 "flea_rndis_xmit_done_cb", "flea_rndis_xmit_result_cb", "flea_rndis_try_xmit", "flea_rndis_reset_cb"):
        symbol = next(s for s in symbols if s.name == name)
        assert symbol["st_info"]["bind"] == "STB_GLOBAL", f"Core weak callback still wins: {name}"

    def configuration(name, expected_classes):
        raw = data(name)
        assert raw[:2] == bytes([9, 2])
        assert struct.unpack_from("<H", raw, 2)[0] == len(raw)
        assert raw[4] == len(expected_classes)
        interfaces, endpoints = [], []
        offset = 9
        while offset < len(raw):
            length, kind = raw[offset:offset + 2]
            assert length >= 2 and offset + length <= len(raw)
            part = raw[offset:offset + length]
            if kind == 4:
                interfaces.append((part[2], part[5]))
            if kind == 5:
                endpoints.append(part[2])
                assert struct.unpack_from("<H", part, 4)[0] == 64
            if kind == 0x21:
                assert struct.unpack_from("<H", part, 7)[0] == 67, "Keyboard report length mismatch"
            offset += length
        assert offset == len(raw)
        assert interfaces == list(enumerate(expected_classes))
        assert len(endpoints) == len(set(endpoints)), "Overlapping endpoints"
        return endpoints

    assert configuration("s_storageConfig", [8]) == [0x02, 0x82]
    active = data("s_activeConfig")
    classes = [8, 3, 2, 10] if active[4] == 4 else [8, 3]
    active_endpoints = configuration("s_activeConfig", classes)
    assert active_endpoints[:4] == [0x02, 0x82, 0x01, 0x81]
    if len(classes) == 4:
        assert active_endpoints[4:] == [0x85, 0x03, 0x84]
    storage_device = data("s_storageDevice")
    active_device = data("s_activeDevice")
    assert storage_device[:2] == active_device[:2] == bytes([18, 1])
    assert storage_device[4:7] == bytes([0, 0, 0])
    assert active_device[4:7] == (bytes([0xEF, 2, 1]) if len(classes) == 4 else bytes([0, 0, 0]))
    assert storage_device[16] == active_device[16] == 3  # mode-specific serial callback
    assert storage_device[17] == active_device[17] == 1

    assert configuration("s_hotspotConfig", [0xEF, 10]) == [0x81, 0x82, 0x02]
    hotspot_device = data("s_hotspotDevice")
    assert hotspot_device[:2] == bytes([18, 1])
    assert hotspot_device[4:7] == bytes([0xEF, 2, 1])
    assert hotspot_device[16:18] == bytes([3, 1])
    hotspot = data("s_hotspotConfig")
    assert hotspot[13:16] == bytes([0xEF, 4, 1]), "RNDIS association class mismatch"
    assert any(s.name == "flea_rndis_open" for s in symbols), "Application network driver missing"

print("PASS: linked USB callbacks, storage/active/hotspot descriptors, application RNDIS driver")
