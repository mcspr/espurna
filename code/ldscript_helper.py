from collections import defaultdict
from string import Formatter  # vformat / no format_map in py2.7

IROM0_0_SEG_DEFAULT = 0x40201010
VARIANTS = {
    "512k0m1s": {"_SPIFFS_start": 0x4027B000, "_SPIFFS_end": 0x4027B000},
    "1m0m1s": {"_SPIFFS_start": 0x402FB000, "_SPIFFS_end": 0x402FB000},
    "1m0m2s": {"_SPIFFS_start": 0x402FA000, "_SPIFFS_end": 0x402FA000},
    "2m1m4s": {
        "_SPIFFS_start": 0x40300000,
        "_SPIFFS_end": 0x403F8000,
        "_SPIFFS_page": 0x100,
        "_SPIFFS_block": 0x2000,
    },
    "4m1m4s": {
        "_SPIFFS_start": 0x40500000,
        "_SPIFFS_end": 0x405F8000,
        "_SPIFFS_page": 0x100,
        "_SPIFFS_block": 0x2000,
    },
    "4m3m4e": {
        "_SPIFFS_start": 0x40300000,
        "_SPIFFS_end": 0x405F8000,
        "_SPIFFS_page": 0x100,
        "_SPIFFS_block": 0x2000,
    },
}

TEMPLATE = """
MEMORY
{{
  dport0_0_seg :                        org = 0x3FF00000, len = 0x10
  dram0_0_seg :                         org = 0x3FFE8000, len = 0x14000
  iram1_0_seg :                         org = 0x40100000, len = 0x8000
  irom0_0_seg :                         org = 0x40201010, len = 0x{irom0_0_seg_len:X}
}}

PROVIDE ( _SPIFFS_start = 0x{_SPIFFS_start:X} );
PROVIDE ( _SPIFFS_end = 0x{_SPIFFS_end:X} );
PROVIDE ( _SPIFFS_page = 0x{_SPIFFS_page:X} );
PROVIDE ( _SPIFFS_block = 0x{_SPIFFS_block:X} );

INCLUDE \"{include}\"
"""


def render_ldscript(variant, include):
    args = VARIANTS[variant]
    args_dict = defaultdict(int, **args)
    args_dict["include"] = include
    args_dict["irom0_0_seg_len"] = args["_SPIFFS_start"] - IROM0_0_SEG_DEFAULT
    ldscript = Formatter().vformat(TEMPLATE, (), args_dict)
    return ldscript


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("variant", choices=sorted(VARIANTS.keys()))
    parser.add_argument("include", help="INCLUDE file name")

    args = parser.parse_args()

    print(render_ldscript(args.variant, args.include).strip())
