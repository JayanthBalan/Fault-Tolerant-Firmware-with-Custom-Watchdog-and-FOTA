
idf_component_register(
    SRCS "main.c" "task_ops.c" "wdt_ops.c"
    INCLUDE_DIRS "inc"
    REQUIRES ota_ops
)
