# Status

Work in progress. Many things is broken. <br>
## Main tasks:
**MSDC0, MSDC1, Block IO working**; <br>
**Custom** *`bootaa64.efi`* **booting from EMMC**; <br>
**Watchdog timer kicking properly**; <br>

# Features: 
| Feature  | Status | Description |
| ------------- | ------------- | ------------------------------ |
| Flashing |  Works | You can flash image via `fastboot flash boot [image.img]`. |
| SimpleFB |  Works | SimpleFBDxe works fine. |
| Logging |  Partially | FrameBufferSerialLib works, but colors/ESC sequence is broken. |
| EMMC |  Broken | WIP; CMD*(17,2, or 13) failure(may be), probably some clock/voltage failure. |
| SDMMC |  Not Tested | MSDC1?... idk |
| SPI |  Broken | SPI code/driver is missing. |
| I2C |  Broken | Same as a SPI. |
| Touchscreen | Broken | There are no NT36xxx/FT8006S driver right now. |
| BDS | Works | `BdsDxe` works smoothly. |
| UEFI Shell | Works | Boots into UEFI shell.
| GPIO | ??? | Driver is missing. |
| WDT | Broken | Watchdog timer is probably doesnt resets. Imma fix that. |
| etc. | ??? | no drivers... -_-|






# Building
First, clone EDK2:
```
git clone https://github.com/tianocore/edk2 --recursive -b edk2-stable202302
git clone https://github.com/tianocore/edk2-platforms.git
```
First run `./firstrun.sh`; <br>
Then, `./build.sh`  or `./baf.sh` for building and flasing;<br>
This should make a boot-uefi.img to be flashed/booted via fastboot.

# Credits
[edk2-exynos7885](https://github.com/sonic011gamer/edk2-exynos7885/) - edk2-mt6765 based port <br>
[edk2-mt6765](https://github.com/xiaomi-blossom-dev/edk2-mt6765) - forked
