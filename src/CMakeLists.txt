include_directories(.)
include_directories(./threads/)
include_directories(./devices/)


add_subdirectory(./lib)

#add_subdirectory(./threads)

#add_subdirectory(./devices)


add_executable(hello
        ./threads/init.c ./threads/interrupt.c ./threads/malloc.c ./threads/palloc.c ./threads/synch.c ./threads/thread.c
        ./devices/timer.c ./lib/kernel/list.c)
