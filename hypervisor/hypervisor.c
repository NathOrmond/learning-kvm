#include <fcntl.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

void kvm(uint8_t code[], size_t code_len);

int main(int argc, char *argv[]){
   /*
  .code16
  mov al, 0x61
  mov dx, 0x217
  out dx, al
  mov al, 10
  out dx, al
  hlt
  */
  uint8_t code[] = "\xB0\x61\xBA\x17\x02\xEE\xB0\n\xEE\xF4";
  kvm(code, sizeof(code));
}

/**
 * Steps 1 - 3 set up user memory region
 * Steps 4 - 6 create and set up vCPU
 * Step  7     execute code ...
 **/
void kvm(uint8_t code[], size_t code_len) {
  // step 1, open /dev/kvm
  int kvmfd = open("/dev/kvm", O_RDWR|O_CLOEXEC);
  if(kvmfd == -1) errx(1, "failed to open /dev/kvm");

  // step 2, create VM
  int vmfd = ioctl(kvmfd, KVM_CREATE_VM, 0);

  // step 3, set up user memory region
  size_t mem_size = 0x40000000; // size of user memory you want to assign
  void *mem = mmap(0, mem_size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
  int user_entry = 0x0;
  memcpy((void*)((size_t)mem + user_entry), code, code_len);
  struct kvm_userspace_memory_region region = {
    .slot = 0,
    .flags = 0,
    .guest_phys_addr = 0,
    .memory_size = mem_size,
    .userspace_addr = (size_t)mem
  };
  ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &region);
  /* end of step 3 */
  
  // step 4, create vCPU
  int vcpufd = ioctl(vmfd, KVM_CREATE_VCPU, 0);

  // step 5, set up memory for vCPU
  size_t vcpu_mmap_size = ioctl(kvmfd, KVM_GET_VCPU_MMAP_SIZE, NULL);
  struct kvm_run* run = (struct kvm_run*) mmap(0, vcpu_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpufd, 0);

  // step 6, set up vCPU's registers
  /* standard registers include general-purpose registers and flags */
  struct kvm_regs regs;
  ioctl(vcpufd, KVM_GET_REGS, &regs);
  regs.rip = user_entry;
  regs.rsp = 0x200000; // stack address
  regs.rflags = 0x2; // in x86 the 0x2 bit should always be set
  ioctl(vcpufd, KVM_SET_REGS, &regs); // set registers

  /* special registers include segment registers */
  struct kvm_sregs sregs;
  ioctl(vcpufd, KVM_GET_SREGS, &sregs);
  sregs.cs.base = sregs.cs.selector = 0; // let base of code segment equal to zero
  ioctl(vcpufd, KVM_SET_SREGS, &sregs);
  // not finished ...
    
  // step 7, execute vm and handle exit reason
  while (1) {
    ioctl(vcpufd, KVM_RUN, NULL);
    switch (run->exit_reason) {
    case KVM_EXIT_HLT:
      fputs("KVM_EXIT_HLT\n", stderr);
      return 0;
    case KVM_EXIT_IO:
      /* TODO: check port and direction here */
      putchar(*(((char *)run) + run->io.data_offset));
      break;
    case KVM_EXIT_FAIL_ENTRY:
      errx(1, "KVM_EXIT_FAIL_ENTRY: hardware_entry_failure_reason = 0x%llx\n",  run->fail_entry.hardware_entry_failure_reason);
    case KVM_EXIT_INTERNAL_ERROR:
      errx(1, "KVM_EXIT_INTERNAL_ERROR: suberror = 0x%x\n", run->internal.suberror);
    case KVM_EXIT_SHUTDOWN:
      errx(1, "KVM_EXIT_SHUTDOWN");
    default:
      errx(1, "Unhandled reason: %d", run->exit_reason);
    }
  }

}
