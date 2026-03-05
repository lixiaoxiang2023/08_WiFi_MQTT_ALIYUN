# analyze_iram_report_fixed2.py
from elftools.elf.elffile import ELFFile

ELF_PATH = "build/08_WiFi_MQTT_ALIYUN.elf"
OUTPUT_FILE = "iram_report.txt"
IRAM_SECTION = ".iram0.text"
MIN_SIZE_BYTES = 0  # 输出大于此字节的函数

def main():
    with open(ELF_PATH, "rb") as f:
        elf = ELFFile(f)

        # 找到 IRAM section 的 header
        iram_section = None
        for idx, sec in enumerate(elf.iter_sections()):
            if sec.name == IRAM_SECTION:
                iram_section = sec
                iram_section_index = idx
                break
        if iram_section is None:
            print(f"Section {IRAM_SECTION} not found!")
            return

        iram_size = iram_section.data_size
        symbols = []

        # 遍历符号表
        for sec in elf.iter_sections():
            if sec.header['sh_type'] != 'SHT_SYMTAB':
                continue
            for sym in sec.iter_symbols():
                if sym['st_shndx'] == iram_section_index:
                    size = sym['st_size']
                    if size >= MIN_SIZE_BYTES:
                        symbols.append({
                            "name": sym.name,
                            "size": size
                        })

        # 按大小排序
        symbols.sort(key=lambda x: x['size'], reverse=True)

        # 输出报告
        cumulative = 0
        with open(OUTPUT_FILE, "w", encoding="utf-8") as f_out:
            f_out.write(f"Total IRAM section {IRAM_SECTION} size: {iram_size} bytes\n\n")
            f_out.write(f"{'Function':<35} {'Size (KB)':>10} {'% of IRAM':>12} {'Cumulative %':>14}\n")
            f_out.write("="*75 + "\n")
            for sym in symbols:
                size_kb = sym["size"] / 1024
                percent = sym["size"] / iram_size * 100
                cumulative += percent
                f_out.write(f"{sym['name']:<35} {size_kb:>10.2f} {percent:>12.2f}% {cumulative:>14.2f}%\n")

        print(f"IRAM report generated: {OUTPUT_FILE}")

if __name__ == "__main__":
    main()