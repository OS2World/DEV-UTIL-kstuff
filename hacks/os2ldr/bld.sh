#!/bin/bash
set -ex
yasm -f bin -L nasm -l pcibios_workaround1.lst -o pcibios_workaround1.bin pcibios_workaround1.asm
# nasm -f bin -l pcibios_workaround1.lst -o pcibios_workaround1.bin pcibios_workaround1.asm - doesn't work
g++ -g os2ldr_patcher.cpp  -o os2ldr_patcher -g -Wall -pedantic
./os2ldr_patcher os2ldr pcibios_workaround1.bin os2ldr.new

