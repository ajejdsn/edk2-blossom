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
| Logging |  Partially | FrameBufferSerialLib works, but coloring/ESC seqs are broken. |
| EMMC |  Partially | WIP; Read some logs below |
| SDMMC |  Not Tested | MSDC1?... idk |
| SPI |  Broken | SPI code/driver is missing. |
| I2C |  Broken | Same as a SPI. |
| Touchscreen | Broken | There are no NT36xxx/FT8006S driver right now. |
| BDS | Works | `BdsDxe` works smoothly. |
| UEFI Shell | Works | Boots into UEFI shell.
| GPIO | ??? | Driver is missing. |
| WDT | WIP | Working at WDT disable/reset, can't find proper address |
| SMBIOS | Works | SMBIOS tables succesful creating |
| etc. | ??? | no drivers... -_-|



### Latest problem
Now it initializes eMMC, reads CID/EXT_CSD properly, and even can read LBA0-LBA2, but I gotta problem with LBA3.<br>
It has zero length, and, probably everything is fucked up because of that.<br>
Also, MmcDxe is trying to send CMD65554, probably some bug that I need to fix.<br>
Log: <br>
```
CMD18 ARG=00000003 RAW=02000892
CMD18 timeout
MSDC0 cmd-timeout-pending: CFG=00000299 IOCON=00000010 PS=81000002 INT=00000200 INTEN=0000B700 FIFO=00000080 SDC_CFG=09020000 SDC_STS=00100000 PB0=403C0007 PB1=FFFA4340 PB2=3489180B CMD=02000892 ARG=00000003
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
* DO NOT fake RCA/Response/CMD.

# Credits
[edk2-exynos7885](https://github.com/sonic011gamer/edk2-exynos7885/) - edk2-mt6765 based port <br>
[edk2-mt6765](https://github.com/xiaomi-blossom-dev/edk2-mt6765) - forked


### Feedback:<br>
[EMAIL](mailto:emilermekov@national.shitposting.agency)<br>
[Telegram](https://t.me/thiscoolworld)<br>
