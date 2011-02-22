# .gdbinit file
# $Id: dot.gdbinit,v 1.1.1.1 2006/01/06 00:51:12 jsantiago Exp $
#-----
# NOTE: this file is generated by a script, "gen_gdbinit.pl".
# (Please type "gen_gdbinit.pl --help" and check the help message).
# $ Id: gen_gdbinit.pl,v 1.12 2004/07/26 09:56:10 takata Exp $
#-----
# target platform: mappi

# setting
set width 0d70
set radix 0d16
debug_chaos

# clk xin:cpu:bif:bus=30:360:180:90
define clock_init
  set *(unsigned long *)0x00ef4024 = 2
  set *(unsigned long *)0x00ef4020 = 1
  set *(unsigned long *)0x00ef4010 = 0
  set *(unsigned long *)0x00ef4014 = 0
  set *(unsigned long *)0x00ef4004 = 5
  shell sleep 0.1
  set *(unsigned long *)0x00ef4008 = 0x00000200
end

# Initialize programmable ports
define port_init
  set $sfrbase = 0x00ef0000
  set *(unsigned short *)0x00ef1060 = 0x5555
  set *(unsigned short *)0x00ef1062 = 0x5555
  set *(unsigned short *)0x00ef1064 = 0x5555
  set *(unsigned short *)0x00ef1066 = 0x5555
  set *(unsigned short *)0x00ef1068 = 0x5555
  set *(unsigned short *)0x00ef106a = 0x0000
  set *(unsigned short *)0x00ef106e = 0x5555
  set *(unsigned short *)0x00ef1070 = 0x5555
  # LED ON
  set *(unsigned char *)($sfrbase + 0x1015) = 0xff
  set *(unsigned char *)($sfrbase + 0x1085) = 0xff
  shell sleep 0.1
  # LED OFF
  set *(unsigned char *)($sfrbase + 0x1085) = 0x00
end
document port_init
  P5=LED(output), P6.b4=LAN_RESET(output)
end

# Initialize SDRAM controller
define sdram_init
  # SDIR0
  set *(unsigned long *)0x00ef6008 = 0x00000182
  # SDIR1
  set *(unsigned long *)0x00ef600c = 0x00000001
  # Initialize wait
  shell sleep 0.1
  # Ch0-MOD
  set *(unsigned long *)0x00ef602c = 0x00000020
  # Ch0-TR
  set *(unsigned long *)0x00ef6028 = 0x00051502
  # Ch0-ADR (size:64MB)
  set *(unsigned long *)0x00ef6020 = 0x08000004
  # AutoRef On
  set *(unsigned long *)0x00ef6004 = 0x00010e2b
  # Access enable
  set *(unsigned long *)0x00ef6024 = 0x00000001
end
document sdram_init
  SDRAM controller initialization
  0x08000000 - 0x0bffffff (64MB)
end

# Initialize LAN controller
define lanc_init
  set $sfrbase = 0x00ef0000
  # Set BSEL3 (BSEL3 for the Chaos's bselc)
  set *(unsigned long *)($sfrbase + 0x5300) = 0x0a0a8040
  set *(unsigned long *)($sfrbase + 0x5304) = 0x01120203
  set *(unsigned long *)($sfrbase + 0x5308) = 0x00000001
  # Reset (P5=LED,P6.b4=LAN_RESET)
  set *(unsigned short *)($sfrbase + 0x106c) = 0x0000
  set *(unsigned char *)($sfrbase + 0x1016) = 0xff
  set *(unsigned char *)($sfrbase + 0x1086) = 0xff
  shell sleep 0.1
  # swivel: 0=normal, 4=reverse
#  set *(unsigned char *)($sfrbase + 0x1086) = 0x00
  set *(unsigned char *)($sfrbase + 0x1086) = 0x04
  set *(unsigned long *)(0x0c000330) = 0xffffffff
  # Set mac address
  set $lanc = (void*)0x0c000300
  set *(unsigned long *)($lanc + 0x0000) = 0x00610010
  set *(unsigned long *)($lanc + 0x0004) = 0x00200030
  set *(unsigned long *)($lanc + 0x0008) = 0x00400050
  set *(unsigned long *)($lanc + 0x000c) = 0x00600007
end
document lanc_init
  LAN controller initialization
  ex.) MAC address:  10 20 30 40 50 60
end

# LCD & CRT dual-head setting (8bpp)
define dispc_init
  set $sfrbase = 0x00ef0000
  # BSEL4 Dispc
  set *(unsigned long *)($sfrbase + 0x5400) = 0x0e0e8000
  set *(unsigned long *)($sfrbase + 0x5404) = 0x0012220a
end

# MMU enable
define mmu_enable
  set $evb=0x88000000
  set *(unsigned long *)0xffff0024=1
end

# MMU disable
define mmu_disable
  set $evb=0
  set *(unsigned long *)0xffff0024=0
end

# Show TLB entries
define show_tlb_entries
  set $i = 0
  set $addr = $arg0
  set $nr_entries = $arg1
  use_mon_code
  while ($i < $nr_entries)
    set $tlb_tag = *(unsigned long*)$addr
    set $tlb_data = *(unsigned long*)($addr + 4)
    printf " [%2d] 0x%08lx : 0x%08lx - 0x%08lx\n", $i, $addr, $tlb_tag, $tlb_data
    set $i = $i + 1
    set $addr = $addr + 8
  end
  use_debug_dma
end
define itlb
  set $itlb=0xfe000000
  show_tlb_entries $itlb 0d32
end
define dtlb
  set $dtlb=0xfe000800
  show_tlb_entries $dtlb 0d32
end

# Show current task structure
define show_current
  set $current = $spi & 0xffffe000
  printf "$current=0x%08lX\n",$current
  print *(struct task_struct *)$current
end

# Show user assigned task structure
define show_task
  set  = $arg0 & 0xffffe000
  printf "$task=0x%08lX\n",$task
  print *(struct task_struct *)$task
end
document show_task
  Show user assigned task structure
  arg0 : task structure address
end

# Show M32R registers
define show_regs
  printf " R0[0x%08lX]   R1[0x%08lX]   R2[0x%08lX]   R3[0x%08lX]\n",$r0,$r1,$r2,$r3
  printf " R4[0x%08lX]   R5[0x%08lX]   R6[0x%08lX]   R7[0x%08lX]\n",$r4,$r5,$r6,$r7
  printf " R8[0x%08lX]   R9[0x%08lX]  R10[0x%08lX]  R11[0x%08lX]\n",$r8,$r9,$r10,$r11
  printf "R12[0x%08lX]   FP[0x%08lX]   LR[0x%08lX]   SP[0x%08lX]\n",$r12,$fp,$lr,$sp
  printf "PSW[0x%08lX]  CBR[0x%08lX]  SPI[0x%08lX]  SPU[0x%08lX]\n",$psw,$cbr,$spi,$spu
  printf "BPC[0x%08lX]   PC[0x%08lX] ACCL[0x%08lX] ACCH[0x%08lX]\n",$bpc,$pc,$accl,$acch
  printf "EVB[0x%08lX]\n",$evb
end

# Setup all
define setup
  use_mon_code
  set *(unsigned int)0xfffffffc=0x60
  shell sleep 0.1
  clock_init
  shell sleep 0.1
  port_init
  sdram_init
  lanc_init
  dispc_init
  set $evb=0x08000000
end

# Load modules
define load_modules
  use_debug_dma
  load
end

# Set kernel parameters
define set_kernel_parameters
  set $param = (void*)0x08001000
  # INITRD_START
#  set *(unsigned long *)($param + 0x0010) = 0x08300000
  # INITRD_SIZE
#  set *(unsigned long *)($param + 0x0014) = 0x00000000
  # M32R_CPUCLK
  set *(unsigned long *)($param + 0x0018) = 0d360000000
  # M32R_BUSCLK
  set *(unsigned long *)($param + 0x001c) = 0d90000000

  # M32R_TIMER_DIVIDE
  set *(unsigned long *)($param + 0x0020) = 0d128

  set {char[0x200]}($param + 0x100) = "console=ttyS0,115200n8x console=tty1 root=/dev/nfsroot nfsroot=192.168.0.1:/project/m32r-linux/export/rootfs nfsaddrs=192.168.0.101:192.168.0.1:192.168.0.1:255.255.255.0:mappi001 \0"
end

# Boot
define boot
  set_kernel_parameters
  set $fp = 0
  set $pc = 0x08002000
  si
  c
end

# Set breakpoints
define set_breakpoints
  b *0x08000030
end

# Restart
define restart
  sdireset
  sdireset
  setup
  load_modules
  boot
end

sdireset
sdireset
file vmlinux
target m32rsdi
setup
#load_modules
#set_breakpoints
#boot

