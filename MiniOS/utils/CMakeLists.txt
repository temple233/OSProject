aux_source_directory(. DIR_UTILS_SRCS)
add_compile_options(-mno-ms-bitfields)
add_library(utils ${DIR_UTILS_SRCS})