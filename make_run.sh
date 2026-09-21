scons build/X86/gem5.opt -j40

sudo build/X86/gem5.opt --debug-flag=CxlMemory --debug-flag=SimpleCPU  -d origin configs/fs.py  --disk ~/gem5-for-CXL/full-system-image/disks/parsec.img --kernel ~/linux/vmlinux --cpu-type=X86AtomicSimpleCPU

