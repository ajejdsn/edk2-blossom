# Status
Boots to UEFI Shell

SimpleFbDxe works pretty smoothly

UART works too, but for it, you must first disable FrameBufferSerialLib in MT6765Pkg.dsc

MSDC0 is under construction; CMD*(17,2, or 13) failure(may be), probably some clock/voltage failure

# Building
First, clone EDK2:
```
git clone https://github.com/tianocore/edk2 --recursive -b edk2-stable202302
git clone https://github.com/tianocore/edk2-platforms.git
```
First run `./firstrun.sh` <br>
Then, `./build.sh` <br>
This should make a boot-uefi.img to be flashed/booted via fastboot.

# Credits
[edk2-exynos7885](https://github.com/sonic011gamer/edk2-exynos7885/) - edk2-mt6765 based port
[edk2-mt6765](https://github.com/xiaomi-blossom-dev/edk2-mt6765) - forked
