# Status

Work in progress. Many things is broken. <br>
It can boot image from *`\EFI\BOOT\BOOTAA64.efi`* in a FAT partiton(FAT32 tested), but it doesn't takes any sence right now;<br>

## Main tasks/TODO:
- **~~MSDC0~~, MSDC1, ~~Block IO~~ working**; <br>
- **ACPI Tables**;<br>
- Make GIC, SPI/SMBus work.;<br>
- Fix the memory allocation/Memory Map<br>
- Make BDS see anything else except FAT
- Make GPIO buttons works as the keyboard arrow keys in a early state
- ~~**Custom** *`bootaa64.efi`* **booting from EMMC**(TESTED);~~ <br>
- ~~**Watchdog timer kicking/turning off properly**;~~ <br>

# Features: 
| Feature  | Status | Description |
| ------------- | ------------- | ------------------------------ |
| Flashing |  Works | You can flash image via `fastboot flash boot [image.img]`. |
| SimpleFB |  Partially | SimpleFBDxe works fine in a text output mode. However, it cant create GOP. |
| Logging |  Partially | FrameBufferSerialLib works, but coloring/ESC seqs are broken. |
| EMMC |  Works | Works at 50MHz 8-bit SDR; DDR is broken |
| DDR | Partially | Memory allocation doesn't works properly, see some logs below. |
| SDMMC |  Not Tested | MSDC1?... idk |
| SPI |  Broken | SPI code/driver is missing. |
| I2C |  Broken | Same as a SPI. |
| Touchscreen | Broken | There are no NT36xxx/FT8006S driver right now. |
| BDS | Works | `BdsDxe` works smoothly. |
| UEFI Shell | Works | Boots into UEFI shell. |
| GPIO | Broken | Driver is missing. |
| WDT | Works | WDT is turned off right now. |
| SMBIOS | Works | SMBIOS tables succesful creating |
| GIC/INTS | ??? | MT6762G uses GICv3, and we can make this thing work, but... |
| SYSIRQ | Broken | ...This SoC has proprietary SYSIRQ Interrupt controller, idk what can I do... >_< |
| etc. | ??? | no drivers... -_-|



### Issues
EMMC DDR is broken. Not gonna fix it cuz idc.<br>
There are memory allocation failure/FDT transfer issue. GRUB/EDKII log parts:
```
Error: Image at 0004EC660000 start failed: Out of resources
error: invalid device tree.
EFI stub: ERROR: Failed to relocate kernel
EFI stub: ERROR: Failed to relocate kernel
Failed to boot both default and fallback entries.
```

# Building
First, clone EDK2:
```
git clone https://github.com/tianocore/edk2 --recursive -b edk2-stable202302
git clone https://github.com/tianocore/edk2-platforms.git
```
First run `./firstrun.sh`; <br>
Then, `./build.sh`  or `./baf.sh` for building and flasing;<br>
This should make a boot-uefi.img to be flashed/booted via fastboot.

# Notes
* RTFM;
* KISS;
* DO NOT fake RCA/Response/CMD. (except multi-block operation emulation)

# Credits
[edk2-exynos7885](https://github.com/sonic011gamer/edk2-exynos7885/) - edk2-mt6765 <br>
[edk2-mt6765](https://github.com/xiaomi-blossom-dev/edk2-mt6765) - forked <br>
[edk2-mtk](https://github.com/linux-mediatek/edk2-mtk) - idea source, WDT turn off code

### Feedback:<br>
[EMAIL](mailto:emilermekov@national.shitposting.agency)<br>
[Telegram](https://t.me/thiscoolworld)<br>
