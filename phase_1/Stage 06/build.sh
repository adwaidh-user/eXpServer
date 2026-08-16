gcc -g -o ../build/xps_v6 \
    main.c \
    lib/vec/vec.c \
    network/xps_connection.c \
    network/xps_listener.c \
    utils/xps_logger.c \
    utils/xps_utils.c
