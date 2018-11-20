aux_source_directory(. DIR_FSCACHE_SRCS)
add_compile_options(-mno-ms-bitfields)
add_library(fscache ${DIR_FSCACHE_SRCS})
