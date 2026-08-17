gcc -g -o ../build/xps_v7 \
    main.c \
    lib/vec/vec.c \
    core/xps_core.c \
    core/xps_loop.c \
    network/xps_connection.c \
    network/xps_listener.c \
    utils/xps_logger.c \
    utils/xps_utils.c
