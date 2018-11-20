aux_source_directory(. DIR_FAT32_SRCS)
add_compile_options(-mno-ms-bitfields)
add_library(fat32 ${DIR_FAT32_SRCS})