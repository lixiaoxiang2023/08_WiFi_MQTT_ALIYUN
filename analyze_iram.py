from elftools.elf.elffile import ELFFile

MIN_SIZE = 1024  # 1KB

with open("build/08_WiFi_MQTT_ALIYUN.elf", "rb") as f:
    elf = ELFFile(f)
    symtab = elf.get_section_by_name('.symtab')
    if not symtab:
        print("No symbol table found!")
        exit(1)

    iram_functions = []
    for symbol in symtab.iter_symbols():
        # 忽略没有 section 的符号
        if isinstance(symbol['st_shndx'], str):
            continue

        sec = elf.get_section(symbol['st_shndx'])
        if not sec:
            continue

        # 判断是否在 IRAM
        if '.iram0.text' in sec.name:
            size = symbol['st_size']
            if size >= MIN_SIZE:
                iram_functions.append((symbol.name, size))

print("IRAM functions >1KB:")
for name, size in sorted(iram_functions, key=lambda x: -x[1]):
    print(f"{name:40} {size/1024:.2f} KB")