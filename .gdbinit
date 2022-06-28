set pagination off
#file ./build/dartmonkey/RO/ec.RO.elf
file ./build/dartmonkey/ec.obj
add-symbol-file ./build/dartmonkey/RO/ec.RO.elf
add-symbol-file ./build/dartmonkey/RW/ec.RW.elf
target remote localhost:2331
monitor halt
#monitor flash erase
monitor reset
load
monitor reset
#break main
#break fp_task
break fp_sensor_init
break fpc_private.c:212
break fpsensor.cc:272

set  disassemble-next-line on
show disassemble-next-line
break *0x0812b39a
